#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace evil::analog {
/** The drive pedals work in volts, using the component values of the circuits
   they model. Digital full scale is +12 dBu peak, a common level for reamping
   NAM captures, so a pedal hits a NAM amp about as hard as the real pedal would.
   Captures of a real Tube Screamer agree with this scale: they start to clip at
   the input level their diodes predict. */
inline constexpr double voltsAtFullScale = 4.37;

/** How far a 9 V pedal's op-amp or transistor can swing, fitted to the Tube
   Screamer captures' behaviour at full-scale input. */
inline constexpr double railVolts = 3.0;

/** Fraction of an audio-taper pot's resistance at knob position 0..1: 15% at
   noon, as on a typical "A" pot. */
inline double audioTaper(double knob) noexcept {
  constexpr double base = 32.1; // 1 / (sqrt(base) + 1) = 0.15
  return (std::pow(base, std::clamp(knob, 0.0, 1.0)) - 1.0) / (base - 1.0);
}

/** Fraction of a pot's resistance at knob position 0..1, for a pot measured or
   specified at noonFraction at noon: an exponential law, as tapered pots
   approximate. 0.5 would be linear. */
inline double potTaper(double knob, double noonFraction) noexcept {
  const double root = (1.0 - noonFraction) / noonFraction;
  const double base = root * root;
  if (std::abs(base - 1.0) < 1.0e-6)
    return std::clamp(knob, 0.0, 1.0);
  return (std::pow(base, std::clamp(knob, 0.0, 1.0)) - 1.0) / (base - 1.0);
}

/** The knob position that gives this fraction: the inverse of potTaper. */
inline double potPosition(double fraction, double noonFraction) noexcept {
  const double root = (1.0 - noonFraction) / noonFraction;
  const double base = root * root;
  if (std::abs(base - 1.0) < 1.0e-6)
    return std::clamp(fraction, 0.0, 1.0);
  return std::clamp(std::log(1.0 + std::max(fraction, 0.0) * (base - 1.0)) / std::log(base),
                    0.0, 1.0);
}

/** Output level knobs, in dB, at their real pots' positions. Each is its pedal's
   output with the pot fully up (maxDb, relative to about unity on a guitar) and
   its fraction at noon. Measured on real pedals for the overdrive: two Tube
   Screamers are 7 to 8 dB down at noon, nothing like a textbook audio pot. */
struct LevelKnob {
  double maxDb, noonFraction;
  static constexpr double minDb = -60.0;
  double dbAt(double knob) const noexcept {
    const double fraction = potTaper(knob, noonFraction);
    return fraction <= 0.0 ? minDb
                           : std::max(minDb, maxDb + 20.0 * std::log10(fraction));
  }
  double positionOf(double db) const noexcept {
    return db <= minDb ? 0.0
                       : potPosition(std::pow(10.0, (db - maxDb) / 20.0), noonFraction);
  }
};
inline constexpr LevelKnob overdriveLevel{3.9, 0.42}; // measured: see TestPedals
inline constexpr LevelKnob distortionVolume{3.5, 0.3};
inline constexpr LevelKnob fuzzVolume{6.0, 0.3};
inline constexpr LevelKnob compressorOutput{12.0, 0.2512}; // 0 dB at noon

/** MXR Micro Amp: a non-inverting op-amp stage, unity to 26 dB (20 times), with
   the pot in its feedback. */
inline double boostDbAt(double knob) noexcept {
  return 20.0 * std::log10(1.0 + 19.0 * audioTaper(knob));
}
inline double boostPositionOf(double db) noexcept {
  return potPosition((std::pow(10.0, db / 20.0) - 1.0) / 19.0, 0.15);
}

/** Corner frequency of an RC filter, in Hz. */
inline constexpr double cornerHz(double ohms, double farads) noexcept {
  return 1.0 / (2.0 * 3.14159265358979323846 * ohms * farads);
}

/** Back-to-back 1N4148/1N914 silicon diodes, the Shockley equation with the
   common SPICE values: Is = 2.52 nA, n = 1.752, at 25 C. */
struct SiliconDiodes {
  static constexpr double saturationAmps = 2.52e-9;
  static constexpr double nVt = 1.752 * 0.02585;

