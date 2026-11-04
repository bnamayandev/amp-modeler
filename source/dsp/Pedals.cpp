#include "Pedals.h"

#include <cmath>
#include <numbers>

namespace evil {
namespace {
float coeffForTime(double sampleRate, double seconds) {
  return (float)(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}

/** Advances an LFO phase in [0, 1) by one sample and returns its sine. */
float stepSine(double &phase, float rateHz, double sampleRate) noexcept {
  phase += rateHz / sampleRate;
  phase -= std::floor(phase);
  return (float)std::sin(2.0 * std::numbers::pi * phase);
}

/** Steps a smoothed knob and calls apply(value) every 16 samples while it moves,
   and at the top of each block. */
template <typename Smoothed, typename Apply>
void updateKnob(Smoothed &knob, int i, int n, Apply apply) noexcept {
  if ((i & 15) != 0)
    return;
  if (knob.isSmoothing() || i == 0)
    apply(knob.isSmoothing() ? knob.skip(std::min(16, n - i))
                             : knob.getTargetValue());
}

/** updateKnob for a knob that sets one filter's coefficients. */
template <typename Smoothed, typename Make>
void updateToneFilter(Smoothed &tone, Biquad &filter, int i, int n,
                      Make make) noexcept {
  updateKnob(tone, i, n, [&](float value) { filter.setCoeffs(make(value)); });
}

using analog::audioTaper;
using analog::cornerHz;
using analog::railVolts;
using analog::voltsAtFullScale;
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

void FuzzPedal::prepare(double sampleRate) {
  oversampler.prepare(sampleRate);
  rate = sampleRate * oversampler.getFactor();
  // Each stage's 470 pF across its 470k feedback resistor.
  const auto feedback = BiquadCoeffs::onePoleLowPass(rate, cornerHz(470.0e3, 470.0e-12));
  inputLowPass.setCoeffs(feedback);
  feedback2.setCoeffs(feedback);
  feedback3.setCoeffs(feedback);
  // 1 uF into each stage's 10k input resistor.
  const auto coupled = BiquadCoeffs::onePoleHighPass(rate, cornerHz(10.0e3, 1.0e-6));
  coupling2.setCoeffs(coupled);
  coupling3.setCoeffs(coupled);
  level.reset(sampleRate, 0.05);
  fuzz.reset(sampleRate, 0.03);
  tone.reset(sampleRate, 0.03);
  reset();
}

void FuzzPedal::reset() noexcept {
  oversampler.reset();
  stage2.reset();
  stage3.reset();
  for (auto *f : {&inputLowPass, &feedback2, &feedback3, &coupling2, &coupling3,
                  &toneStack})
    f->reset();
  primed = false;
}

void FuzzPedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr double feedbackOhms = 470.0e3;
  constexpr double inputGain = feedbackOhms / 39.0e3; // first stage, no diodes
  constexpr double stage3Gain = feedbackOhms / 10.0e3;
  constexpr double recoveryGain = 10.0e3 / 2.2e3; // the output transistor
  constexpr double outputScale = 0.5;             // about unity at the defaults

  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    level.setCurrentAndTargetValue(levelTarget);
    fuzz.setCurrentAndTargetValue(s.fuzz);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  level.setTargetValue(levelTarget);
  fuzz.setTargetValue(s.fuzz);
  tone.setTargetValue(s.tone);

  const auto setSustain = [this](float knob) {
    // A 100k pot from the first stage to the second: its wiper's resistance adds
    // to the second stage's 10k input resistor, lowering that stage's gain. A real
    // pot never quite reaches zero, so the knob's bottom isn't silence.
    sustainGain = 0.01 + 0.99 * audioTaper(knob);
    stage2Gain = feedbackOhms / (10.0e3 + 100.0e3 * sustainGain * (1.0 - sustainGain));
  };
  const auto toneCoeffs = [this](float t) {
    // Nodal analysis of the tone stack: 39k and 10 nF to one end of a 100k pot,
    // 3.9 nF and 22k to the other, the wiper unloaded.
    constexpr double gLow = 1.0 / 39.0e3, gHigh = 1.0 / 22.0e3, gPot = 1.0 / 100.0e3;
    constexpr double cLow = 10.0e-9, cHigh = 3.9e-9;
    const double k = t;
    return BiquadCoeffs::fromAnalog(
        rate, (1.0 - k) * gLow * (gHigh + gPot) + k * gPot * gLow,
        cHigh * (gLow + gPot), k * cLow * cHigh,
        (gLow + gPot) * (gHigh + gPot) - gPot * gPot,
        cLow * (gHigh + gPot) + cHigh * (gLow + gPot), cLow * cHigh);
  };

  const int factor = oversampler.getFactor();
  std::array<double, analog::Oversampler::maxFactor> buffer;
  for (int i = 0; i < n; ++i) {
    updateKnob(fuzz, i, n, setSustain);
    updateToneFilter(tone, toneStack, i, n, toneCoeffs);

    oversampler.up((double)data[i] * voltsAtFullScale, buffer.data());
    for (int k = 0; k < factor; ++k) {
      const double first = std::clamp(
          inputLowPass.processSample(buffer[(size_t)k]) * inputGain, -railVolts,
          railVolts);
      const double second = stage2.process(
          feedback2.processSample(coupling2.processSample(first * sustainGain)) *
              stage2Gain,
          feedbackOhms);
      const double third = stage3.process(
          feedback3.processSample(coupling3.processSample(second)) * stage3Gain,
          feedbackOhms);
      buffer[(size_t)k] = toneStack.processSample(third);
    }
    data[i] = (float)(oversampler.down(buffer.data()) * recoveryGain * outputScale /
                      voltsAtFullScale) *
              level.getNextValue();
  }
}

// --- Overdrive --------------------------------------------------------------

void OverdrivePedal::prepare(double sampleRate) {
  oversampler.prepare(sampleRate);
  rate = sampleRate * oversampler.getFactor();
  // 47 nF and 4.7k to ground set which frequencies the op-amp amplifies.
  clipHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(rate, cornerHz(4.7e3, 47.0e-9)));
  coupling.setCoeffs(BiquadCoeffs::onePoleHighPass(rate, 33.0));
  level.reset(sampleRate, 0.05);
  drive.reset(sampleRate, 0.03);
  tone.reset(sampleRate, 0.03);
  reset();
}

void OverdrivePedal::reset() noexcept {
  oversampler.reset();
  clipper.reset();
  for (auto *f : {&coupling, &clipHighPass, &clipLowPass, &toneFilter})
    f->reset();
  primed = false;
}

void OverdrivePedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr double groundLegOhms = 4.7e3;
  constexpr double outputScale = 0.6; // about unity on a guitar at the defaults

  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    level.setCurrentAndTargetValue(levelTarget);
    drive.setCurrentAndTargetValue(s.drive);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  level.setTargetValue(levelTarget);
  drive.setTargetValue(s.drive);
  tone.setTargetValue(s.tone);

