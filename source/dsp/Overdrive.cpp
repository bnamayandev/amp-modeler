#include "Overdrive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace evil {
namespace {
constexpr float bias =
    0.15f; // asymmetry: adds the even harmonics of a real gain stage
const float biasOffset = std::tanh(bias);

float toneToFrequency(float tone) noexcept {
  return 600.0f * std::pow(2.0f, 5.0f * tone); // 600 Hz .. 19.2 kHz
}
} // namespace

void Overdrive::prepare(double newSampleRate, int maxBlockSize) {
  sampleRate = newSampleRate;
  for (size_t i = 0; i < oversamplers.size(); ++i) {
    if (i == 0)
      continue; // 1x runs the waveshaper directly
    oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>>(
        1, i, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true,
        false);
    oversamplers[i]->initProcessing((size_t)maxBlockSize);
  }

  preHighPass.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 110.0));
  preEmphasis.setCoeffs(BiquadCoeffs::peak(sampleRate, 900.0, 0.7, 6.0));
  dcBlocker.setCoeffs(BiquadCoeffs::onePoleHighPass(sampleRate, 15.0));
  fizzLowPass.setCoeffs(BiquadCoeffs::lowPass(sampleRate, 9000.0, 0.707));

  driveGain.reset(sampleRate, 0.03);
  toneSmoothed.reset(sampleRate, 0.03);
  switchFadeLength = std::max(1, (int)(0.005 * sampleRate));
  preShape.assign((size_t)maxBlockSize, 0.0f);
  switchBuffer.assign((size_t)maxBlockSize, 0.0f);
  makeupGain.assign((size_t)maxBlockSize, 1.0f);
  reset();
}

void Overdrive::reset() noexcept {
  for (auto &os : oversamplers)
    if (os != nullptr)
      os->reset();
  for (auto *f :
       {&preHighPass, &preEmphasis, &dcBlocker, &toneLowPass, &fizzLowPass})
    f->reset();
  switchFadePosition = switchFadeLength; // no switch in progress
  previousOversampling = currentOversampling;
  primed = false;
}

int Overdrive::getLatencySamples(int oversamplingIndex) const noexcept {
  const auto index =
      (size_t)std::clamp(oversamplingIndex, 0, numOversamplingChoices - 1);
  if (oversamplers[index] == nullptr)
    return 0;
  return (int)std::lround(oversamplers[index]->getLatencyInSamples());
}

void Overdrive::shape(float *data, int n, int oversamplingIndex) noexcept {
  auto waveshape = [](float *x, size_t count) {
    for (size_t i = 0; i < count; ++i)
      x[i] = std::tanh(x[i] + bias) - biasOffset;
  };

  auto &os = oversamplers[(size_t)oversamplingIndex];
  if (os == nullptr) {
    waveshape(data, (size_t)n);
    return;
  }

  float *channels[] = {data};
  juce::dsp::AudioBlock<float> block(channels, 1, (size_t)n);
  auto up = os->processSamplesUp(block);
  waveshape(up.getChannelPointer(0), up.getNumSamples());
  os->processSamplesDown(block);
}

void Overdrive::process(float *data, int n, float driveDb, float tone,
                        int oversamplingIndex) noexcept {
  oversamplingIndex =
      std::clamp(oversamplingIndex, 0, numOversamplingChoices - 1);
  if (!primed) {
    currentOversampling = previousOversampling = oversamplingIndex;
    driveGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(driveDb));
    toneSmoothed.setCurrentAndTargetValue(tone);
    primed = true;
  }
  if (oversamplingIndex != currentOversampling) {
    // Start the new path from clean filter state and fade over to it.
    previousOversampling = currentOversampling;
    currentOversampling = oversamplingIndex;
    if (auto &os = oversamplers[(size_t)currentOversampling])
      os->reset();
    switchFadePosition = 0;
  }

  driveGain.setTargetValue(juce::Decibels::decibelsToGain(driveDb));
  toneSmoothed.setTargetValue(tone);

  // Linear pre-processing at the base rate: tighten the lows, push the mids,
  // apply drive.
  for (int i = 0; i < n; ++i) {
    const float g = driveGain.getNextValue();
    makeupGain[(size_t)i] = 1.0f / std::sqrt(g);
    preShape[(size_t)i] =
        preEmphasis.processSample(preHighPass.processSample(data[i])) * g;
  }

  std::memcpy(data, preShape.data(), sizeof(float) * (size_t)n);
  shape(data, n, currentOversampling);

  if (switchFadePosition < switchFadeLength) {
    std::memcpy(switchBuffer.data(), preShape.data(),
                sizeof(float) * (size_t)n);
    shape(switchBuffer.data(), n, previousOversampling);
    for (int i = 0; i < n; ++i) {
      const float t = std::min(1.0f, (float)(switchFadePosition + i + 1) /
                                         (float)switchFadeLength);
      data[i] =
          switchBuffer[(size_t)i] + t * (data[i] - switchBuffer[(size_t)i]);
    }
    switchFadePosition += n;
  }

  // Post: remove the DC the asymmetry creates, tone control, fixed fizz filter,
  // and make-up gain so higher drive gets more saturated rather than ~30 dB
  // louder.
  const bool toneMoving = toneSmoothed.isSmoothing();
  if (!toneMoving)
    toneLowPass.setCoeffs(BiquadCoeffs::onePoleLowPass(
        sampleRate, toneToFrequency(toneSmoothed.getTargetValue())));

  for (int i = 0; i < n; ++i) {
    if (toneMoving && (i & 15) == 0) {
      const float t = toneSmoothed.skip(std::min(16, n - i));
      toneLowPass.setCoeffs(
          BiquadCoeffs::onePoleLowPass(sampleRate, toneToFrequency(t)));
    }
    const float y = toneLowPass.processSample(dcBlocker.processSample(data[i]));
    data[i] = fizzLowPass.processSample(y) * makeupGain[(size_t)i];
  }
}
} // namespace evil
