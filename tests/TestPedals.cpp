#include "TestHelpers.h"
#include "dsp/Metronome.h"
#include "dsp/SignalChain.h"
#include "state/Parameters.h"
#include "state/PresetManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_dsp/juce_dsp.h>

using namespace evil;
using namespace evil::test;

namespace {
constexpr double rate = 48000.0;
constexpr int block = 128;

SignalChain::Settings neutral() {
  SignalChain::Settings s;
  s.gateOn = false;
  s.cabOn = false;
  return s;
}

void run(SignalChain &chain, std::vector<float> &x,
         const SignalChain::Settings &s) {
  for (size_t start = 0; start < x.size(); start += (size_t)block)
    chain.process(x.data() + start,
                  (int)std::min<size_t>((size_t)block, x.size() - start), s);
}

/** Magnitude of x at one frequency (Goertzel), over [from, from + count). */
double magnitudeAt(const std::vector<float> &x, size_t from, size_t count,
                   double freq) {
  const double w = 2.0 * juce::MathConstants<double>::pi * freq / rate;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = from; i < from + count; ++i) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double)count;
}

double dominantFrequency(const std::vector<float> &x, size_t from,
                         size_t count, double lo, double hi) {
  double best = lo, bestMag = -1.0;
  for (double f = lo; f <= hi; f += 0.5)
    if (const double m = magnitudeAt(x, from, count, f); m > bestMag) {
      bestMag = m;
      best = f;
    }
  return best;
}

double rmsDb(const std::vector<float> &x, size_t from, size_t to) {
  return juce::Decibels::gainToDecibels(rms(x.data() + from, (int)(to - from)),
                                        -200.0);
}

float maxStep(const std::vector<float> &x, size_t from, size_t to) {
  float m = 0.0f;
  for (size_t i = from + 1; i < to; ++i)
    m = std::max(m, std::abs(x[i] - x[i - 1]));
  return m;
}

/** A note with its first few harmonics, rolling off like a plucked string. */
std::vector<float> harmonicNote(double freq, double sampleRate, int count,
                                float level) {
  std::vector<float> x((size_t)count, 0.0f);
  for (int h = 1; h <= 5; ++h) {
    const auto partial =
        sine(freq * h, sampleRate, count, level / (float)h);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] += partial[i];
  }
  return x;
}

/** How far the level has got: the first sample at or after `from` where a 1 ms
    RMS reaches half way from the level before `from` to the level after it. */
size_t riseTime(const std::vector<float> &x, size_t from, double sampleRate) {
  const int window = (int)(0.001 * sampleRate);
  const auto level = [&](size_t end) {
    return rms(x.data() + end - (size_t)window, window);
  };
  double before = 0.0, after = 0.0;
  for (size_t i = from - (size_t)(0.03 * sampleRate); i < from; ++i)
    before = std::max(before, (double)level(i));
  for (size_t i = from; i < from + (size_t)(0.06 * sampleRate); ++i)
    after = std::max(after, (double)level(i));
  const double half = 0.5 * (before + after);
  size_t i = from;
  while (i < x.size() - 1 && level(i) < half)
    ++i;
  return i;
}

struct AttackDelay {
  double meanMs = 0.0, worstMs = 0.0;
};

/** The delay a sudden jump in level picks up on its way through the transposer,
    sampled at irregular moments so they fall all over the read tap's travel. */
AttackDelay attackDelay(double sampleRate, int semitones, double freq) {
  const int count = (int)(sampleRate * 12.0);
  auto x = harmonicNote(freq, sampleRate, count, 0.3f);

  juce::Random random(5);
  std::vector<size_t> attacks;
  for (auto at = (size_t)sampleRate; at < x.size() - (size_t)sampleRate;
       at += (size_t)((0.18 + 0.24 * random.nextDouble()) * sampleRate))
    attacks.push_back(at);
  const auto loud = (size_t)(0.06 * sampleRate);
  for (size_t i = 0; i < x.size(); ++i) {
    const auto next = std::upper_bound(attacks.begin(), attacks.end(), i);
    const bool isLoud = next != attacks.begin() && i - *(next - 1) < loud;
    x[i] *= isLoud ? 1.0f : 0.05f;
  }

  SignalChain chain;
  chain.prepare(sampleRate, block);
  auto s = neutral();
  s.transposeSemitones = semitones;
  auto y = x;
  run(chain, y, s);

  AttackDelay result;
  for (const size_t at : attacks) {
    const double ms = 1000.0 *
                      ((double)riseTime(y, at, sampleRate) -
                       (double)riseTime(x, at, sampleRate)) /
                      sampleRate;
    result.meanMs += ms;
    result.worstMs = std::max(result.worstMs, ms);
  }
  result.meanMs /= (double)attacks.size();
  return result;
}

/** Level (dB) of everything further than widthHz from any of the given
    frequencies, relative to what's near them: how clean a shift came out. */
double residualDb(const std::vector<float> &x, size_t from,
                  const std::vector<double> &freqs, double widthHz) {
  constexpr int order = 16;
  const size_t size = (size_t)1 << order;
  std::vector<float> buffer(size * 2, 0.0f);
  std::copy_n(x.begin() + (std::ptrdiff_t)from, size, buffer.begin());
  juce::dsp::WindowingFunction<float> window(
      size, juce::dsp::WindowingFunction<float>::hann, false);
  window.multiplyWithWindowingTable(buffer.data(), size);
  juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(buffer.data());

  double near = 1.0e-30, far = 1.0e-30;
  const double binHz = rate / (double)size;
  for (size_t bin = 1; bin < size / 2; ++bin) {
    const double f = (double)bin * binHz;
    const bool isNear = std::any_of(freqs.begin(), freqs.end(), [&](double t) {
      return std::abs(f - t) <= widthHz;
    });
    (isNear ? near : far) += (double)buffer[bin] * buffer[bin];
  }
  return 10.0 * std::log10(far / near);
}
} // namespace

TEST_CASE("The transposer adds little delay to single notes",
          "[pedals][transpose]") {
  // The tap only has to travel about one period of what's being played: ~12 ms
  // on a low E, a few ms up the neck. (Going down, an attack takes 1/ratio
  // times the tap's delay to reach it, so an octave down doubles it.) The old
  // fixed 32 ms window averaged ~15 ms on both, and 30-40 ms at worst.
  const double sampleRate = GENERATE(44100.0, 48000.0, 96000.0);
  const std::vector<int> intervals = {-12, -5, -2, -1, 1, 2, 5, 12};

  for (const auto &[note, freq, meanLimit, worstLimit] :
       {std::tuple{"low E", 82.41, 11.0, 28.0},
        std::tuple{"A4", 440.0, 7.0, 14.0}}) {
    double mean = 0.0, worst = 0.0;
    for (const int semitones : intervals) {
      const auto delay = attackDelay(sampleRate, semitones, freq);
      UNSCOPED_INFO(note << " " << semitones << " st: mean " << delay.meanMs
                         << " ms, worst " << delay.worstMs << " ms");
      mean += delay.meanMs / (double)intervals.size();
      worst = std::max(worst, delay.worstMs);
    }
    INFO(sampleRate << " Hz, " << note << ": mean " << mean << " ms, worst "
                    << worst << " ms");
    CHECK(mean < meanLimit);
    CHECK(worst < worstLimit);
  }
}

