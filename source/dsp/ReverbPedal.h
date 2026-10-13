#pragma once

#include "DelayLine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace evil {
/** Plate reverb after Dattorro, "Effect Design Part 1" (JAES 1997): four input
   diffusers feeding a figure-of-eight tank of modulated allpasses, delays and
   damping filters. The output sums both of the paper's tap sets to mono. The
   reverb is added on top of the unchanged dry signal. */
class ReverbPedal {
public:
  struct Settings {
    bool on = false;
    float decay = 0.5f; // 0..1
    float tone = 0.6f;  // 0 dark .. 1 bright
    float mix = 0.3f;   // reverb level relative to the dry signal
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  struct Allpass {
    DelayLine line;
    int length = 1;
    float process(float x, float gain) noexcept {
      const float delayed = line.at(length - 1);
      const float v = x - gain * delayed;
      line.push(v);
      return delayed + gain * v;
    }
  };

  struct TankHalf {
    Allpass modulated, diffuser;
    DelayLine first, second;
    int firstLength = 1, secondLength = 1;
    float damping = 0.0f;
  };

  int scaled(int samplesAt29761) const noexcept;
  void processTankHalf(TankHalf &half, float in, float lfo) noexcept;

  double sampleRate = 48000.0;
  DelayLine predelay;
  OnsetRamp onset;
  int predelaySamples = 0;
  float inputHighPass = 0.0f, inputHighPassCoeff = 0.0f;
  float bandwidth = 0.0f, bandwidthCoeff = 0.0f;
  std::array<Allpass, 4> inputDiffusers;
  TankHalf left, right;
  float excursion = 16.0f;
  std::array<int, 14> taps{};
  double lfoPhase = 0.0, lfoIncrement = 0.0;

  float decayGain = 0.5f, dampingCoeff = 0.5f;
  juce::SmoothedValue<float> decay, tone, mix;
  bool primed = false;
};
} // namespace evil
