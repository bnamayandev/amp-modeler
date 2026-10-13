#include "ReverbPedal.h"

#include <cmath>
#include <numbers>

namespace evil {
namespace {
constexpr double paperRate = 29761.0; // the rate the paper's lengths are in

float onePoleCoeff(double sampleRate, double freq) {
  return (float)(1.0 - std::exp(-2.0 * std::numbers::pi * freq / sampleRate));
}
} // namespace

int ReverbPedal::scaled(int samplesAt29761) const noexcept {
  return std::max(1, (int)std::lround(samplesAt29761 * sampleRate / paperRate));
}

void ReverbPedal::prepare(double newSampleRate) {
  sampleRate = newSampleRate;
  excursion = (float)(16.0 * sampleRate / paperRate);

  predelaySamples = (int)(0.012 * sampleRate);
  predelay.setMaxDelay(predelaySamples + 1);
  onset.prepare(sampleRate);
  inputHighPassCoeff = onePoleCoeff(sampleRate, 120.0);
  bandwidthCoeff = onePoleCoeff(sampleRate, 9000.0);

  const int diffuserLengths[] = {142, 107, 379, 277};
  for (size_t i = 0; i < inputDiffusers.size(); ++i) {
    inputDiffusers[i].length = scaled(diffuserLengths[i]);
    inputDiffusers[i].line.setMaxDelay(inputDiffusers[i].length + 1);
  }

  const auto setUp = [this](TankHalf &h, int modulated, int first,
                            int diffuser, int second) {
    h.modulated.length = scaled(modulated);
    h.modulated.line.setMaxDelay(h.modulated.length + 2 * (int)excursion + 4);
    h.firstLength = scaled(first);
    h.first.setMaxDelay(h.firstLength + 1);
    h.diffuser.length = scaled(diffuser);
    h.diffuser.line.setMaxDelay(h.diffuser.length + 1);
    h.secondLength = scaled(second);
    h.second.setMaxDelay(h.secondLength + 1);
  };
  setUp(left, 672, 4453, 1800, 3720);
  setUp(right, 908, 4217, 2656, 3163);

  // Output taps (Dattorro, table 2): the left set, then the right set.
  const int tapsAtPaperRate[] = {266,  2974, 1913, 1996, 1990, 187,  1066,
                                 353,  3627, 1228, 2673, 2111, 335,  121};
  for (size_t i = 0; i < taps.size(); ++i)
    taps[i] = scaled(tapsAtPaperRate[i]);

  lfoIncrement = 0.9 / sampleRate; // ~1 Hz tank modulation
  for (auto *v : {&decay, &tone, &mix})
    v->reset(sampleRate, 0.05);
  reset();
}

void ReverbPedal::reset() noexcept {
  predelay.clear();
  onset.restart();
  inputHighPass = bandwidth = 0.0f;
  for (auto &d : inputDiffusers)
    d.line.clear();
  for (auto *h : {&left, &right}) {
    h->modulated.line.clear();
    h->diffuser.line.clear();
    h->first.clear();
    h->second.clear();
    h->damping = 0.0f;
  }
  lfoPhase = 0.0;
  primed = false;
}

void ReverbPedal::processTankHalf(TankHalf &h, float in, float lfo) noexcept {
  // Decay diffusion 1 is negative in the paper; the modulated allpass reads
  // with linear interpolation around its nominal length.
  constexpr float decayDiffusion1 = -0.7f;
  const float delayed =
      h.modulated.line.linear((float)(h.modulated.length - 1) + excursion * lfo +
                              excursion);
  const float v = in - decayDiffusion1 * delayed;
  h.modulated.line.push(v);
  h.first.push(delayed + decayDiffusion1 * v);

  float x = h.first.at(h.firstLength);
  h.damping += dampingCoeff * (x - h.damping);
  x = h.damping * decayGain;
  const float decayDiffusion2 = std::clamp(decayGain + 0.15f, 0.25f, 0.5f);
  h.second.push(h.diffuser.process(x, decayDiffusion2));
}

void ReverbPedal::process(float *data, int n, const Settings &s) noexcept {
  if (!primed) {
    decay.setCurrentAndTargetValue(s.decay);
    tone.setCurrentAndTargetValue(s.tone);
    mix.setCurrentAndTargetValue(s.mix);
    primed = true;
  }
  decay.setTargetValue(s.decay);
  tone.setTargetValue(s.tone);
  mix.setTargetValue(s.mix);

  for (int i = 0; i < n; ++i) {
    if ((i & 15) == 0) { // control rate
      const int step = std::min(16, n - i);
      decayGain = 0.25f + 0.7f * decay.skip(step);
      dampingCoeff = onePoleCoeff(sampleRate,
                                  1500.0 * std::pow(2.0, 3.0 * tone.skip(step)));
    }

    const float x = data[i];

    // Input: predelay, high-pass, bandwidth, four diffusers.
    predelay.push(onset.process(x));
    float in = predelay.at(predelaySamples);
    inputHighPass += inputHighPassCoeff * (in - inputHighPass);
    in -= inputHighPass;
    bandwidth += bandwidthCoeff * (in - bandwidth);
    in = bandwidth;
    in = inputDiffusers[0].process(in, 0.75f);
    in = inputDiffusers[1].process(in, 0.75f);
    in = inputDiffusers[2].process(in, 0.625f);
    in = inputDiffusers[3].process(in, 0.625f);

    // Tank: each half is fed by the other's output from the previous sample.
    const float leftEnd = left.second.at(left.secondLength - 1) * decayGain;
    const float rightEnd = right.second.at(right.secondLength - 1) * decayGain;
    lfoPhase += lfoIncrement;
    lfoPhase -= std::floor(lfoPhase);
    const auto angle = 2.0 * std::numbers::pi * lfoPhase;
    processTankHalf(left, in + rightEnd, (float)std::sin(angle));
    processTankHalf(right, in + leftEnd, (float)std::cos(angle));

    // Left and right tap sets summed to mono.
    const float outLeft = right.first.at(taps[0]) + right.first.at(taps[1]) -
                          right.diffuser.line.at(taps[2]) +
                          right.second.at(taps[3]) - left.first.at(taps[4]) -
                          left.diffuser.line.at(taps[5]) -
                          left.second.at(taps[6]);
    const float outRight = left.first.at(taps[7]) + left.first.at(taps[8]) -
                           left.diffuser.line.at(taps[9]) +
                           left.second.at(taps[10]) - right.first.at(taps[11]) -
                           right.diffuser.line.at(taps[12]) -
                           right.second.at(taps[13]);
    const float wet = 0.3f * (outLeft + outRight);

    data[i] = x + mix.getNextValue() * wet;
  }
}
} // namespace evil