TEST_CASE("A low-E power chord stays clean through the transposer",
          "[pedals][transpose]") {
  // Its notes only line up every 24 ms; a shorter jump leaves the chord warbling
  // however well it's chosen. Guards the longest jump the search may use.
  const int semitones = GENERATE(-2, -1, 1, 2);
  const std::vector<double> notes = {82.41, 123.47, 164.81};
  const int count = (int)rate * 2;
  std::vector<float> x((size_t)count, 0.0f);
  for (const double f : notes) {
    const auto note = harmonicNote(f, rate, count, 0.1f);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] += note[i];
  }

  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.transposeSemitones = semitones;
  run(chain, x, s);

  const double ratio = std::exp2(semitones / 12.0);
  std::vector<double> shifted;
  for (const double f : notes)
    for (int h = 1; h <= 5; ++h)
      shifted.push_back(f * h * ratio);
  const double residual = residualDb(x, (size_t)rate / 2, shifted, 3.0);
  INFO(semitones << " st: residual " << residual << " dB");
  CHECK(residual < -15.0);
}

TEST_CASE("High notes stay in tune through many short splices",
          "[pedals][transpose]") {
  // Up the neck the tap splices every few ms; a jump rounded to the sample
  // would add up to a pitch error and smear the upper harmonics.
  const int semitones = GENERATE(-7, -5, 5, 7);
  auto x = harmonicNote(659.25, rate, (int)rate * 2, 0.15f);

  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.transposeSemitones = semitones;
  run(chain, x, s);

  const double ratio = std::exp2(semitones / 12.0);
  std::vector<double> shifted;
  for (int h = 1; h <= 5; ++h)
    shifted.push_back(659.25 * h * ratio);
  const double residual = residualDb(x, (size_t)rate / 2, shifted, 15.0);
  INFO(semitones << " st: residual " << residual << " dB");
  CHECK(residual < -30.0);
}

TEST_CASE("The transposer shifts pitch by the requested interval",
          "[pedals][transpose]") {
  const int semitones = GENERATE(-12, -5, -1, 2, 7, 12);
  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.transposeSemitones = semitones;

  auto x = sine(220.0, rate, (int)rate * 2, 0.4f);
  run(chain, x, s);

  const double expected = 220.0 * std::exp2(semitones / 12.0);
  const double found =
      dominantFrequency(x, (size_t)rate / 2, (size_t)rate, 80.0, 500.0 * 2);
  INFO(semitones << " st: expected " << expected << " Hz, found " << found);
  CHECK(std::abs(found - expected) < 1.5);
  // The original pitch is gone, not just outweighed.
  CHECK(magnitudeAt(x, (size_t)rate / 2, (size_t)rate, 220.0) <
        0.1 * magnitudeAt(x, (size_t)rate / 2, (size_t)rate, expected));
}

TEST_CASE("The transposer shifts chords, not just single notes",
          "[pedals][transpose]") {
  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.transposeSemitones = 5;
  auto x = sine(196.0, rate, (int)rate * 2, 0.25f); // G3 + D4
  const auto d = sine(293.66, rate, (int)rate * 2, 0.25f);
  for (size_t i = 0; i < x.size(); ++i)
    x[i] += d[i];
  run(chain, x, s);

  const auto at = [&](double f) {
    return magnitudeAt(x, (size_t)rate / 2, (size_t)rate, f);
  };
  const double up = std::exp2(5.0 / 12.0);
  for (double f : {196.0, 293.66}) {
    INFO(f << " Hz -> " << f * up << " Hz: " << at(f * up) << " vs " << at(f));
    CHECK(at(f * up) > 0.06); // both notes come through near full level
    CHECK(at(f) < 0.2 * at(f * up));
  }
}

TEST_CASE("Pedals switched on and off again return to a bit-exact pass-through",
          "[pedals]") {
  SignalChain chain;
  chain.prepare(rate, block);
  juce::Random random(11);
  std::vector<float> x((size_t)rate);
  for (auto &v : x)
    v = random.nextFloat() * 0.6f - 0.3f;

  auto s = neutral();
  s.transposeSemitones = 3;
  s.wah.on = s.compressor.on = s.fuzz.on = s.overdrive.on = true;
  s.distortion.on = s.boost.on = true;
  s.phaser.on = s.flanger.on = s.chorus.on = s.tremolo.on = true;
  s.delay.on = s.reverb.on = true;
  auto y = x;
  run(chain, y, s);
  for (float v : y)
    REQUIRE(std::isfinite(v));
  CHECK(y != x);

  // After the 10 ms footswitch fades, everything off is exact again.
  auto off = neutral();
  std::vector<float> fade((size_t)rate / 20, 0.0f);
  run(chain, fade, off);
  y = x;
  run(chain, y, off);
  CHECK(y == x);
}

TEST_CASE("Footswitching modulation and ambience pedals does not click",
          "[pedals]") {
  SignalChain chain;
  chain.prepare(rate, block);
  auto x = sine(196.0, rate, (int)rate * 2, 0.3f);
  const float signalStep = maxStep(x, 0, x.size());

  auto s = neutral();
  for (size_t start = 0, b = 0; start < x.size(); start += block, ++b) {
    // Toggle a different pedal every ~107 ms.
    if (b % 40 == 20) {
      switch ((b / 40) % 8) {
      case 0: s.compressor.on = !s.compressor.on; break;
      case 1: s.chorus.on = !s.chorus.on; break;
      case 2: s.delay.on = !s.delay.on; break;
      case 3: s.phaser.on = !s.phaser.on; break;
      case 4: s.flanger.on = !s.flanger.on; break;
      case 5: s.tremolo.on = !s.tremolo.on; break;
      case 6: s.boost.on = !s.boost.on; break;
      default: s.reverb.on = !s.reverb.on; break;
      }
    }
    chain.process(x.data() + start, block, s);
  }
  // A click is a jump far above the signal's own slope.
  CHECK(maxStep(x, 0, x.size()) < 4.0f * signalStep);
}

TEST_CASE("The delay repeats at the set time and feedback decays",
          "[pedals][delay]") {
  DelayPedal delay;
  delay.prepare(rate);
  DelayPedal::Settings s;
  s.on = true;
  s.timeMs = 250.0f;
  s.feedback = 0.5f;
  s.mix = 1.0f;

  std::vector<float> x((size_t)rate, 0.0f);
  x[0] = 0.5f;
  delay.process(x.data(), (int)x.size(), s);

  const auto peakNear = [&](size_t centre) {
    size_t at = centre - 50;
    for (size_t i = centre - 50; i < centre + 50; ++i)
      if (std::abs(x[i]) > std::abs(x[at]))
        at = i;
    return at;
  };
  CHECK(std::abs((int)peakNear(12000) - 12000) <= 1);
  // The bucket-brigade's darkening delays each repeat by a few samples more.
  CHECK(std::abs((int)peakNear(24000) - 24000) <= 8);
  // The loop filters smear later repeats, so compare energy, not peaks.
  const auto energy = [&](size_t centre) {
    double e = 0.0;
    for (size_t i = centre - 100; i < centre + 100; ++i)
      e += (double)x[i] * x[i];
    return e;
  };
  const double ratio = energy(24000) / energy(12000);
  INFO("second / first echo energy " << ratio);
  CHECK(ratio > 0.02);
  CHECK(ratio < 0.25); // feedback 0.5 is at most a quarter of the energy
  // Nothing between the dry impulse and the first echo.
  for (size_t i = 10; i < 11900; ++i)
    REQUIRE(std::abs(x[i]) < 1.0e-4f);
}

TEST_CASE("Maximum delay feedback and reverb decay stay bounded",
          "[pedals]") {
  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.delay = {true, 120.0f, 0.95f, 1.0f};
  s.reverb = {true, 1.0f, 1.0f, 1.0f};
  juce::Random random(5);
  std::vector<float> x((size_t)rate * 20);
  for (auto &v : x)
    v = random.nextFloat() * 1.6f - 0.8f;
  run(chain, x, s);
  float peak = 0.0f;
  for (float v : x) {
    REQUIRE(std::isfinite(v));
    peak = std::max(peak, std::abs(v));
  }
  INFO("peak " << peak);
  CHECK(peak < 8.0f);
}

