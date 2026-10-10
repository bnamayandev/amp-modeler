#pragma once

#include "Biquad.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace evil {
/** Post-cab three-band EQ: bass low shelf (100 Hz), mid peak (800 Hz), treble
   high shelf (3.2 kHz), each +/-12 dB. Gains glide towards their targets and
   the coefficients are recomputed every 32 samples while they move, so sweeping
   a knob doesn't zipper. */
class Equaliser {
public:
  void prepare(double newSampleRate) noexcept {
    sampleRate = newSampleRate;
    glidePerSubBlock =
        (float)(subBlock * 24.0 / (0.05 * sampleRate)); // full range in ~50 ms
    current = target = {0.0f, 0.0f, 0.0f};
    updateCoefficients();
    reset();
    primed = false;
  }

  void reset() noexcept {
    for (auto &f : filters)
      f.reset();
  }

  void process(float *data, int n, float bassDb, float midDb,
               float trebleDb) noexcept {
    target = {bassDb, midDb, trebleDb};
    if (!primed) // start at the first block's settings rather than gliding from
                 // flat
    {
      current = target;
      updateCoefficients();
      primed = true;
    }
    for (int start = 0; start < n; start += subBlock) {
      if (!isAt(target)) {
        for (size_t b = 0; b < current.size(); ++b)
          current[b] +=
              std::fmax(-glidePerSubBlock,
                        std::fmin(glidePerSubBlock, target[b] - current[b]));
        updateCoefficients();
      }
      if (isAt({0.0f, 0.0f, 0.0f})) {
        // Flat: leave the signal bit-exact. Zero state makes the near-identity
        // filters start cleanly when a knob next moves.
        reset();
        continue;
      }

      const int count = std::min(subBlock, n - start);
      for (auto &f : filters)
        f.process(data + start, count);
    }
  }

private:
  bool isAt(const std::array<float, 3> &gains) const noexcept {
    return juce::exactlyEqual(current[0], gains[0]) &&
           juce::exactlyEqual(current[1], gains[1]) &&
           juce::exactlyEqual(current[2], gains[2]);
  }

  void updateCoefficients() noexcept {
    filters[0].setCoeffs(
        BiquadCoeffs::lowShelf(sampleRate, 100.0, 0.707, current[0]));
    filters[1].setCoeffs(
        BiquadCoeffs::peak(sampleRate, 800.0, 0.8, current[1]));
    filters[2].setCoeffs(
        BiquadCoeffs::highShelf(sampleRate, 3200.0, 0.707, current[2]));
  }

  static constexpr int subBlock = 32;
  double sampleRate = 48000.0;
  float glidePerSubBlock = 1.0f;
  std::array<float, 3> current{}, target{};
  std::array<Biquad, 3> filters;
  bool primed = false;
};
} // namespace evil