  const auto setDrive = [this](float knob) {
    feedbackOhms = 51.0e3 + 500.0e3 * audioTaper(knob);
    clipLowPass.setCoeffs( // 51 pF across the feedback resistor
        BiquadCoeffs::onePoleLowPass(rate, cornerHz(feedbackOhms, 51.0e-12)));
  };
  const auto toneCoeffs = [this](float knob) {
    // Nodal analysis of the tone section. 1k and 220 nF (723 Hz) feed the
    // op-amp's + input. A 20k pot runs from there to its - input, with 220R and
    // 220 nF (3.3 kHz) from the wiper to ground, and 1k of feedback.
    constexpr double r1 = 1.0e3, c1 = 220.0e-9, rs = 220.0, c2 = 220.0e-9,
                     rf = 1.0e3, pot = 20.0e3;
    // The knob law is an S rather than a linear pot's straight line: fitted to
    // captures of a real one with its Tone at 5 and at 8.
    const double x = knob;
    const double k = x * x * x * (x * (6.0 * x - 15.0) + 10.0);
    const double parallel = pot * k * (1.0 - k); // both sides of the wiper
    const double t1 = c1 * r1, t2 = c2 * (rs + parallel);
    return BiquadCoeffs::fromAnalog(rate, 1.0, c2 * (rs + parallel + rf * k), 0.0,
                                    1.0, t1 + t2 + r1 * c2 * (1.0 - k), t1 * t2);
  };