TEST_CASE("The reverb tail decays, and lasts longer with more decay",
          "[pedals][reverb]") {
  const auto tailDb = [](float decay) {
    ReverbPedal reverb;
    reverb.prepare(rate);
    // The impulse comes after the 10 ms input fade-in a fresh reverb applies.
    std::vector<float> x((size_t)rate * 3, 0.0f);
    x[960] = 1.0f;
    reverb.process(x.data(), (int)x.size(), {true, decay, 0.6f, 1.0f});
    for (float v : x)
      REQUIRE(std::isfinite(v));
    return std::pair{rmsDb(x, 2400, 24000), rmsDb(x, 48000, 72000)};
  };
  const auto [shortEarly, shortLate] = tailDb(0.2f);
  const auto [longEarly, longLate] = tailDb(0.9f);
  INFO("short " << shortEarly << " -> " << shortLate << " dB, long "
                << longEarly << " -> " << longLate << " dB");
  CHECK(shortLate < shortEarly - 20.0);
  CHECK(longLate < longEarly);
  CHECK(longLate > shortLate + 15.0);
  CHECK(shortEarly > -60.0); // it is audible at all
  CHECK(longEarly > -60.0);
}

TEST_CASE("The compressor narrows the gap between loud and quiet playing",
          "[pedals][compressor]") {
  CompressorPedal comp;
  comp.prepare(rate);
  auto x = sine(196.0, rate, (int)rate * 2, 0.5f);
  for (size_t i = (size_t)rate; i < x.size(); ++i)
    x[i] *= 0.1f; // 20 dB quieter for the second second
  const double inGap = rmsDb(x, 24000, 48000) - rmsDb(x, 72000, 96000);
  comp.process(x.data(), (int)x.size(), {true, 0.8f, 1.0f, 0.0f});
  const double outGap = rmsDb(x, 24000, 48000) - rmsDb(x, 72000, 96000);
  INFO("in gap " << inGap << " dB, out gap " << outGap << " dB");
  CHECK(inGap == Catch::Approx(20.0).margin(0.1));
  CHECK(outGap < 10.0);
  CHECK(comp.getGainReductionDb() > 0.0f);
}

TEST_CASE("Drive pedals stay bounded and near unity level at their defaults",
          "[pedals]") {
  const auto measure = [](auto &pedal, auto settings, float amplitude) {
    pedal.prepare(rate);
    auto x = sine(196.0, rate, (int)rate, amplitude);
    const double in = rmsDb(x, 4800, x.size());
    pedal.process(x.data(), (int)x.size(), settings);
    float peak = 0.0f;
    for (float v : x) {
      REQUIRE(std::isfinite(v));
      peak = std::max(peak, std::abs(v));
    }
    return std::pair{rmsDb(x, 4800, x.size()) - in, peak};
  };
  FuzzPedal fuzz;
  OverdrivePedal od;
  DistortionPedal dist;
  FuzzPedal::Settings fuzzOn;
  fuzzOn.on = true;
  OverdrivePedal::Settings odOn;
  odOn.on = true;
  DistortionPedal::Settings distOn;
  distOn.on = true;

  for (float amplitude : {0.05f, 0.2f, 1.0f}) {
    const auto [fuzzGain, fuzzPeak] = measure(fuzz, fuzzOn, amplitude);
    const auto [odGain, odPeak] = measure(od, odOn, amplitude);
    const auto [distGain, distPeak] = measure(dist, distOn, amplitude);
    INFO("input " << amplitude << ": fuzz " << fuzzGain << " dB (peak "
                  << fuzzPeak << "), overdrive " << odGain << " dB (peak "
                  << odPeak << "), distortion " << distGain << " dB (peak "
                  << distPeak << ")");
    CHECK(fuzzPeak < 1.0f);
    CHECK(odPeak < 1.2f);
    CHECK(distPeak < 1.0f);
    if (amplitude == 0.2f) { // typical DI level
      CHECK(std::abs(fuzzGain) < 6.0);
      CHECK(std::abs(odGain) < 6.0);
      CHECK(std::abs(distGain) < 6.0);
    }
  }
}

namespace {
/** Gain at one frequency once settled, in dB, at a given input level. */
template <typename Pedal>
double gainDb(const typename Pedal::Settings &settings, double freq,
              float amplitude, double sampleRate = rate) {
  Pedal pedal;
  pedal.prepare(sampleRate);
  auto x = sine(freq, sampleRate, (int)sampleRate, amplitude);
  pedal.process(x.data(), (int)x.size(), settings);
  const double w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
  double re = 0.0, im = 0.0;
  const size_t from = x.size() / 2;
  // Whole cycles, so the tone's own harmonics don't count towards it.
  const auto count = (size_t)(std::floor((double)(x.size() - from) * freq / sampleRate) *
                              sampleRate / freq);
  for (size_t i = from; i < from + count; ++i) {
    re += x[i] * std::cos(w * (double)i);
    im += x[i] * std::sin(w * (double)i);
  }
  return juce::Decibels::gainToDecibels(
      2.0 * std::sqrt(re * re + im * im) / (double)count / amplitude, -200.0);
}

/** Inharmonic energy below 16 kHz relative to everything, in dB: aliasing, as
   analog clipping has none. Whole cycles fit the FFT exactly, so nothing leaks. */
template <typename Pedal>
double inharmonicDb(const typename Pedal::Settings &settings, int cycles,
                    float amplitude) {
  constexpr int order = 16, size = 1 << order;
  constexpr int settle = (int)rate / 2;
  const double freq = cycles * rate / size;
  Pedal pedal;
  pedal.prepare(rate);
  auto x = sine(freq, rate, settle + size, amplitude);
  pedal.process(x.data(), (int)x.size(), settings);

  std::vector<float> spectrum(2 * size, 0.0f);
  std::copy(x.begin() + settle, x.end(), spectrum.begin());
  juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(spectrum.data());
  double inharmonic = 0.0, total = 0.0;
  for (int bin = 1; bin < size / 2; ++bin) {
    const double power = (double)spectrum[(size_t)bin] * spectrum[(size_t)bin];
    total += power;
    if (bin % cycles != 0 && bin * rate / size < 16000.0)
      inharmonic += power;
  }
  return 10.0 * std::log10(inharmonic / total + 1.0e-30);
}

/** Total harmonic distortion of a tone through the pedal, as a fraction. */
template <typename Pedal>
double thd(const typename Pedal::Settings &settings, double freq,
           float amplitude) {
  double harmonics = 0.0;
  Pedal pedal;
  pedal.prepare(rate);
  auto x = sine(freq, rate, (int)rate, amplitude);
  pedal.process(x.data(), (int)x.size(), settings);
  const size_t from = x.size() / 2;
  const auto count = (size_t)(std::floor((double)(x.size() - from) * freq / rate) * rate / freq);
  for (int h = 2; h * freq < 20000.0; ++h)
    harmonics += std::pow(magnitudeAt(x, from, count, h * freq), 2.0);
  return std::sqrt(harmonics) / magnitudeAt(x, from, count, freq);
}
} // namespace

