#include "TestHelpers.h"
#include "dsp/Metronome.h"
#include "dsp/SignalChain.h"
#include "state/Parameters.h"
#include "state/PresetManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
} // namespace

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
  CHECK(std::abs((int)peakNear(24000) - 24000) <= 2);
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
  host.set(params::eqBands[4], 12.0f); // beyond the range: clamped
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
    double was = 0.0, now = 0.0;
    was += BiquadCoeffs::lowShelf(48000.0, 100.0, 0.707, -3.0).gainDbAt(48000.0, f);
    was += BiquadCoeffs::peak(48000.0, 800.0, 0.8, 2.0).gainDbAt(48000.0, f);
    was += BiquadCoeffs::highShelf(48000.0, 3200.0, 0.707, 6.0).gainDbAt(48000.0, f);
    for (size_t i = 0; i < gains.size(); ++i)
      now += BiquadCoeffs::peak(48000.0, Equaliser::frequencies[i],
                                Equaliser::q, gains[i])
                 .gainDbAt(48000.0, f);
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
