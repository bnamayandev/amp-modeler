#pragma once

#include <cmath>
#include <complex>
#include <numbers>

namespace evil {
/** Second-order section coefficients (normalised so a0 == 1), from the RBJ
   Audio EQ Cookbook. Computed with plain arithmetic, so they are safe to
   recalculate on the audio thread (juce::dsp::IIR::Coefficients allocates). */
struct BiquadCoeffs {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

  static BiquadCoeffs lowShelf(double sampleRate, double freq, double q,
                               double gainDb) {
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto [cw, alpha] = trig(sampleRate, freq, q);
    const auto sqA = 2.0 * std::sqrt(A) * alpha;
    return normalise(
        A * ((A + 1) - (A - 1) * cw + sqA), 2 * A * ((A - 1) - (A + 1) * cw),
        A * ((A + 1) - (A - 1) * cw - sqA), (A + 1) + (A - 1) * cw + sqA,
        -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sqA);
  }

  static BiquadCoeffs highShelf(double sampleRate, double freq, double q,
                                double gainDb) {
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto [cw, alpha] = trig(sampleRate, freq, q);
    const auto sqA = 2.0 * std::sqrt(A) * alpha;
    return normalise(
        A * ((A + 1) + (A - 1) * cw + sqA), -2 * A * ((A - 1) + (A + 1) * cw),
        A * ((A + 1) + (A - 1) * cw - sqA), (A + 1) - (A - 1) * cw + sqA,
        2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sqA);
  }

  static BiquadCoeffs peak(double sampleRate, double freq, double q,
                           double gainDb) {
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto [cw, alpha] = trig(sampleRate, freq, q);
    return normalise(1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A,
                     -2 * cw, 1 - alpha / A);
  }

  static BiquadCoeffs lowPass(double sampleRate, double freq, double q) {
    const auto [cw, alpha] = trig(sampleRate, freq, q);
    return normalise((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw,
                     1 - alpha);
  }

  static BiquadCoeffs highPass(double sampleRate, double freq, double q) {
    const auto [cw, alpha] = trig(sampleRate, freq, q);
    return normalise((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw,
                     1 - alpha);
  }

  /** First-order sections, expressed as a biquad with b2 = a2 = 0 (bilinear
   * transform). */
  static BiquadCoeffs onePoleLowPass(double sampleRate, double freq) {
    const auto k =
        std::tan(std::numbers::pi * clampFreq(sampleRate, freq) / sampleRate);
    return {k / (1 + k), k / (1 + k), 0.0, (k - 1) / (1 + k), 0.0};
  }

  static BiquadCoeffs onePoleHighPass(double sampleRate, double freq) {
    const auto k =
        std::tan(std::numbers::pi * clampFreq(sampleRate, freq) / sampleRate);
    return {1 / (1 + k), -1 / (1 + k), 0.0, (k - 1) / (1 + k), 0.0};
  }

  /** Magnitude response at `freq`, in dB. */
  double gainDbAt(double sampleRate, double freq) const {
    const auto z = std::polar(1.0, -2.0 * std::numbers::pi * freq / sampleRate);
    return 20.0 * std::log10(std::abs((b0 + (b1 + b2 * z) * z) /
                                      (1.0 + (a1 + a2 * z) * z)));
  }

private:
  static double clampFreq(double sampleRate, double freq) {
    return std::fmin(std::fmax(freq, 1.0), 0.49 * sampleRate);
  }

  struct Trig {
    double cosw, alpha;
  };

  static Trig trig(double sampleRate, double freq, double q) {
    const auto w0 =
        2.0 * std::numbers::pi * clampFreq(sampleRate, freq) / sampleRate;
    return {std::cos(w0), std::sin(w0) / (2.0 * q)};
  }

  static BiquadCoeffs normalise(double b0, double b1, double b2, double a0,
                                double a1, double a2) {
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
  }
};

/** Transposed direct form II biquad with double-precision state (stable for low
   shelves at high sample rates, where float state loses precision). */
class Biquad {
public:
  void setCoeffs(const BiquadCoeffs &c) noexcept { coeffs = c; }
  void reset() noexcept { z1 = z2 = 0.0; }

  float processSample(float in) noexcept {
    const double x = in;
    const double y = coeffs.b0 * x + z1;
    z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
    z2 = coeffs.b2 * x - coeffs.a2 * y;
    return static_cast<float>(y);
  }

  void process(float *data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
      data[i] = processSample(data[i]);
  }

private:
  BiquadCoeffs coeffs;
  double z1 = 0.0, z2 = 0.0;
};
} // namespace evil