  const int factor = oversampler.getFactor();
  std::array<double, analog::Oversampler::maxFactor> buffer;
  for (int i = 0; i < n; ++i) {
    updateKnob(drive, i, n, setDrive);
    updateToneFilter(tone, toneFilter, i, n, toneCoeffs);

    oversampler.up((double)data[i] * voltsAtFullScale, buffer.data());
    for (int k = 0; k < factor; ++k) {
      const double in = coupling.processSample(buffer[(size_t)k]);
      // The op-amp makes the voltage across its feedback path whatever passes
      // the current through its ground leg: unity plus that.
      const double overDiodes = clipper.process(
          clipLowPass.processSample(clipHighPass.processSample(in) * feedbackOhms /
                                    groundLegOhms),
          feedbackOhms);
      buffer[(size_t)k] = toneFilter.processSample(
          std::clamp(in + overDiodes, -railVolts, railVolts));
    }
    data[i] = (float)(oversampler.down(buffer.data()) * outputScale / voltsAtFullScale) *
              level.getNextValue();
  }
}

// --- Distortion -------------------------------------------------------------

void DistortionPedal::prepare(double sampleRate) {
  oversampler.prepare(sampleRate);
  rate = sampleRate * oversampler.getFactor();
  level.reset(sampleRate, 0.05);
  distortion.reset(sampleRate, 0.03);
  tone.reset(sampleRate, 0.03);
  reset();
}

void DistortionPedal::reset() noexcept {
  oversampler.reset();
  gainStage.reset();
  clipper.reset();
  toneLowPass.reset();
  primed = false;
}

void DistortionPedal::process(float *data, int n, const Settings &s) noexcept {
  constexpr double seriesOhms = 1.0e3; // from the op-amp to the diodes
  constexpr double outputScale = 0.6;  // about unity at the defaults

  const auto levelTarget = juce::Decibels::decibelsToGain(s.levelDb);
  if (!primed) {
    level.setCurrentAndTargetValue(levelTarget);
    distortion.setCurrentAndTargetValue(s.distortion);
    tone.setCurrentAndTargetValue(s.tone);
    primed = true;
  }
  level.setTargetValue(levelTarget);
  distortion.setTargetValue(s.distortion);
  tone.setTargetValue(s.tone);

  const auto setDistortion = [this](float knob) {
    // Feedback R over two legs to ground: 560R + 4.7 uF (60 Hz) and 47R + 2.2 uF
    // (1.5 kHz). N(s) = 1 + R * Y(s) is the gain an ideal op-amp would give. An
    // LM308 with its 30 pF manages 30 dB at 10 kHz, so it has a gain-bandwidth
    // of 316 kHz, and the closed loop is N / (1 + s N / wt).
    constexpr double c1 = 4.7e-6, c2 = 2.2e-6, t1 = 560.0 * c1, t2 = 47.0 * c2;
    constexpr double inverseWt = 1.0 / (2.0 * juce::MathConstants<double>::pi * 316.0e3);
    const double r = 100.0e3 * audioTaper(knob);
    const double p1 = t1 + t2 + r * (c1 + c2);
    const double p2 = t1 * t2 + r * (c1 * t2 + c2 * t1);
    gainStage.setAnalog(rate, 1.0, p1, p2, 1.0, t1 + t2 + inverseWt,
                        t1 * t2 + p1 * inverseWt, p2 * inverseWt);
  };
  const auto toneCoeffs = [this](float t) {
    // 1.5k and the 100k Filter pot into 3.3 nF: 32 kHz down to 475 Hz. The real
    // knob darkens clockwise; here 0 is dark.
    return BiquadCoeffs::onePoleLowPass(
        rate, cornerHz(1.5e3 + 100.0e3 * audioTaper(1.0 - t), 3.3e-9));
  };

  const int factor = oversampler.getFactor();
  std::array<double, analog::Oversampler::maxFactor> buffer;
  for (int i = 0; i < n; ++i) {
    updateKnob(distortion, i, n, setDistortion);
    updateToneFilter(tone, toneLowPass, i, n, toneCoeffs);

    oversampler.up((double)data[i] * voltsAtFullScale, buffer.data());
    for (int k = 0; k < factor; ++k) {
      buffer[(size_t)k] = toneLowPass.processSample(clipper.process(
          gainStage.process(buffer[(size_t)k]), seriesOhms, railVolts));
    }
    data[i] = (float)(oversampler.down(buffer.data()) * outputScale / voltsAtFullScale) *
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
