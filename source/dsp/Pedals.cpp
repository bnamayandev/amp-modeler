#include "Pedals.h"

#include <cmath>
#include <numbers>

namespace evil {
namespace {
float coeffForTime(double sampleRate, double seconds) {
  return (float)(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}

/** First-order antiderivative anti-aliasing (Parker et al., DAFx 2016): the
   waveshaper's output is the mean of f over the segment between consecutive
   inputs, F(x1)-F(x0) / (x1-x0), which suppresses most aliasing without
   oversampling. Costs half a sample of delay. */
template <typename F, typename Fn>
float antiderivativeShape(float x, float &previous, float &previousF,
                          F antiderivative, Fn shape) noexcept {
  const double fx = antiderivative((double)x);
  const double diff = (double)x - previous;
  const float y = std::abs(diff) > 1.0e-4
                      ? (float)((fx - previousF) / diff)
                      : shape(0.5f * (x + previous));
  previous = x;
  previousF = (float)fx;
  return y;
}

/** Hard clipper at +/-1 and its antiderivative, for antiderivativeShape. */
constexpr auto hardClip = [](float v) { return std::clamp(v, -1.0f, 1.0f); };
constexpr auto hardClipAntiderivative = [](double v) {
  const double a = std::abs(v);
  return a <= 1.0 ? 0.5 * v * v : a - 0.5;
};

/** Advances an LFO phase in [0, 1) by one sample and returns its sine. */
float stepSine(double &phase, float rateHz, double sampleRate) noexcept {
  phase += rateHz / sampleRate;
  phase -= std::floor(phase);
  return (float)std::sin(2.0 * std::numbers::pi * phase);
}

/** Steps a smoothed tone value and refilters every 16 samples while it moves.
 */
template <typename Smoothed, typename Make>
void updateToneFilter(Smoothed &tone, Biquad &filter, int i, int n,
                      Make make) noexcept {
  if ((i & 15) != 0)
    return;
  if (tone.isSmoothing() || i == 0)
    filter.setCoeffs(make(tone.isSmoothing() ? tone.skip(std::min(16, n - i))
                                             : tone.getTargetValue()));
}
} // namespace

// --- Wah --------------------------------------------------------------------

void WahPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  attackCoeff = coeffForTime(sampleRate, 0.004);
  releaseCoeff = coeffForTime(sampleRate, 0.15);
  for (auto *v : {&position, &autoAmount, &resonance})
    v->reset(sampleRate, 0.03);
  reset();
}

void WahPedal::reset() noexcept {
  ic1 = ic2 = envelope = 0.0f;
  primed = false;
}

void WahPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    position.setCurrentAndTargetValue(s.position);
    autoAmount.setCurrentAndTargetValue(s.autoAmount);
    resonance.setCurrentAndTargetValue(s.resonance);
    primed = true;
  }
  position.setTargetValue(s.position);
  autoAmount.setTargetValue(s.autoAmount);
  resonance.setTargetValue(s.resonance);

  for (int i = 0; i < n; ++i) {
    const float x = data[i];
    const float rectified = std::abs(x);
    envelope += (rectified > envelope ? attackCoeff : releaseCoeff) *
                (rectified - envelope);
    const float pos = position.getNextValue(), amount = autoAmount.getNextValue(),
                res = resonance.getNextValue();
    if ((i & 7) == 0) { // the sweep is slow next to the sample rate
      // An envelope from -45 to -10 dB opens the filter from the pedal
      // position to the toe.
      const float opened = std::clamp(
          (juce::Decibels::gainToDecibels(envelope, -100.0f) + 45.0f) / 35.0f,
          0.0f, 1.0f);
      const float sweep = std::clamp(pos + amount * (1.0f - pos) * opened, 0.0f, 1.0f);
      const float q = 1.5f * std::pow(6.0f, res); // 1.5..9
      const double g = std::tan(std::numbers::pi * 350.0 *
                                std::pow(2200.0 / 350.0, (double)sweep) / sampleRate);
      k = 1.0f / q;
      a1 = (float)(1.0 / (1.0 + g * (g + k)));
      a2 = (float)g * a1;
      a3 = (float)g * a2;
      // k * band-pass peaks at unity; a narrower peak gets a little louder,
      // as on a real wah.
      outputGain = 1.2f * std::sqrt(q) * k;
    }
    // Trapezoidal state-variable filter (Zavalishin / Simper).
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    data[i] = outputGain * v1;
  }
}

// --- Compressor -------------------------------------------------------------

