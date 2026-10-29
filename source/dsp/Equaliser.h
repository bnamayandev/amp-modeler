#pragma once

#include "Biquad.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace evil {
/** Post-cab nine-band graphic EQ: peaking bands an octave apart from 50 Hz to
   12.8 kHz (the classic seven-band guitar series, 100 Hz - 6.4 kHz, plus an
   octave either side), each +/-10 dB. Gains glide towards their targets and a
   moving band's coefficients are recomputed every 32 samples, so sweeping a
   fader doesn't zipper. */
class Equaliser {
public:
  static constexpr int numBands = 9;
  using Gains = std::array<float, numBands>;
  static constexpr std::array<double, numBands> frequencies{
      50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12800.0};
  static constexpr double q = 1.41; // one octave wide at half gain
  static constexpr float maxGainDb = 10.0f;

  void prepare(double newSampleRate) noexcept {
    sampleRate = newSampleRate;
    glidePerSubBlock = (float)(subBlock * 2.0 * maxGainDb /
                               (0.05 * sampleRate)); // full range in ~50 ms
    current = target = {};
    for (size_t b = 0; b < numBands; ++b)
      updateBand(b);
    reset();
    primed = false;
  }

  void reset() noexcept {
    for (auto &f : filters)
      f.reset();
  }

  void process(float *data, int n, const Gains &gainsDb) noexcept {
    target = gainsDb;
    if (!primed) // start at the first block's settings rather than gliding from
                 // flat
    {
      current = target;
      for (size_t b = 0; b < numBands; ++b)
        updateBand(b);
      primed = true;
    }
    for (int start = 0; start < n; start += subBlock) {
      const int count = std::min(subBlock, n - start);
      for (size_t b = 0; b < numBands; ++b) {
        if (!juce::exactlyEqual(current[b], target[b])) {
          const float step = target[b] - current[b];
          current[b] = std::fabs(step) <= glidePerSubBlock
                           ? target[b]
                           : current[b] + std::copysign(glidePerSubBlock, step);
          updateBand(b);
        }
        if (juce::exactlyEqual(current[b], 0.0f)) {
          // Flat: skip the band, leaving the signal bit-exact. Zero state makes
          // the near-identity filter start cleanly when its fader next moves.
          filters[b].reset();
          continue;
        }
        filters[b].process(data + start, count);
      }
    }
  }

  /** Band gains that approximate the original three-band EQ (bass low shelf
     100 Hz, mid peak 800 Hz, treble high shelf 3.2 kHz), for presets and
     sessions saved before there were nine bands. Matches its response at each
     band's centre, allowing for the overlap between neighbouring bands. */
  static Gains fromLegacy(float bassDb, float midDb, float trebleDb) {
    constexpr double rate = 48000.0;
    const auto responseAt = [](const auto &sections, double freq) {
      double db = 0.0;
      for (const auto &s : sections)
        db += s.gainDbAt(rate, freq);
      return db;
    };
    const std::array legacy{
        BiquadCoeffs::lowShelf(rate, 100.0, 0.707, bassDb),
        BiquadCoeffs::peak(rate, 800.0, 0.8, midDb),
        BiquadCoeffs::highShelf(rate, 3200.0, 0.707, trebleDb)};

    std::array<double, numBands> wanted{}, gains{};
    for (size_t b = 0; b < numBands; ++b)
      wanted[b] = gains[b] = responseAt(legacy, frequencies[b]);
    for (int round = 0; round < 12; ++round) {
      std::array<BiquadCoeffs, numBands> bands;
      for (size_t b = 0; b < numBands; ++b)
        bands[b] = coeffs(rate, b, gains[b]);
      for (size_t b = 0; b < numBands; ++b)
        gains[b] = std::fmax(-maxGainDb,
                             std::fmin(maxGainDb,
                                       gains[b] + wanted[b] -
                                           responseAt(bands, frequencies[b])));
    }

    Gains result;
    for (size_t b = 0; b < numBands; ++b) // to the parameters' 0.01 dB steps
      result[b] = (float)(std::round(gains[b] * 100.0) / 100.0);
    return result;
  }

private:
  static BiquadCoeffs coeffs(double rate, size_t band, double gainDb) {
    return BiquadCoeffs::peak(rate, frequencies[band], q, gainDb);
  }

  void updateBand(size_t b) noexcept {
    filters[b].setCoeffs(coeffs(sampleRate, b, current[b]));
  }

  static constexpr int subBlock = 32;
  double sampleRate = 48000.0;
  float glidePerSubBlock = 1.0f;
  Gains current{}, target{};
  std::array<Biquad, numBands> filters;
  bool primed = false;
};
} // namespace evil