// Reference numbers below come from real pedals: NAM captures of an Ibanez Tube
// Screamer Mini (pelennor2170/NAM_models, "Tudor N", held-out ESR under 0.001),
// measured through this plugin's own model runner. The Mini is a close relative of
// the classic circuit, and the fit below shows how close. Level is matched at a
// single knob setting: the real knob's numbers don't map to dB.
TEST_CASE("The overdrive's response matches a real Tube Screamer's",
          "[pedals][overdrive]") {
  struct Point {
    double hz, db;
  };
  const auto check = [](const OverdrivePedal::Settings &settings,
                        std::initializer_list<Point> real) {
    for (const auto &p : real) {
      const double db = gainDb<OverdrivePedal>(settings, p.hz, 0.003f);
      INFO(p.hz << " Hz: real " << p.db << " dB, plugin " << db << " dB");
      CHECK(std::abs(db - p.db) < 1.6);
    }
  };
  // Drive 0, Tone 10, Level 10: flat 20 dB of mids over clean lows, rolling off
  // from 3.3 kHz.
  check({true, 0.0f, 1.0f, 3.9f}, {{200.0, 9.1},
                                   {500.0, 15.7},
                                   {1000.0, 19.4},
                                   {1600.0, 20.4},
                                   {2500.0, 20.3},
                                   {4000.0, 18.7},
                                   {6300.0, 15.7}});
  // Drive 5, Tone 5, Level 5: the classic setting, its treble going from 1 kHz.
  check({true, 0.5f, 0.5f, -4.0f}, {{200.0, 8.2},
                                    {500.0, 13.0},
                                    {1000.0, 13.2},
                                    {1600.0, 11.4},
                                    {2500.0, 8.7},
                                    {4000.0, 5.1},
                                    {6300.0, 0.3}});
}

TEST_CASE("The overdrive saturates like a real Tube Screamer, at the same input "
          "levels", "[pedals][overdrive]") {
  // Drive 5, Tone 5, Level 5, 1 kHz. This is the check on the volts-per-full-scale
  // calibration and the diode model: both have to be right for all of it to fit.
  const OverdrivePedal::Settings noon{true, 0.5f, 0.5f, -4.0f};
  for (const auto [amplitude, real] :
       {std::pair{0.003f, 13.2}, std::pair{0.01f, 7.1}, std::pair{0.03f, 0.5},
        std::pair{0.1f, -5.5}, std::pair{0.3f, -9.0}, std::pair{1.0f, -15.0}}) {
    const double db = gainDb<OverdrivePedal>(noon, 1000.0, amplitude);
    INFO("input " << amplitude << ": real " << real << " dB, plugin " << db << " dB");
    CHECK(std::abs(db - real) < 1.0);
  }
}

TEST_CASE("The overdrive stays clean in the lows and clips in the mids",
          "[pedals][overdrive]") {
  // The point of the circuit: unity below 720 Hz, so bass stays out of the diodes.
  const OverdrivePedal::Settings low{true, 0.0f, 0.5f, 0.0f};
  CHECK(thd<OverdrivePedal>(low, 80.0, 0.02f) < 0.01);
  CHECK(thd<OverdrivePedal>(low, 1000.0, 0.1f) > 0.05);
  // And a hump, even with Drive all the way down.
  CHECK(gainDb<OverdrivePedal>(low, 800.0, 0.001f) -
            gainDb<OverdrivePedal>(low, 80.0, 0.001f) > 10.0);
  // More drive, more clipping, at the same input.
  CHECK(thd<OverdrivePedal>({true, 1.0f, 1.0f, 0.0f}, 200.0, 0.02f) >
        3.0 * thd<OverdrivePedal>({true, 0.0f, 1.0f, 0.0f}, 200.0, 0.02f));
}

TEST_CASE("The overdrive's Tone runs from the circuit's dark end to its bright "
          "end", "[pedals][overdrive]") {
  // Small signal, so this is the tone section alone: nodal analysis says a corner
  // near 340 Hz with the knob down and flat to 3.3 kHz with it up.
  const auto atTone = [](float tone, double hz) {
    return gainDb<OverdrivePedal>({true, 0.0f, tone, 0.0f}, hz, 1.0e-4f);
  };
  CHECK(atTone(1.0f, 3000.0) - atTone(0.0f, 3000.0) > 15.0);
  for (const double hz : {1000.0, 2000.0, 4000.0, 8000.0}) {
    CHECK(atTone(0.5f, hz) > atTone(0.0f, hz));
    CHECK(atTone(1.0f, hz) > atTone(0.5f, hz));
  }
  // Lows pass whatever the knob says.
  CHECK(std::abs(atTone(1.0f, 80.0) - atTone(0.0f, 80.0)) < 3.0);
}

TEST_CASE("The distortion has the RAT circuit's gain", "[pedals][distortion]") {
  const auto gain = [](float distortion, double hz, float tone = 1.0f) {
    // So little signal the diodes stay out of it, with the filter wide open.
    return gainDb<DistortionPedal>({true, distortion, tone, 0.0f}, hz, 1.0e-5f);
  };
  // All the way down it's a clean, flat buffer: 1 + 0 / R.
  const double unity = gain(0.0f, 1000.0);
  for (const double hz : {100.0, 500.0, 2000.0, 5000.0})
    CHECK(std::abs(gain(0.0f, hz) - unity) < 1.0);

  // All the way up: at 100 Hz the two legs to ground together are about 390R,
  // so 1 + 100k / 390R = 48 dB. Published analysis puts the slow LM308 at no more
  // than 30 dB at 10 kHz. The feedback's 2.2 uF leg has a 129 Hz to 1.5 kHz rise
  // between them, which the op-amp cuts off short.
  const auto full100 = gain(1.0f, 100.0) - unity;
  const auto full10k = gain(1.0f, 10000.0) - unity;
  INFO("100 Hz " << full100 << " dB, 10 kHz " << full10k << " dB");
  CHECK(std::abs(full100 - 48.0) < 2.0);
  CHECK(std::abs(full10k - 30.0) < 2.0);
  CHECK(gain(1.0f, 500.0) - unity < 67.0); // the ideal op-amp's ceiling

  // The audio-taper pot: 15% of 100k at noon is 1 + 15k / 390R = 32 dB.
  CHECK(std::abs(gain(0.5f, 100.0) - unity - 32.0) < 2.0);
  // Below the 60 Hz leg the gain falls away to unity.
  CHECK(gain(1.0f, 100.0) - gain(1.0f, 15.0) > 8.0);
}

TEST_CASE("The distortion's Tone is the RAT's Filter pot", "[pedals][distortion]") {
  const auto gain = [](float tone, double hz) {
    return gainDb<DistortionPedal>({true, 0.0f, tone, 0.0f}, hz, 1.0e-5f);
  };
  const double flat = gain(1.0f, 100.0);
  // 1.5k + 100k into 3.3 nF: -3 dB at 475 Hz with the knob at its dark end.
  CHECK(std::abs(gain(0.0f, 475.0) - flat + 3.0) < 0.6);
  // 1.5k into 3.3 nF: open past the audio band at the bright end.
  CHECK(std::abs(gain(1.0f, 10000.0) - flat) < 0.8);
  // Measured on a real one: 4.8 kHz at noon.
  CHECK(std::abs(gain(0.5f, 4800.0) - flat + 3.0) < 0.6);
}

TEST_CASE("The distortion goes from nearly clean to heavy, limited at the silicon "
          "diode drop", "[pedals][distortion]") {
  const DistortionPedal::Settings down{true, 0.0f, 1.0f, 0.0f};
  const DistortionPedal::Settings noon{true, 0.5f, 1.0f, 0.0f};
  CHECK(thd<DistortionPedal>(down, 200.0, 0.02f) < 0.01);
  CHECK(thd<DistortionPedal>(noon, 200.0, 0.02f) > 0.2);

  // However hard it's hit, its flat top sits at about 0.6 V, scaled by the same
  // output gain as its unity gain. The median, as the band-limited edges overshoot.
  DistortionPedal pedal;
  pedal.prepare(rate);
  auto x = sine(200.0, rate, (int)rate, 1.0f);
  pedal.process(x.data(), (int)x.size(), {true, 1.0f, 1.0f, 0.0f});
  std::vector<float> magnitudes;
  for (size_t i = x.size() / 2; i < x.size(); ++i)
    magnitudes.push_back(std::abs(x[i]));
  std::nth_element(magnitudes.begin(), magnitudes.begin() + (long)magnitudes.size() / 2,
                   magnitudes.end());
  const auto flatGain = (float)juce::Decibels::decibelsToGain(
      gainDb<DistortionPedal>(down, 1000.0, 1.0e-5f));
  const float volts = magnitudes[magnitudes.size() / 2] / flatGain *
                      (float)analog::voltsAtFullScale;
  INFO("clips at " << volts << " V");
  CHECK(volts > 0.55f);
  CHECK(volts < 0.7f);
}