  /** Solves v + 2b sinh(v / nVt) = u for v by Newton's method. The left side only
     ever rises with v, so there is one answer. The previous sample's answer is
     usually a close guess, which takes a step or two.

     On a near-square edge u jumps from one side to the other, and Newton would
     crawl down the exponential by about nVt per step, run out of iterations and
     spike the output. So a big step restarts from just above the answer: |v| can't
     exceed |u|, nor nVt asinh(|u| / 2b), and the smaller of those is close to it.
     From above, Newton on this convex curve goes straight there. */
  static double solve(double b, double u, double guess) noexcept {
    double v = guess;
    bool restarted = false;
    for (int iteration = 0; iteration < 16; ++iteration) {
      const double e = std::exp(v / nVt), inverse = 1.0 / e;
      const double step =
          (v + b * (e - inverse) - u) / (1.0 + (b / nVt) * (e + inverse));
      if (!restarted && std::abs(step) > 0.02) {
        const double bound =
            std::min(std::abs(u), nVt * std::asinh(std::abs(u) / (2.0 * b)));
        v = std::copysign(bound, u);
        restarted = true;
        continue;
      }
      v -= step;
      if (std::abs(step) < 1.0e-9)
        break;
    }
    return v;
  }
};

/** Silicon diodes behind a resistance, memoryless: the RAT's diodes to ground
   behind 1k, or the Tube Screamer's and Big Muff's in parallel with their feedback
   resistor (the same equation, with that resistor as the series one and the
   source voltage being the current into it times its value).

   Antiderivative anti-aliased (Parker et al., DAFx 2016): the output is the mean
   of the curve between consecutive inputs. Clipping this hard is nearly a square
   wave, whose harmonics still reach past 4x oversampling. The curve's integral
   has a closed form: with u = v + 2b sinh(v / nVt),
   F(u) = v^2 / 2 + 2b (v sinh(v / nVt) - nVt cosh(v / nVt)), so dF/du = v.

   An op-amp in front that runs out of swing has to be part of the same curve: a
   clamp ahead of this would lose where between two samples the signal crossed
   zero, which is exactly what the averaging needs. Past the limit the curve is
   flat, so F just carries on in a straight line. */
class DiodeClipper {
public:
  void reset() noexcept {
    voltage = source = integral = 0.0;
    previousB = -1.0; // the first sample has no segment to average over
  }

  double process(double sourceVolts, double seriesOhms,
                 double limitVolts = 1.0e6) noexcept {
    constexpr double nVt = SiliconDiodes::nVt;
    const double b = seriesOhms * SiliconDiodes::saturationAmps;
    const double limited = std::clamp(sourceVolts, -limitVolts, limitVolts);
    const double v = SiliconDiodes::solve(b, limited, voltage);
    const double e = std::exp(v / nVt), inverse = 1.0 / e;
    const double f = 0.5 * v * v + b * (v * (e - inverse) - nVt * (e + inverse)) +
                     v * (sourceVolts - limited);
    const double du = sourceVolts - source;
    // The integral is only comparable between samples with the same resistance.
    const double out = std::abs(du) > 1.0e-5 && std::abs(b - previousB) < 1.0e-15
                           ? (f - integral) / du
                           : 0.5 * (v + voltage);
    voltage = v;
    source = sourceVolts;
    integral = f;
    previousB = b;
    return out;
  }

private:
  double voltage = 0.0, source = 0.0, integral = 0.0, previousB = -1.0;
};

/** A third-order filter from its analog transfer function (bilinear transform),
   for a circuit with more poles than a biquad. Double precision direct form:
   fine for the RAT's poles even at 192 kHz. */