void CompressorPedal::prepare(double sampleRate) {
  attackCoeff = coeffForTime(sampleRate, 0.008); // lets the pick attack through
  releaseCoeff = coeffForTime(sampleRate, 0.18);
  peakReleaseCoeff = coeffForTime(sampleRate, 0.04); // rides over a cycle
  sustain.reset(sampleRate, 0.05);
  blend.reset(sampleRate, 0.05);
  level.reset(sampleRate, 0.05);
  reset();
}

void CompressorPedal::reset() noexcept {
  peak = reductionDb = 0.0f;
  primed = envelopePrimed = false;
}

void CompressorPedal::process(float *data, int n, const Settings &s) noexcept {
  const auto levelGain = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    sustain.setCurrentAndTargetValue(s.sustain);
    blend.setCurrentAndTargetValue(s.blend);
    level.setCurrentAndTargetValue(levelGain);
    primed = true;
  }
  sustain.setTargetValue(s.sustain);
  blend.setTargetValue(s.blend);
  level.setTargetValue(levelGain);

  constexpr float kneeDb = 6.0f;
  constexpr float referenceDb = -12.0f; // make-up keeps this level unchanged
  for (int i = 0; i < n; ++i) {
    const float sus = sustain.getNextValue();
    const float thresholdDb = -8.0f - 34.0f * sus;
    const float slope = 1.0f - 1.0f / (2.0f + 8.0f * sus); // 1 - 1/ratio

    const float x = data[i];
    const float rectified = std::abs(x);
    peak = rectified > peak ? rectified
                            : peak + peakReleaseCoeff * (rectified - peak);
    const float over =
        juce::Decibels::gainToDecibels(peak, -120.0f) - thresholdDb;
    float target = 0.0f;
    if (over >= 0.5f * kneeDb)
      target = slope * over;
    else if (over > -0.5f * kneeDb)
      target = slope * (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) /
               (2.0f * kneeDb);
    if (!envelopePrimed) { // start compressed rather than with a burst of make-up
      reductionDb = target;
      envelopePrimed = true;
    }
    reductionDb += (target > reductionDb ? attackCoeff : releaseCoeff) *
                   (target - reductionDb);

    const float makeupDb = slope * std::max(0.0f, referenceDb - thresholdDb);
    const float wet = x * juce::Decibels::decibelsToGain(makeupDb - reductionDb);
    data[i] = (x + blend.getNextValue() * (wet - x)) * level.getNextValue();
  }
}

// --- Fuzz -------------------------------------------------------------------

void FuzzPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  inputHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 120.0));
  inputLowPass.setCoeffs(BiquadCoeffs::onePoleLowPass(sampleRate, 4500.0));
  dcBlocker.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 20.0));
  toneLow.setCoeffs(BiquadCoeffs::onePoleLowPass(sampleRate, 500.0));
  toneHigh.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 1100.0));
  gain.reset(sampleRate, 0.03);
  level.reset(sampleRate, 0.05);
  tone.reset(sampleRate, 0.03);
  reset();
}

void FuzzPedal::reset() noexcept {
  for (auto *f :
       {&inputHighPass, &inputLowPass, &dcBlocker, &toneLow, &toneHigh})
    f->reset();
  previous = previousAntiderivative = 0.0f;
  primed = false;
}

void FuzzPedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr float bias = 0.3f; // asymmetric clipping: even harmonics
  constexpr float outputScale = 0.3f;
  const auto gainTarget = juce::Decibels::decibelsToGain(12.0f + 42.0f * s.fuzz);
  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    gain.setCurrentAndTargetValue(gainTarget);
    level.setCurrentAndTargetValue(levelTarget);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  gain.setTargetValue(gainTarget);
  level.setTargetValue(levelTarget);
  tone.setTargetValue(s.tone);

  for (int i = 0; i < n; ++i) {
    const float v =
        inputLowPass.processSample(inputHighPass.processSample(data[i])) *
            gain.getNextValue() +
        bias;
    float y = antiderivativeShape(v, previous, previousAntiderivative,
                                  hardClipAntiderivative, hardClip);
    y = dcBlocker.processSample(y);

    // Big-muff style tone: crossfade a low-pass and a high-pass, which leaves
    // the mids scooped in the middle of the knob.
    const float t = tone.getNextValue();
    const float lows = toneLow.processSample(y), highs = toneHigh.processSample(y);
    data[i] = (lows + t * (highs - lows)) * outputScale * level.getNextValue();
  }
}

// --- Overdrive --------------------------------------------------------------

void OverdrivePedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  clipHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 720.0));
  clipLowPass.setCoeffs(BiquadCoeffs::onePoleLowPass(sampleRate, 5600.0));
  gain.reset(sampleRate, 0.03);
  level.reset(sampleRate, 0.05);
  tone.reset(sampleRate, 0.03);
  reset();
}

void OverdrivePedal::reset() noexcept {
  for (auto *f : {&clipHighPass, &clipLowPass, &toneLowPass})
    f->reset();
  previous = previousAntiderivative = 0.0f;
  primed = false;
}

void OverdrivePedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr float clipLevel = 0.5f;   // diode ceiling relative to the dry path
  constexpr float outputScale = 0.6f; // about unity at the default drive
  const auto gainTarget = juce::Decibels::decibelsToGain(6.0f + 36.0f * s.drive);
  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    gain.setCurrentAndTargetValue(gainTarget);
    level.setCurrentAndTargetValue(levelTarget);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  gain.setTargetValue(gainTarget);
  level.setTargetValue(levelTarget);
  tone.setTargetValue(s.tone);

  const auto tanhShape = [](float v) { return std::tanh(v); };
  const auto logCosh = [](double v) { // antiderivative of tanh, overflow-safe
    const double a = std::abs(v);
    return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
  };
  const auto toneCoeffs = [this](float t) {
    return BiquadCoeffs::onePoleLowPass(sampleRate,
                                        700.0 * std::pow(2.0, 3.5 * t));
  };

  for (int i = 0; i < n; ++i) {
    updateToneFilter(tone, toneLowPass, i, n, toneCoeffs);
    const float x = data[i];
    const float v = clipLowPass.processSample(clipHighPass.processSample(x)) *
                    gain.getNextValue();
    const float clipped = antiderivativeShape(v, previous, previousAntiderivative,
                                              logCosh, tanhShape);
    const float y = toneLowPass.processSample(x + clipLevel * clipped);
    data[i] = y * outputScale * level.getNextValue();
  }
}

// --- Distortion -------------------------------------------------------------

void DistortionPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  inputHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 60.0));
  trebleHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 1500.0));
  gain.reset(sampleRate, 0.03);
  level.reset(sampleRate, 0.05);
  bandwidth.reset(sampleRate, 0.03);
  tone.reset(sampleRate, 0.03);
  reset();
}

void DistortionPedal::reset() noexcept {
  for (auto *f : {&inputHighPass, &trebleHighPass, &opAmpLowPass, &toneLowPass})
    f->reset();
  previous = previousAntiderivative = 0.0f;
  primed = false;
}

void DistortionPedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr float trebleLift = 2.0f; // the op-amp's second feedback leg
  constexpr float outputScale = 0.2f; // about unity at the default setting
  const auto gainDb = [](float d) { return 12.0f + 40.0f * d; };
  const auto gainTarget = juce::Decibels::decibelsToGain(gainDb(s.distortion));
  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    gain.setCurrentAndTargetValue(gainTarget);
    level.setCurrentAndTargetValue(levelTarget);
    bandwidth.setCurrentAndTargetValue(s.distortion);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  gain.setTargetValue(gainTarget);
  level.setTargetValue(levelTarget);
  bandwidth.setTargetValue(s.distortion);
  tone.setTargetValue(s.tone);

  const auto opAmpCoeffs = [this, gainDb](float d) {
    // Gain-bandwidth limit: past moderate settings, more gain is less treble.
    const double cutoff =
        8.0e5 / juce::Decibels::decibelsToGain((double)gainDb(d));
    return BiquadCoeffs::onePoleLowPass(sampleRate,
                                        std::clamp(cutoff, 2000.0, 16000.0));
  };
  const auto toneCoeffs = [this](float t) {
    return BiquadCoeffs::onePoleLowPass(sampleRate,
                                        500.0 * std::pow(2.0, 4.6 * t));
  };

  for (int i = 0; i < n; ++i) {
    updateToneFilter(bandwidth, opAmpLowPass, i, n, opAmpCoeffs);
    updateToneFilter(tone, toneLowPass, i, n, toneCoeffs);
    const float x = inputHighPass.processSample(data[i]);
    const float lifted = x + trebleLift * trebleHighPass.processSample(x);
    const float v = opAmpLowPass.processSample(lifted * gain.getNextValue());
    const float clipped = antiderivativeShape(v, previous, previousAntiderivative,
                                              hardClipAntiderivative, hardClip);
    data[i] = toneLowPass.processSample(clipped) * outputScale *
              level.getNextValue();
  }
}