TEST_CASE("The fuzz has the Big Muff tone stack and sustain",
          "[pedals][fuzz]") {
  // Tone against itself, so the stages in front cancel out. So little signal
  // the diodes stay out of it. Corners 408 Hz and 1.86 kHz.
  const auto gain = [](float tone, double hz) {
    return gainDb<FuzzPedal>({true, 0.5f, tone, 0.0f}, hz, 1.0e-6f);
  };
  CHECK(gain(0.0f, 100.0) - gain(1.0f, 100.0) > 12.0);
  CHECK(gain(1.0f, 6000.0) - gain(0.0f, 6000.0) > 12.0);
  // The scoop: at noon the middle is well under either end of the knob.
  const double scoopAt = 800.0;
  CHECK(gain(0.5f, scoopAt) < gain(0.0f, scoopAt) - 2.0);
  CHECK(gain(0.5f, scoopAt) < gain(1.0f, scoopAt) - 2.0);

  // Sustain: more of it clips sooner, and the bottom of the knob isn't silence.
  const auto distortionAt = [](float fuzz) {
    return thd<FuzzPedal>({true, fuzz, 0.5f, 0.0f}, 200.0, 1.0e-4f);
  };
  CHECK(distortionAt(0.0f) < 0.05);
  CHECK(distortionAt(0.8f) > 3.0 * distortionAt(0.0f));
  CHECK(gainDb<FuzzPedal>({true, 0.0f, 0.5f, 0.0f}, 200.0, 0.05f) > -40.0);
}

// Reference numbers below come from real pedals: NAM captures from TONE3000 of an
// EHX NYC Big Muff Pi (philvr), a ProCo RAT (romanrecords) and an Ibanez TS9
// (madhatteramplab), measured through this plugin's own model runner. Only the
// numbers are kept here. Captures have their own rig's level and an unknown volume
// knob, so these compare shapes and ratios, not absolute levels.
TEST_CASE("The fuzz's tone follows a real Big Muff's", "[pedals][fuzz]") {
  struct Point {
    double hz, db; // relative to 200 Hz, below clipping
  };
  const auto check = [](float sustain, float tone, std::initializer_list<Point> real) {
    const FuzzPedal::Settings s{true, sustain, tone, 0.0f};
    const double reference = gainDb<FuzzPedal>(s, 200.0, 1.0e-6f);
    for (const auto &p : real) {
      const double db = gainDb<FuzzPedal>(s, p.hz, 1.0e-6f) - reference;
      INFO(p.hz << " Hz: real " << p.db << " dB, plugin " << db << " dB");
      CHECK(std::abs(db - p.db) < 2.5);
    }
  };
  // Sustain 25%, Tone 30%.
  check(0.25f, 0.3f,
        {{100.0, 0.4}, {400.0, -1.7}, {800.0, -7.2}, {1000.0, -10.1}, {1600.0, -19.2},
         {2500.0, -28.5}, {8000.0, -52.0}, {10000.0, -55.8}});
  // Sustain 75%, Tone 55%.
  check(0.75f, 0.55f,
        {{100.0, 0.6}, {400.0, -1.6}, {800.0, -8.2}, {1000.0, -11.2}, {1600.0, -17.8},
         {2500.0, -21.7}, {4000.0, -29.7}, {8000.0, -46.4}});

  // From Sustain 25% to 75%, a real one gains 11 dB below clipping.
  const double more = gainDb<FuzzPedal>({true, 0.75f, 0.55f, 0.0f}, 200.0, 1.0e-6f) -
                      gainDb<FuzzPedal>({true, 0.25f, 0.3f, 0.0f}, 200.0, 1.0e-6f);
  CHECK(std::abs(more - 11.3) < 2.0);
}

TEST_CASE("The distortion's knobs follow a real RAT's", "[pedals][distortion]") {
  // Distortion all the way down, Filter at noon, relative to 200 Hz. Clean, so this
  // is the Filter alone. 7 steps across each knob, and step 4 is noon.
  const DistortionPedal::Settings clean{true, 0.0f, 0.5f, 0.0f};
  const double reference = gainDb<DistortionPedal>(clean, 200.0, 1.0e-4f);
  for (const auto &[hz, real] :
       {std::pair{1000.0, 0.1}, std::pair{2500.0, -0.7}, std::pair{4000.0, -2.3},
        std::pair{6300.0, -4.8}, std::pair{10000.0, -8.4}}) {
    const double db = gainDb<DistortionPedal>(clean, hz, 1.0e-4f) - reference;
    INFO(hz << " Hz: real " << real << " dB, plugin " << db << " dB");
    CHECK(std::abs(db - real) < 1.2);
  }
  // Distortion at noon has 34 dB more gain at 200 Hz than down, below clipping.
  const double gained =
      gainDb<DistortionPedal>({true, 0.5f, 0.5f, 0.0f}, 200.0, 1.0e-4f) - reference;
  CHECK(std::abs(gained - 34.0) < 2.0);
}

TEST_CASE("The overdrive's Level knob is a real Tube Screamer's", "[pedals][overdrive]") {
  const auto &level = analog::overdriveLevel;
  // Noon on two real ones: 7.9 dB under full on a Mini, about 7 on a TS9.
  CHECK(std::abs(level.dbAt(0.5) - level.dbAt(1.0) + 7.5) < 0.5);
  // And the TS9 itself: Drive 5, Tone 6, Level 5 against Drive 0, Tone 6, Level 10.
  // More drive and less level come out about the same below clipping.
  const OverdrivePedal::Settings d5{true, 0.5f, 0.6f, (float)level.dbAt(0.5)};
  const OverdrivePedal::Settings d0{true, 0.0f, 0.6f, (float)level.dbAt(1.0)};
  for (const auto &[hz, real] : {std::pair{200.0, -0.2}, std::pair{1000.0, -0.4},
                                 std::pair{3150.0, -0.4}}) {
    const double db =
        gainDb<OverdrivePedal>(d5, hz, 1.0e-4f) - gainDb<OverdrivePedal>(d0, hz, 1.0e-4f);
    INFO(hz << " Hz: real " << real << " dB, plugin " << db << " dB");
    CHECK(std::abs(db - real) < 1.8);
  }
}

TEST_CASE("Drive pedals keep aliasing far below the signal", "[pedals]") {
  // 3.1 kHz at 0.2 is harsher than a guitar: nearly a square wave with its
  // harmonics way past what 4x oversampling can represent on their own.
  const OverdrivePedal::Settings od{true, 1.0f, 1.0f, 0.0f};
  const DistortionPedal::Settings dist{true, 1.0f, 1.0f, 0.0f};
  const FuzzPedal::Settings fuzz{true, 0.7f, 0.7f, 0.0f};
  constexpr int hard = 4283, soft = 1361; // 3137 and 997 Hz, whole cycles
  CHECK(inharmonicDb<OverdrivePedal>(od, hard, 0.2f) < -65.0);
  CHECK(inharmonicDb<DistortionPedal>(dist, hard, 0.2f) < -50.0);
  CHECK(inharmonicDb<FuzzPedal>(fuzz, hard, 0.2f) < -70.0);
  CHECK(inharmonicDb<OverdrivePedal>(od, soft, 0.05f) < -80.0);
  CHECK(inharmonicDb<DistortionPedal>(dist, soft, 0.05f) < -60.0);
  CHECK(inharmonicDb<FuzzPedal>(fuzz, soft, 0.05f) < -70.0);
}