class ThirdOrderFilter {
public:
  void reset() noexcept { z = {}; }
  /** (b0 + b1 s + b2 s^2) / (a0 + a1 s + a2 s^2 + a3 s^3) */
  void setAnalog(double sampleRate, double b0, double b1, double b2, double a0,
                 double a1, double a2, double a3) noexcept {
    const double k = 2.0 * sampleRate, k2 = k * k, k3 = k2 * k;
    const double d0 = a0 + a1 * k + a2 * k2 + a3 * k3;
    n = {(b0 + b1 * k + b2 * k2) / d0, (3 * b0 + b1 * k - b2 * k2) / d0,
         (3 * b0 - b1 * k - b2 * k2) / d0, (b0 - b1 * k + b2 * k2) / d0};
    d = {(3 * a0 + a1 * k - a2 * k2 - 3 * a3 * k3) / d0,
         (3 * a0 - a1 * k - a2 * k2 + 3 * a3 * k3) / d0,
         (a0 - a1 * k + a2 * k2 - a3 * k3) / d0};
  }
  double process(double x) noexcept {
    const double y = n[0] * x + z[0];
    z[0] = n[1] * x - d[0] * y + z[1];
    z[1] = n[2] * x - d[1] * y + z[2];
    z[2] = n[3] * x - d[2] * y;
    return y;
  }

private:
  std::array<double, 4> n{};
  std::array<double, 3> d{}, z{};
};

/** Polyphase half-band filter: two chains of first-order all-passes (de Soras,
   HIIR). Barely any delay and no latency to report: a nonlinear phase shift near
   the top of the band, as an analog circuit has too. N must be even. */
template <size_t N> class Halfband {
public:
  explicit constexpr Halfband(const std::array<double, N> &coefficients)
      : c(coefficients) {}

  void reset() noexcept { x = y = {}; }

  void up(double in, double &first, double &second) noexcept {
    first = second = in;
    run(first, second);
  }

  double down(double older, double newer) noexcept {
    run(newer, older);
    return 0.5 * (newer + older);
  }

private:
  void run(double &even, double &odd) noexcept {
    for (size_t i = 0; i < N; i += 2) {
      const double nextEven = (even - y[i]) * c[i] + x[i];
      const double nextOdd = (odd - y[i + 1]) * c[i + 1] + x[i + 1];
      x[i] = even;
      x[i + 1] = odd;
      y[i] = even = nextEven;
      y[i + 1] = odd = nextOdd;
    }
  }

  std::array<double, N> c, x{}, y{};
};

/** Runs a clipper at 4x (2x from 88.2 kHz, not at all from 176.4 kHz). Analog
   clipping makes harmonics far above the audio band; at the plain sample rate
   they would fold back down as inharmonic fizz.

   Stopbands: over 115 dB from 27.4 kHz in the first stage (at 48 kHz) and from
   72 kHz in the second, flat to well under 0.001 dB up to 20 kHz. */
class Oversampler {
public:
  static constexpr int maxFactor = 4;

  void prepare(double sampleRate) noexcept {
    factor = sampleRate <= 60000.0 ? 4 : sampleRate <= 120000.0 ? 2 : 1;
    reset();
  }
  void reset() noexcept {
    upOuter.reset();
    upInner.reset();
    downOuter.reset();
    downInner.reset();
  }
  int getFactor() const noexcept { return factor; }

  /** Writes getFactor() samples. */
  void up(double in, double *out) noexcept {
    if (factor == 4) {
      double a, b;
      upOuter.up(in, a, b);
      upInner.up(a, out[0], out[1]);
      upInner.up(b, out[2], out[3]);
    } else if (factor == 2) {
      upInner.up(in, out[0], out[1]);
    } else {
      out[0] = in;
    }
  }

  /** Reads getFactor() samples. */
  double down(const double *in) noexcept {
    if (factor == 4) {
      const double a = downInner.down(in[0], in[1]);
      const double b = downInner.down(in[2], in[3]);
      return downOuter.down(a, b);
    }
    return factor == 2 ? downInner.down(in[0], in[1]) : in[0];
  }

private:
  static constexpr std::array<double, 8> outerCoefficients{
      0.029224847909, 0.111006330185, 0.230345696821, 0.369345995103,
      0.513232032097, 0.653727857184, 0.789787692015, 0.927163835842};
  static constexpr std::array<double, 4> innerCoefficients{
      0.042454709865, 0.170739850497, 0.393319893190, 0.745713588720};

  int factor = 4;
  Halfband<8> upOuter{outerCoefficients}, downOuter{outerCoefficients};
  Halfband<4> upInner{innerCoefficients}, downInner{innerCoefficients};
};
} // namespace evil::analog