// --- Boost ------------------------------------------------------------------

void BoostPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  gain.reset(sampleRate, 0.05);
  bass.reset(sampleRate, 0.05);
  treble.reset(sampleRate, 0.05);
  reset();
}

void BoostPedal::reset() noexcept {
  bassShelf.reset();
  trebleShelf.reset();
  primed = false;
}

void BoostPedal::process(float *data, int n, const Settings &s) noexcept {
  const auto gainTarget = juce::Decibels::decibelsToGain(s.gainDb);
  if (!primed) {
    gain.setCurrentAndTargetValue(gainTarget);
    bass.setCurrentAndTargetValue(s.bassDb);
    treble.setCurrentAndTargetValue(s.trebleDb);
    primed = true;
  }
  gain.setTargetValue(gainTarget);
  bass.setTargetValue(s.bassDb);
  treble.setTargetValue(s.trebleDb);

  const auto bassCoeffs = [this](float db) {
    return BiquadCoeffs::lowShelf(sampleRate, 120.0, 0.707, db);
  };
  const auto trebleCoeffs = [this](float db) {
    return BiquadCoeffs::highShelf(sampleRate, 2500.0, 0.707, db);
  };
  for (int i = 0; i < n; ++i) {
    updateToneFilter(bass, bassShelf, i, n, bassCoeffs);
    updateToneFilter(treble, trebleShelf, i, n, trebleCoeffs);
    data[i] = trebleShelf.processSample(bassShelf.processSample(data[i])) *
              gain.getNextValue();
  }
}

// --- Chorus -----------------------------------------------------------------

void ChorusPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  line.setMaxDelay((int)std::ceil(0.012 * sampleRate) + 4);
  onset.prepare(sampleRate);
  wetLowPass.setCoeffs(BiquadCoeffs::onePoleLowPass(sampleRate, 7500.0));
  for (auto *v : {&rate, &depth, &mix})
    v->reset(sampleRate, 0.05);
  reset();
}

void ChorusPedal::reset() noexcept {
  line.clear();
  onset.restart();
  wetLowPass.reset();
  phase = 0.0;
  primed = false;
}

void ChorusPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    rate.setCurrentAndTargetValue(s.rateHz);
    depth.setCurrentAndTargetValue(s.depth);
    mix.setCurrentAndTargetValue(s.mix);
    primed = true;
  }
  rate.setTargetValue(s.rateHz);
  depth.setTargetValue(s.depth);
  mix.setTargetValue(s.mix);

  const auto msToSamples = (float)(0.001 * sampleRate);
  for (int i = 0; i < n; ++i) {
    const float lfo = stepSine(phase, rate.getNextValue(), sampleRate);
    const float delayMs = 7.0f + 3.0f * depth.getNextValue() * lfo; // 4..10 ms

    const float x = data[i];
    line.push(onset.process(x));
    const float wet = wetLowPass.processSample(line.hermite(delayMs * msToSamples));
    data[i] = x + mix.getNextValue() * (wet - x);
  }
}

// --- Delay ------------------------------------------------------------------

void DelayPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  line.setMaxDelay((int)std::ceil(maxTimeMs * 0.001 * sampleRate) + 4);
  onset.prepare(sampleRate);
  glideCoeff = coeffForTime(sampleRate, 0.12);
  loopLowPass.setCoeffs(BiquadCoeffs::onePoleLowPass(sampleRate, 4800.0));
  loopHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 90.0));
  feedback.reset(sampleRate, 0.05);
  mix.reset(sampleRate, 0.05);
  reset();
}

void DelayPedal::reset() noexcept {
  line.clear();
  onset.restart();
  loopLowPass.reset();
  loopHighPass.reset();
  primed = false;
}

void DelayPedal::process(float *data, int n, const Settings &s) noexcept {
  const float target =
      std::clamp(s.timeMs, 1.0f, maxTimeMs) * 0.001f * (float)sampleRate;
  const float fb = std::clamp(s.feedback, 0.0f, 0.95f);
  if (!primed) {
    delaySamples = target;
    feedback.setCurrentAndTargetValue(fb);
    mix.setCurrentAndTargetValue(s.mix);
    primed = true;
  }
  feedback.setTargetValue(fb);
  mix.setTargetValue(s.mix);

  for (int i = 0; i < n; ++i) {
    delaySamples += glideCoeff * (target - delaySamples);
    // Read before pushing: delay d from now is d - 1 pushes back.
    const float echo = line.hermite(delaySamples - 1.0f);
    const float returned =
        loopLowPass.processSample(loopHighPass.processSample(echo));
    const float x = data[i];
    line.push(
        std::tanh(onset.process(x) + feedback.getNextValue() * returned));
    data[i] = x + mix.getNextValue() * echo;
  }
}
// --- Phaser -----------------------------------------------------------------

void PhaserPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  for (auto *v : {&rate, &depth, &feedback})
    v->reset(sampleRate, 0.05);
  reset();
}

void PhaserPedal::reset() noexcept {
  stages.fill(0.0f);
  last = 0.0f;
  phase = 0.0;
  primed = false;
}

void PhaserPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    rate.setCurrentAndTargetValue(s.rateHz);
    depth.setCurrentAndTargetValue(s.depth);
    feedback.setCurrentAndTargetValue(s.feedback);
    primed = true;
  }
  rate.setTargetValue(s.rateHz);
  depth.setTargetValue(s.depth);
  feedback.setTargetValue(s.feedback);

  for (int i = 0; i < n; ++i) {
    const float lfo = stepSine(phase, rate.getNextValue(), sampleRate);
    // The stages' break frequency sweeps exponentially around 520 Hz:
    // 120 Hz to 2.2 kHz at full depth.
    const double f = 520.0 * std::exp2(2.1 * depth.getNextValue() * lfo);
    const double t = std::tan(std::numbers::pi * f / sampleRate);
    const float a = (float)((t - 1.0) / (t + 1.0));

    const float x = data[i];
    float v = x + 0.7f * feedback.getNextValue() * last;
    for (auto &z : stages) { // first-order all-pass, transposed direct form
      const float y = a * v + z;
      z = v - a * y;
      v = y;
    }
    last = v;
    data[i] = 0.5f * (x + v);
  }
}

// --- Flanger ----------------------------------------------------------------

void FlangerPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  line.setMaxDelay((int)std::ceil(0.008 * sampleRate) + 4);
  onset.prepare(sampleRate);
  for (auto *v : {&rate, &depth, &feedback})
    v->reset(sampleRate, 0.05);
  reset();
}

void FlangerPedal::reset() noexcept {
  line.clear();
  onset.restart();
  phase = 0.0;
  primed = false;
}

void FlangerPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    rate.setCurrentAndTargetValue(s.rateHz);
    depth.setCurrentAndTargetValue(s.depth);
    feedback.setCurrentAndTargetValue(s.feedback);
    primed = true;
  }
  rate.setTargetValue(s.rateHz);
  depth.setTargetValue(s.depth);
  feedback.setTargetValue(s.feedback);

  const auto msToSamples = (float)(0.001 * sampleRate);
  for (int i = 0; i < n; ++i) {
    const float lfo = stepSine(phase, rate.getNextValue(), sampleRate);
    // Swept exponentially, so the notches move evenly in pitch: 0.7..7 ms.
    const float delayMs = 2.2f * std::exp2(1.7f * depth.getNextValue() * lfo);
    // Read before pushing: delay d from now is d - 1 pushes back.
    const float echo = line.hermite(delayMs * msToSamples - 1.0f);
    const float x = data[i];
    line.push(onset.process(x) + 0.85f * feedback.getNextValue() * echo);
    data[i] = 0.5f * (x + echo);
  }
}

// --- Tremolo ----------------------------------------------------------------

void TremoloPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  for (auto *v : {&rate, &depth, &shape})
    v->reset(sampleRate, 0.05);
  reset();
}

void TremoloPedal::reset() noexcept {
  phase = 0.25; // start at the top of the wave: full volume
  primed = false;
}

void TremoloPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    rate.setCurrentAndTargetValue(s.rateHz);
    depth.setCurrentAndTargetValue(s.depth);
    shape.setCurrentAndTargetValue(s.shape);
    primed = true;
  }
  rate.setTargetValue(s.rateHz);
  depth.setTargetValue(s.depth);
  shape.setTargetValue(s.shape);

  for (int i = 0; i < n; ++i) {
    const float lfo = stepSine(phase, rate.getNextValue(), sampleRate);
    // tanh squares the sine off as Shape goes up, normalised to keep +/-1.
    const float sh = shape.getNextValue();
    const float hardness = 0.05f + 12.0f * sh * sh;
    const float wave = std::tanh(hardness * lfo) / std::tanh(hardness);
    data[i] *= 1.0f - depth.getNextValue() * 0.5f * (1.0f - wave);
  }
}
} // namespace evil