TEST_CASE("Drive pedals sound the same at any sample rate", "[pedals]") {
  const double sampleRate = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
  INFO(sampleRate << " Hz");
  const auto compare = [&](auto pedalTag, auto settings) {
    using Pedal = decltype(pedalTag);
    for (const double hz : {200.0, 1000.0, 5000.0})
      for (const float amplitude : {1.0e-4f, 0.05f}) {
        INFO(hz << " Hz at " << amplitude);
        CHECK(std::abs(gainDb<Pedal>(settings, hz, amplitude, sampleRate) -
                       gainDb<Pedal>(settings, hz, amplitude, 48000.0)) < 0.7);
      }
  };
  compare(OverdrivePedal{}, OverdrivePedal::Settings{true, 0.5f, 0.5f, 0.0f});
  compare(DistortionPedal{}, DistortionPedal::Settings{true, 0.5f, 0.7f, 0.0f});
  compare(FuzzPedal{}, FuzzPedal::Settings{true, 0.7f, 0.5f, 0.0f});
}

TEST_CASE("Turning a drive pedal's knobs doesn't click", "[pedals]") {
  // The biggest jump between samples, sweeping the knobs up and back down far
  // faster than anyone turns one, against the same pedal with its knobs still. A
  // near-square wave has big jumps of its own, so only more than that is a click.
  const auto biggestJump = [](auto pedal, auto settings, auto knobsOf, bool moving,
                              float still) {
    pedal.prepare(rate);
    auto x = sine(196.0, rate, (int)rate * 2, 0.02f);
    float jump = 0.0f, previous = 0.0f;
    for (size_t start = 0; start < x.size(); start += (size_t)block) {
      const auto t = (float)start / (float)x.size();
      const float position = moving ? (t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t) : still;
      for (auto *k : knobsOf(settings))
        *k = position;
      pedal.process(x.data() + start, block, settings);
      for (size_t i = start + (size_t)block / 2; i < start + (size_t)block; ++i) {
        REQUIRE(std::isfinite(x[i]));
        if (start > 0)
          jump = std::max(jump, std::abs(x[i] - previous));
        previous = x[i];
      }
    }
    return jump;
  };
  const auto check = [&](auto pedal, auto settings, auto knobsOf) {
    float stillJump = 0.0f;
    for (const float position : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
      stillJump = std::max(stillJump,
                           biggestJump(pedal, settings, knobsOf, false, position));
    const float movingJump = biggestJump(pedal, settings, knobsOf, true, 0.0f);
    INFO("moving " << movingJump << ", still " << stillJump);
    CHECK(movingJump < 1.3f * stillJump + 1.0e-3f);
  };
  check(OverdrivePedal{}, OverdrivePedal::Settings{true},
        [](auto &s) { return std::array{&s.drive, &s.tone}; });
  check(DistortionPedal{}, DistortionPedal::Settings{true},
        [](auto &s) { return std::array{&s.distortion, &s.tone}; });
  check(FuzzPedal{}, FuzzPedal::Settings{true},
        [](auto &s) { return std::array{&s.fuzz, &s.tone}; });
}

TEST_CASE("The wah sweeps from heel to toe, and Auto opens it as you pick",
          "[pedals][wah]") {
  // Low and high partials: which one the wah lets through shows where it is.
  const auto partials = [](float position, float autoAmount, float amplitude) {
    WahPedal wah;
    wah.prepare(rate);
    auto x = sine(400.0, rate, (int)rate, amplitude);
    const auto high = sine(2000.0, rate, (int)rate, amplitude);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] += high[i];
    wah.process(x.data(), (int)x.size(), {true, position, autoAmount, 0.5f});
    for (float v : x)
      REQUIRE(std::isfinite(v));
    const auto at = [&](double f) {
      return magnitudeAt(x, (size_t)rate / 4, (size_t)rate / 2, f);
    };
    return std::pair{at(400.0), at(2000.0)};
  };
  const auto [heelLow, heelHigh] = partials(0.0f, 0.0f, 0.2f);
  const auto [toeLow, toeHigh] = partials(1.0f, 0.0f, 0.2f);
  INFO("heel " << heelLow << " / " << heelHigh << ", toe " << toeLow << " / "
               << toeHigh);
  CHECK(heelLow > 4.0 * heelHigh);
  CHECK(toeHigh > 4.0 * toeLow);

  // Heel down, but Auto lets loud playing open the filter.
  const auto [autoLow, autoHigh] = partials(0.0f, 1.0f, 0.2f);
  INFO("auto " << autoLow << " / " << autoHigh);
  CHECK(autoHigh > 4.0 * heelHigh);
  // Quiet playing barely opens it.
  const auto [quietLow, quietHigh] = partials(0.0f, 1.0f, 0.002f);
  CHECK(quietLow > 4.0 * quietHigh);
}

TEST_CASE("The boost adds clean gain and shapes bass and treble",
          "[pedals][boost]") {
  const auto gainAt = [](double freq, BoostPedal::Settings s) {
    BoostPedal boost;
    boost.prepare(rate);
    auto x = sine(freq, rate, (int)rate / 2, 0.1f);
    boost.process(x.data(), (int)x.size(), s);
    return juce::Decibels::gainToDecibels(
        magnitudeAt(x, 4800, x.size() - 4800, freq) /
        magnitudeAt(sine(freq, rate, (int)rate / 2, 0.1f), 4800,
                    x.size() - 4800, freq));
  };
  CHECK(gainAt(800.0, {true, 9.0f, 0.0f, 0.0f}) == Catch::Approx(9.0).margin(0.05));
  CHECK(gainAt(60.0, {true, 0.0f, 0.0f, 0.0f}) == Catch::Approx(0.0).margin(0.05));
  CHECK(gainAt(40.0, {true, 0.0f, 9.0f, 0.0f}) > 7.0);
  CHECK(gainAt(8000.0, {true, 0.0f, 0.0f, -9.0f}) < -7.0);
  CHECK(std::abs(gainAt(800.0, {true, 0.0f, 9.0f, 9.0f})) < 2.5); // mids stay

  // Linear: a loud sine comes out as a sine, with no added harmonics.
  BoostPedal boost;
  boost.prepare(rate);
  auto x = sine(200.0, rate, (int)rate / 2, 0.5f);
  boost.process(x.data(), (int)x.size(), {true, 20.0f, 0.0f, 0.0f});
  // Whole cycles, so the window doesn't leak into the harmonic.
  CHECK(magnitudeAt(x, 4800, 19200, 400.0) <
        1.0e-4 * magnitudeAt(x, 4800, 19200, 200.0));
}

