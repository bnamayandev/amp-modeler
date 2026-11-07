#pragma once

#include "Biquad.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace evil {
/** Post-cab nine-band graphic EQ: peaking bands an octave apart from 50 Hz to
   12.8 kHz (the seven bands of a Boss GE-7, 100 Hz - 6.4 kHz, plus an octave either
   side), each +/-15 dB as on a GE-7.

   Bands an octave apart overlap, so on a plain graphic EQ five faders at +6 dB give
   nearly +9 dB. Here the filters are set so that the response at each band's
   centre is what its fader says, by inverting how the bands interact (after
   Valimaki & Liski, "Accurate Cascade Graphic Equalizer", IEEE SPL 2017), so a dB
   on a fader is a dB.

   Gains glide towards their targets and a moving band's coefficients are recomputed
   every 32 samples, so sweeping a fader doesn't zipper. */
class Equaliser {
public:
  static constexpr int numBands = 9;
  using Gains = std::array<float, numBands>;
  static constexpr std::array<double, numBands> frequencies{
      50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12800.0};
  static constexpr double q = 1.41; // one octave wide at half gain
  static constexpr float maxGainDb = 15.0f;
  /** How far a filter can be pushed to make up for its neighbours. */
  static constexpr float maxFilterGainDb = 24.0f;

  void prepare(double newSampleRate) noexcept {
    sampleRate = newSampleRate;
    glidePerSubBlock = (float)(subBlock * 2.0 * maxGainDb /
                               (0.05 * sampleRate)); // full range in ~50 ms
    computeCorrection();
    current = target = faders = {};
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
    if (gainsDb != faders || !primed) {
      faders = gainsDb;
      for (size_t b = 0; b < numBands; ++b) {
        double filterDb = 0.0;
        for (size_t f = 0; f < numBands; ++f)
          filterDb += correction[b][f] * (double)faders[f];
        target[b] = (float)std::fmax(-maxFilterGainDb,
                                     std::fmin(maxFilterGainDb, filterDb));
      }
    }
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

  /** Fader settings that approximate the original three-band EQ (bass low shelf
     100 Hz, mid peak 800 Hz, treble high shelf 3.2 kHz), for presets and sessions
     saved before there were nine bands: its response at each band's centre, which
     is what a fader gives. */
  static Gains fromLegacy(float bassDb, float midDb, float trebleDb) {
    constexpr double rate = 48000.0;
    const std::array legacy{
        BiquadCoeffs::lowShelf(rate, 100.0, 0.707, bassDb),
        BiquadCoeffs::peak(rate, 800.0, 0.8, midDb),
        BiquadCoeffs::highShelf(rate, 3200.0, 0.707, trebleDb)};
    Gains result;
    for (size_t b = 0; b < numBands; ++b) {
      double db = 0.0;
      for (const auto &s : legacy)
        db += s.gainDbAt(rate, frequencies[b]);
      // To the parameters' 0.01 dB steps.
      result[b] = (float)(std::round(std::fmax(-(double)maxGainDb,
                                               std::fmin((double)maxGainDb, db)) * 100.0) /
                          100.0);
    }
    return result;
  }

private:
  static BiquadCoeffs coeffs(double rate, size_t band, double gainDb) {
    return BiquadCoeffs::peak(rate, frequencies[band], q, gainDb);
  }

  /** The inverse of how much each band's filter shows up at every band's centre,
     per dB, at a typical gain. Run in prepare: fine to take a moment. */
  void computeCorrection() noexcept {
    constexpr double designDb = 6.0;
    std::array<std::array<double, 2 * numBands>, numBands> m{};
    for (size_t f = 0; f < numBands; ++f) {
      const auto shape = coeffs(sampleRate, f, designDb);
      for (size_t b = 0; b < numBands; ++b)
        m[b][f] = shape.gainDbAt(sampleRate, frequencies[b]) / designDb;
      m[f][numBands + f] = 1.0;
    }
    // Gauss-Jordan. The matrix is strongly diagonal, so no pivoting is needed.
    for (size_t i = 0; i < numBands; ++i) {
      const double pivot = m[i][i];
      for (auto &value : m[i])
        value /= pivot;
      for (size_t r = 0; r < numBands; ++r)
        if (r != i) {
          const double factor = m[r][i];
          for (size_t c = 0; c < 2 * numBands; ++c)
            m[r][c] -= factor * m[i][c];
        }
    }
    for (size_t r = 0; r < numBands; ++r)
      for (size_t c = 0; c < numBands; ++c)
        correction[r][c] = m[r][numBands + c];
  }

  void updateBand(size_t b) noexcept {
    filters[b].setCoeffs(coeffs(sampleRate, b, current[b]));
  }

  static constexpr int subBlock = 32;
  double sampleRate = 48000.0;
  float glidePerSubBlock = 1.0f;
  Gains current{}, target{}, faders{};
  std::array<std::array<double, numBands>, numBands> correction{};
  std::array<Biquad, numBands> filters;
  bool primed = false;
};
} // namespace evil