TEST_CASE("The phaser and flanger notch where their stages and delay put "
          "them", "[pedals][phaser][flanger]") {
  // With no depth the sweep stands still, so the notches are fixed.
  const auto passes = [](auto &pedal, auto settings, double freq) {
    pedal.prepare(rate);
    auto x = sine(freq, rate, (int)rate / 2, 0.3f);
    pedal.process(x.data(), (int)x.size(), settings);
    return magnitudeAt(x, 4800, x.size() - 4800, freq) /
           magnitudeAt(sine(freq, rate, (int)rate / 2, 0.3f), 4800,
                       x.size() - 4800, freq);
  };

  // Four all-passes break at 520 Hz; each shifts 45 degrees at the first
  // notch, so the four together are 180 degrees out with the dry signal.
  PhaserPedal phaser;
  const PhaserPedal::Settings still{true, 1.0f, 0.0f, 0.0f};
  const double notch =
      rate / juce::MathConstants<double>::pi *
      std::atan(std::tan(juce::MathConstants<double>::pi * 520.0 / rate) *
                std::tan(juce::MathConstants<double>::pi / 8.0));
  INFO("phaser notch " << notch << " Hz");
  CHECK(passes(phaser, still, notch) < 0.02);
  CHECK(passes(phaser, still, 520.0) > 0.98); // the stages in phase again

  // A 2.2 ms delay cancels the frequency whose half period it is.
  FlangerPedal flanger;
  const FlangerPedal::Settings fixed{true, 1.0f, 0.0f, 0.0f};
  CHECK(passes(flanger, fixed, 1000.0 / 4.4) < 0.02);
  CHECK(passes(flanger, fixed, 1000.0 / 2.2) > 0.98);
}

TEST_CASE("The tremolo pulses at its rate, from full volume down to its depth",
          "[pedals][tremolo]") {
  for (float shape : {0.0f, 1.0f}) {
    TremoloPedal tremolo;
    tremolo.prepare(rate);
    std::vector<float> x((size_t)rate, 0.5f);
    tremolo.process(x.data(), (int)x.size(), {true, 5.0f, 0.8f, shape});
    const auto [lo, hi] = std::minmax_element(x.begin(), x.end());
    INFO("shape " << shape << ": " << *lo << " .. " << *hi);
    CHECK(*hi == Catch::Approx(0.5f).margin(1.0e-3));
    CHECK(*lo == Catch::Approx(0.1f).margin(1.0e-3));
    for (size_t i = 0; i + 9600 < x.size(); i += 487) // 5 Hz period
      REQUIRE(x[i + 9600] == Catch::Approx(x[i]).margin(1.0e-4));
    // A square wave spends longer near its extremes than a sine.
    const auto nearEdge = std::count_if(x.begin(), x.end(), [](float v) {
      return v > 0.48f || v < 0.12f;
    });
    if (shape == 0.0f)
      CHECK(nearEdge < (long)x.size() / 3);
    else
      CHECK(nearEdge > (long)x.size() * 2 / 3);
  }
}

TEST_CASE("Full feedback on the phaser and flanger stays bounded",
          "[pedals]") {
  SignalChain chain;
  chain.prepare(rate, block);
  auto s = neutral();
  s.phaser = {true, 8.0f, 1.0f, 1.0f};
  s.flanger = {true, 0.05f, 1.0f, 1.0f};
  juce::Random random(9);
  std::vector<float> x((size_t)rate * 10);
  for (auto &v : x)
    v = random.nextFloat() * 1.6f - 0.8f;
  run(chain, x, s);
  float peak = 0.0f;
  for (float v : x) {
    REQUIRE(std::isfinite(v));
    peak = std::max(peak, std::abs(v));
  }
  INFO("peak " << peak);
  CHECK(peak < 8.0f);
}

TEST_CASE("The metronome clicks on the beat and accents the bar",
          "[metronome]") {
  Metronome metronome;
  metronome.prepare(rate);
  Metronome::Settings s;
  s.running = true;
  s.bpm = 120.0f;
  s.beatsPerBar = 3;

  std::vector<float> x((size_t)rate * 3, 0.0f);
  for (size_t start = 0; start < x.size(); start += block)
    metronome.process(x.data() + start, block, s);

  // Onsets: first non-silent sample after a gap.
  std::vector<size_t> onsets;
  for (size_t i = 0; i < x.size(); ++i)
    if (std::abs(x[i]) > 1.0e-6f &&
        (onsets.empty() || i > onsets.back() + 4000))
      onsets.push_back(i);
  REQUIRE(onsets.size() == 6); // 3 s at 120 bpm
  for (size_t k = 0; k < onsets.size(); ++k)
    CHECK(std::abs((long)onsets[k] - (long)(k * 24000)) <= 1);

  const auto clickPeak = [&](size_t onset) {
    float m = 0.0f;
    for (size_t i = onset; i < onset + 1000; ++i)
      m = std::max(m, std::abs(x[i]));
    return m;
  };
  CHECK(clickPeak(onsets[0]) > clickPeak(onsets[1])); // beat one of the bar
  CHECK(clickPeak(onsets[3]) > clickPeak(onsets[2]));
  CHECK(metronome.getClickCount() == 6);

  // Stopped: the signal passes untouched once the last click has finished.
  s.running = false;
  std::vector<float> y((size_t)rate / 2, 0.25f);
  metronome.process(y.data(), (int)y.size(), s);
  for (size_t i = 2400; i < y.size(); ++i)
    REQUIRE(y[i] == 0.25f);
  CHECK(metronome.getBeatInBar() == -1);
}

namespace {
/** Just enough of a processor to hold the plugin's parameters. */
struct ParameterHost final : juce::AudioProcessor {
  juce::AudioProcessorValueTreeState state{*this, nullptr, "EvilAmplifier",
                                           params::createLayout()};
  const juce::String getName() const override { return "test"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

  void set(const char *id, float value) {
    auto *p = state.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
  }
  float get(const char *id) { return state.getRawParameterValue(id)->load(); }
};
} // namespace

TEST_CASE("Typing a value into a knob sets what it shows", "[pedals]") {
  ParameterHost host;
  const auto typed = [&host](const char *id, const juce::String &text) {
    auto *p = host.state.getParameter(id);
    return p->convertFrom0to1(p->getValueForText(text));
  };
  // Shown 0-10, stored 0-1: 3.5 used to clamp to the top and read 10.0.
  CHECK(typed(params::odDrive, "3.5") == Catch::Approx(0.35f));
  CHECK(typed(params::odDrive, "0.5") == Catch::Approx(0.05f));
  CHECK(typed(params::odDrive, "10") == 1.0f);
  CHECK(typed(params::odDrive, "11") == 1.0f);
  CHECK(typed(params::odDrive, "-1") == 0.0f);
  // The Filter's reading runs against what it stores.
  CHECK(typed(params::distTone, "3.5") == Catch::Approx(0.65f));
  // Shown as percent, stored 0-1, with or without the sign.
  CHECK(typed(params::compBlend, "35%") == Catch::Approx(0.35f));
  CHECK(typed(params::compBlend, "35") == Catch::Approx(0.35f));
  CHECK(typed(params::delayFeedback, "100%") == Catch::Approx(0.95f));
  // Shown and stored the same, including on the knobs with a pot's taper.
  CHECK(typed(params::input, "-3.5 dB") == Catch::Approx(-3.5f));
  CHECK(typed(params::input, "+3.5") == Catch::Approx(3.5f));
  CHECK(typed(params::odLevel, "-6.5") == Catch::Approx(-6.5f).margin(0.01f));
  CHECK(typed(params::boostGain, "12.5 dB") == Catch::Approx(12.5f).margin(0.01f));
  CHECK(typed(params::phaserRate, "1.25 Hz") == Catch::Approx(1.25f).margin(0.01f));
  CHECK(typed(params::delayTime, "250.5 ms") == Catch::Approx(250.5f).margin(0.01f));

  // Whatever any parameter shows, typing it back leaves it showing the same.
  for (auto *p : host.getParameters())
    for (const float position : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f}) {
      const auto shown = p->getText(position, 0);
      INFO(p->getName(64).toStdString() << " showing " << shown.toStdString());
      CHECK(p->getText(p->getValueForText(shown), 0) == shown);
    }
}

TEST_CASE("A knob's pointer is where the real pedal's would be", "[pedals]") {
  ParameterHost host;
  const auto valueAt = [&host](const char *id, float position) {
    return host.state.getParameter(id)->convertFrom0to1(position);
  };
  // Noon on a Tube Screamer's Level: about 7.5 dB under full.
  CHECK(valueAt(params::odLevel, 1.0f) == Catch::Approx(3.9f));
  CHECK(valueAt(params::odLevel, 0.5f) == Catch::Approx(3.9f - 7.5f).margin(0.1f));
  CHECK(valueAt(params::odLevel, 0.0f) == Catch::Approx(-60.0f));
  // It starts at noon, and every position round-trips.
  for (const auto *id : {params::odLevel, params::fuzzLevel, params::distLevel,
                         params::compLevel, params::boostGain}) {
    auto *p = host.state.getParameter(id);
    CHECK(p->getDefaultValue() == Catch::Approx(0.5f).margin(0.005f));
    for (const float position : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
      CHECK(p->convertTo0to1(p->convertFrom0to1(position)) ==
            Catch::Approx(position).margin(0.01f));
  }
  // A real RAT's Filter darkens clockwise, reading up as the pointer goes.
  auto *filter = host.state.getParameter(params::distTone);
  CHECK(valueAt(params::distTone, 1.0f) == 0.0f); // the dark end of its stored value
  CHECK(valueAt(params::distTone, 0.0f) == 1.0f);
  CHECK(filter->getText(filter->convertTo0to1(0.0f), 8) == "10.0");
  CHECK(filter->getText(filter->convertTo0to1(1.0f), 8) == "0.0");
  // An MXR Micro Amp: unity to 26 dB, with most of the range at the top.
  CHECK(valueAt(params::boostGain, 0.0f) == Catch::Approx(0.0f));
  CHECK(valueAt(params::boostGain, 1.0f) == Catch::Approx(26.0f).margin(0.1f));
  CHECK(valueAt(params::boostGain, 0.5f) == Catch::Approx(11.7f).margin(0.5f));
  // A Boss TR-2: 1.1 to 11 Hz.
  CHECK(valueAt(params::tremRate, 0.0f) == Catch::Approx(1.1f));
  CHECK(valueAt(params::tremRate, 1.0f) == Catch::Approx(11.1f));
  // Old sessions keep their sound: a saved dB value is still that many dB.
  host.set(params::odLevel, 0.0f);
  CHECK(host.get(params::odLevel) == Catch::Approx(0.0f));
}

TEST_CASE("EQ faders have a parabolic taper, fine around 0 dB", "[eq]") {
  ParameterHost host;
  auto *p = host.state.getParameter(params::eqBands[4]);
  const auto range = p->getNormalisableRange();
  CHECK(range.start == -Equaliser::maxGainDb);
  CHECK(range.end == Equaliser::maxGainDb);
  CHECK(range.convertFrom0to1(0.5f) == 0.0f);
  CHECK(range.convertFrom0to1(0.75f) == Catch::Approx(Equaliser::maxGainDb / 4));
  CHECK(range.convertFrom0to1(0.25f) == Catch::Approx(-Equaliser::maxGainDb / 4));
  for (const float db : {-10.0f, -3.5f, -0.25f, 0.0f, 1.0f, 6.0f, 10.0f})
    CHECK(range.convertFrom0to1(range.convertTo0to1(db)) ==
          Catch::Approx(db).margin(1.0e-4));
  host.set(params::eqBands[4], 2.347f);
  CHECK(host.get(params::eqBands[4]) == Catch::Approx(2.35f));
  host.set(params::eqBands[4], 20.0f); // beyond the range: clamped
  CHECK(host.get(params::eqBands[4]) == Equaliser::maxGainDb);
}

TEST_CASE("Presets set the pedals but leave transpose and the metronome alone",
          "[presets]") {
  ParameterHost host;
  PresetManager presets(
      host.state, [] { return PresetManager::Sources{}; },
      [](const PresetManager::Sources &) {});
  host.set(params::transpose, -2.0f);
  host.set(params::metronomeBpm, 90.0f);
  host.set(params::reverbOn, 1.0f);

  const int fuzzLead = presets.getNames().indexOf("Fuzz Lead");
  REQUIRE(fuzzLead >= 0);
  REQUIRE(presets.load(fuzzLead));
  CHECK(host.get(params::fuzzOn) == 1.0f);
  CHECK(host.get(params::delayTime) == Catch::Approx(420.0f));
  CHECK(host.get(params::chorusOn) == 0.0f); // not in the preset: default
  CHECK(host.get(params::transpose) == -2.0f);
  CHECK(host.get(params::metronomeBpm) == 90.0f);

  REQUIRE(presets.load(0)); // the default preset turns every pedal off
  for (auto *id : {params::wahOn, params::compOn, params::fuzzOn, params::odOn,
                   params::distOn, params::boostOn, params::phaserOn,
                   params::flangerOn, params::chorusOn, params::tremOn,
                   params::delayOn, params::reverbOn})
    CHECK(host.get(id) == 0.0f);
  CHECK(host.get(params::transpose) == -2.0f);
}

TEST_CASE("Presets and sessions saved with the three-band EQ keep their tone",
          "[presets]") {
  const auto saved = [](float bass, float mid, float treble) {
    auto xml = std::make_unique<juce::XmlElement>("Preset");
    for (auto [id, value] : {std::pair{"bass", bass}, std::pair{"mid", mid},
                             std::pair{"treble", treble}}) {
      auto *e = xml->createNewChildElement("Param");
      e->setAttribute("id", id);
      e->setAttribute("value", value);
    }
    return xml;
  };
  const auto bandGains = [](const juce::XmlElement &xml) {
    Equaliser::Gains gains{};
    for (size_t b = 0; b < gains.size(); ++b) {
      const auto *e = xml.getChildByAttribute("id", params::eqBands[b]);
      REQUIRE(e != nullptr);
      gains[b] = (float)e->getDoubleAttribute("value");
    }
    for (const auto *legacy : params::legacyEq)
      CHECK(xml.getChildByAttribute("id", legacy) == nullptr);
    return gains;
  };

  // The nine bands reproduce the old response at their centres.
  auto xml = saved(-3.0f, 2.0f, 6.0f);
  params::upgradeLegacyEq(*xml, "Param");
  const auto gains = bandGains(*xml);
  for (size_t b = 0; b < gains.size(); ++b) {
    const double f = Equaliser::frequencies[b];
    double was = 0.0;
    was += BiquadCoeffs::lowShelf(48000.0, 100.0, 0.707, -3.0).gainDbAt(48000.0, f);
    was += BiquadCoeffs::peak(48000.0, 800.0, 0.8, 2.0).gainDbAt(48000.0, f);
    was += BiquadCoeffs::highShelf(48000.0, 3200.0, 0.707, 6.0).gainDbAt(48000.0, f);
    const double now = gains[b]; // a fader's number is its response there
    INFO("band " << b << " was " << was << " dB, now " << now << " dB");
    CHECK(std::abs(now - was) < 0.1);
  }

  // A flat old EQ stays exactly flat (and so bit-exact).
  xml = saved(0.0f, 0.0f, 0.0f);
  params::upgradeLegacyEq(*xml, "Param");
  for (const auto g : bandGains(*xml))
    CHECK(g == 0.0f);

  // Band settings already there win; the stale three-band ones are dropped.
  xml = saved(6.0f, 6.0f, 6.0f);
  auto *band = xml->createNewChildElement("Param");
  band->setAttribute("id", params::eqBands[2]);
  band->setAttribute("value", -4.0);
  params::upgradeLegacyEq(*xml, "Param");
  CHECK(xml->getNumChildElements() == 1);
  CHECK(xml->getChildByAttribute("id", params::eqBands[2])
            ->getDoubleAttribute("value") == -4.0);
}
