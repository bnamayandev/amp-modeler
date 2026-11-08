#include "TestHelpers.h"
#include "dsp/Loudness.h"
#include "dsp/SignalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace evil;
using namespace evil::test;

namespace {
constexpr int block = 128;
constexpr const char *builtin = "builtin:BlackstarHT1";

/** Captures to match, in a scratch directory: the built-in one turned 20 dB down,
    which is the same amp captured quieter, and a different amp altogether. */
struct Captures {
  Captures() {
    dir.createDirectory();
    const auto text = juce::File(EVIL_TEST_DATA_DIR)
                          .getChildFile("../../resources/models/BlackstarHT1.nam")
                          .loadFileAsString();
    // NAM takes a WaveNet's head scale from the last of its weights.
    const auto headScale = text.lastIndexOf("0.02]");
    REQUIRE(headScale > 0);
    quiet = dir.getChildFile("quiet.nam");
    quiet.replaceWithText(text.replaceSection(headScale, 5, "0.002]"));
    other = juce::File(NAM_EXAMPLE_MODELS_DIR).getChildFile("wavenet_a1_standard.nam");
    REQUIRE(other.existsAsFile());

    // A short IR with far more top than the built-in one, as a bright close mic
    // might give, and at another rate, as plenty are.
    bright = dir.getChildFile("bright.wav");
    std::vector<float> ir(512, 0.0f);
    for (size_t i = 0; i < ir.size(); ++i)
      ir[i] = (i % 2 == 0 ? 1.0f : -0.6f) * std::exp(-(float)i / 40.0f);
    writeWav(bright, ir, 44100.0);
  }
  ~Captures() { dir.deleteRecursively(); }

  juce::File dir = juce::File::createTempFile("evil-levels");
  juce::File quiet, other, bright;
};

/** Runs silence, giving JUCE's thread time to build any new IR and the chain
    time to fade it in. */
void settle(SignalChain &chain, const SignalChain::Settings &s) {
  std::vector<float> silence(block, 0.0f);
  for (int i = 0; i < 200; ++i) {
    std::fill(silence.begin(), silence.end(), 0.0f);
    chain.process(silence.data(), block, s);
    juce::Thread::sleep(1);
  }
  chain.getModels().collectGarbage();
}

/** The chain's level for the reference guitar, read as a capture's level is. */
double levelOf(SignalChain &chain, const SignalChain::Settings &s) {
  settle(chain, s);
  const auto lead = (size_t)(0.1 * loudness::rate); // as the capture is measured
  std::vector<float> played(lead, 0.0f);
  played.insert(played.end(), loudness::referenceGuitar().begin(),
                loudness::referenceGuitar().end());
  for (size_t start = 0; start < played.size(); start += block)
    chain.process(played.data() + start,
                  (int)std::min<size_t>(block, played.size() - start), s);
  auto weighted = loudness::kWeighted(std::move(played));
  weighted.erase(weighted.begin(), weighted.begin() + (long)lead);
  return loudness::levelDb(weighted);
}

SignalChain::Settings plain(bool cabOn) {
  SignalChain::Settings s;
  s.gateOn = false;
  s.cabOn = cabOn;
  return s;
}

struct Setup {
  juce::String model, ir;
  bool cabOn = true;
};

double levelOf(const Setup &setup, bool matching) {
  SignalChain chain;
  chain.setMatchingLevels(matching);
  chain.prepare(48000.0, block);
  juce::String error;
  REQUIRE(chain.getCab().load(setup.ir, error));
  REQUIRE(chain.getModels().loadSynchronously(setup.model, error));
  return levelOf(chain, plain(setup.cabOn));
}

/** Block levels in dB, for following the volume through a change. */
std::vector<double> blockLevels(const std::vector<float> &x) {
  std::vector<double> levels;
  for (size_t start = 0; start + block <= x.size(); start += block)
    levels.push_back(20.0 * std::log10(rms(x.data() + start, block) + 1.0e-9));
  return levels;
}

/** Largest sample-to-sample step: a click shows up as a jump far above the
    signal's own slope. */
float maxStep(const std::vector<float> &x, size_t from, size_t to) {
  float m = 0.0f;
  for (size_t i = from + 1; i < to; ++i)
    m = std::max(m, std::abs(x[i] - x[i - 1]));
  return m;
}
} // namespace

TEST_CASE("Loudness weights frequencies as BS.1770 does", "[levels]") {
  const auto weighedSine = [](double hz) {
    const auto x = sine(hz, loudness::rate, 48000, 0.5f);
    const auto weighted = loudness::kWeighted(x);
    return loudness::levelDb({weighted.begin() + 24000, weighted.end()}) -
           loudness::levelDb({x.begin() + 24000, x.end()});
  };
  CHECK(loudness::levelDb(sine(1000.0, loudness::rate, 48000, 1.0f)) ==
        Catch::Approx(-3.01).margin(0.01));
  // BS.1770's own figures: +0.7 dB at 1 kHz, +4 dB well up the top, and the
  // deepest lows cut.
  CHECK(weighedSine(1000.0) == Catch::Approx(0.7).margin(0.1));
  CHECK(weighedSine(8000.0) == Catch::Approx(4.0).margin(0.3));
  CHECK(weighedSine(20.0) < -10.0);
}

TEST_CASE("An IR's gain is read as a convolution would play it", "[levels]") {
  juce::Random random(4);
  std::vector<float> signal(3000), ir(200);
  for (auto &x : signal)
    x = random.nextFloat() - 0.5f;
  for (size_t i = 0; i < ir.size(); ++i)
    ir[i] = (random.nextFloat() - 0.5f) * std::exp(-(float)i / 30.0f);

  std::vector<float> convolved(signal.size() + ir.size() - 1, 0.0f);
  for (size_t i = 0; i < signal.size(); ++i)
    for (size_t j = 0; j < ir.size(); ++j)
      convolved[i + j] += signal[i] * ir[j];
  const double direct = loudness::levelDb(convolved) +
                        10.0 * std::log10((double)convolved.size() / (double)signal.size()) -
                        loudness::levelDb(signal);
  CHECK(loudness::gainThroughDb(signal, ir) == Catch::Approx(direct).margin(0.01));

  CHECK(loudness::gainThroughDb(signal, {1.0f}) == Catch::Approx(0.0).margin(0.001));
  CHECK(loudness::gainThroughDb(signal, {0.5f}) == Catch::Approx(-6.02).margin(0.01));
  CHECK(loudness::gainThroughDb({}, ir) == 0.0);

  // An IR at another rate plays with the same response once resampled, up or down.
  std::vector<float> click(64, 0.0f);
  click[8] = 1.0f;
  const auto low = sine(200.0, loudness::rate, 9600, 0.5f);
  for (const double rate : {44100.0, 96000.0})
    CHECK(loudness::gainThroughDb(low, loudness::atRate(click.data(), 64, rate)) ==
          Catch::Approx(0.0).margin(0.2));
}

TEST_CASE("Captures measure their level as they load", "[levels]") {
  const Captures captures;
  ModelManager models;
  juce::String error;
  REQUIRE(models.loadSynchronously(builtin, error));
  const auto loud = models.getStatus().info;
  REQUIRE(models.loadSynchronously(captures.quiet.getFullPathName(), error));
  const auto quiet = models.getStatus().info;
  REQUIRE(loud.hasLevel);
  REQUIRE(quiet.hasLevel);
  CHECK(loud.levelDb - quiet.levelDb == Catch::Approx(20.0).margin(0.01));
  // A file's own figure isn't changed, and isn't what's matched.
  CHECK(quiet.loudnessDb == loud.loudnessDb);
}

TEST_CASE("The default rig is as loud with levels matched as without", "[levels]") {
  const Setup defaults{builtin, "builtin:EvilCab4x12"};
  const auto raw = levelOf(defaults, false);
  const auto matched = levelOf(defaults, true);
  INFO("raw " << raw << " dB, matched " << matched << " dB");
  CHECK(matched == Catch::Approx(raw).margin(0.05));
  CHECK(matched == Catch::Approx(loudness::targetDb).margin(0.05));
}

TEST_CASE("With levels matched every capture and IR plays at the same level",
          "[levels]") {
  const Captures captures;
  const auto quiet = captures.quiet.getFullPathName();
  const auto other = captures.other.getFullPathName();
  const auto bright = captures.bright.getFullPathName();
  const std::vector<Setup> setups{
      {builtin, "builtin:EvilCab4x12"},
      {quiet, "builtin:EvilCab4x12"},
      {other, "builtin:EvilCab4x12"},
      {builtin, bright},
      {quiet, bright},
      {other, bright},
      {builtin, "builtin:EvilCab4x12", false}, // the cabinet switched off
      {quiet, bright, false},
      {other, bright, false},
  };

  double rawLowest = 1000.0, rawHighest = -1000.0;
  for (const auto &setup : setups) {
    const auto raw = levelOf(setup, false);
    const auto matched = levelOf(setup, true);
    INFO(setup.model << " through " << setup.ir << (setup.cabOn ? "" : " (cab off)")
                     << ": raw " << raw << " dB, matched " << matched << " dB");
    CHECK(matched == Catch::Approx(loudness::targetDb).margin(0.1));
    rawLowest = std::min(rawLowest, raw);
    rawHighest = std::max(rawHighest, raw);
  }
  // Without it, they're all over the place.
  CHECK(rawHighest - rawLowest > 20.0);
}

TEST_CASE("Changing the IR after the capture keeps the level", "[levels]") {
  const Captures captures;
  SignalChain chain;
  chain.setMatchingLevels(true);
  chain.prepare(48000.0, block);
  juce::String error;
  // The other way round to the plugin starting: the IR is matched to the capture
  // already playing.
  REQUIRE(chain.getModels().loadSynchronously(captures.other.getFullPathName(), error));
  REQUIRE(chain.getCab().load("builtin:EvilCab4x12", error));
  CHECK(levelOf(chain, plain(true)) == Catch::Approx(loudness::targetDb).margin(0.1));
  REQUIRE(chain.getCab().load(captures.bright.getFullPathName(), error));
  CHECK(levelOf(chain, plain(true)) == Catch::Approx(loudness::targetDb).margin(0.1));

  // With no capture, the IR is matched to the bare guitar.
  chain.getModels().requestUnload();
  CHECK(levelOf(chain, plain(true)) ==
        Catch::Approx(loudness::levelDb(loudness::weightedReferenceGuitar())).margin(0.1));
}

TEST_CASE("Swapping captures with levels matched doesn't jump in volume", "[levels]") {
  const Captures captures;
  // The same amp 20 dB quieter, and a different amp whose IR level changes too.
  for (const auto &next : {captures.quiet, captures.other})
    for (const bool cabOn : {false, true}) {
      SignalChain chain;
      chain.setMatchingLevels(true);
      chain.prepare(48000.0, block);
      juce::String error;
      REQUIRE(chain.getCab().load("builtin:EvilCab4x12", error));
      REQUIRE(chain.getModels().loadSynchronously(builtin, error));
      const auto s = plain(cabOn);
      settle(chain, s);

      // A held note, about as hard as the reference guitar.
      auto x = sine(196.0, 48000.0, 48000 * 3, 0.05f);
      const auto run = [&](size_t from, size_t to) {
        for (size_t start = from; start < to; start += block) {
          chain.process(x.data() + start, block, s);
          juce::Thread::sleep(0); // lets JUCE's IR thread in, as real time would
        }
      };
      const size_t swapAt = 48000 - 48000 % block;
      run(0, swapAt);
      REQUIRE(chain.getModels().loadSynchronously(next.getFullPathName(), error));
      run(swapAt, x.size() - x.size() % block);

      const auto levels = blockLevels(x);
      const auto steady = [&levels](size_t from, size_t to) {
        double sum = 0.0;
        for (auto b = from; b < to; ++b)
          sum += levels[b];
        return sum / (double)(to - from);
      };
      const size_t swapBlock = swapAt / block;
      const auto before = steady(swapBlock - 100, swapBlock);
      const auto after = steady(levels.size() - 100, levels.size());
      double loudest = -1000.0;
      for (auto b = swapBlock; b < swapBlock + 100; ++b)
        loudest = std::max(loudest, levels[b]);
      INFO(next.getFileName() << (cabOn ? ", cab on" : ", cab off") << ": before "
                              << before << " dB, after " << after
                              << " dB, loudest in the swap " << loudest << " dB");
      if (next == captures.quiet) // the same amp: exactly the same level
        CHECK(after == Catch::Approx(before).margin(0.1));
      else // a held note isn't the reference guitar, but it lands near it
        CHECK(std::abs(after - before) < 4.0);
      CHECK(loudest < std::max(before, after) + 1.0);
      chain.getModels().collectGarbage();
    }
}

TEST_CASE("Turning matching on and off dips rather than jumping", "[levels]") {
  const Captures captures;
  for (const bool cabOn : {false, true}) {
    SignalChain chain;
    chain.prepare(48000.0, block);
    juce::String error;
    REQUIRE(chain.getCab().load("builtin:EvilCab4x12", error));
    REQUIRE(chain.getModels().loadSynchronously(captures.quiet.getFullPathName(), error));
    const auto s = plain(cabOn);
    settle(chain, s);

    auto x = sine(196.0, 48000.0, 48000 * 3, 0.05f);
    const size_t on = 48000 - 48000 % block, off = 2 * on;
    for (size_t start = 0; start + block <= x.size(); start += block) {
      if (start == on || start == off)
        chain.setMatchingLevels(start == on);
      chain.process(x.data() + start, block, s);
      juce::Thread::sleep(1); // about real time, which the IR's swap relies on
    }

    const auto levels = blockLevels(x);
    const auto window = (size_t)(0.3 * 48000) / block;
    // A held note's level wobbles a little from block to block: the loudest of a
    // steady stretch.
    const auto steadyLevel = [&](size_t before) {
      return *std::max_element(levels.begin() + (long)(before - window),
                               levels.begin() + (long)before);
    };
    const auto quietLevel = steadyLevel(on / block), matchedLevel = steadyLevel(off / block);
    double loudest = -1000.0, quietest = 1000.0;
    for (const auto from : {on / block, off / block})
      for (auto b = from; b < from + window; ++b) {
        loudest = std::max(loudest, levels[b]);
        quietest = std::min(quietest, levels[b]);
      }
    INFO((cabOn ? "cab on" : "cab off") << ": " << quietLevel << " dB, then "
                                        << matchedLevel << " dB matched; loudest "
                                        << loudest << " dB on the way");
    CHECK(matchedLevel - quietLevel > 15.0); // it's a quiet capture: it came up
    CHECK(steadyLevel(levels.size()) == Catch::Approx(quietLevel).margin(0.1));
    CHECK(loudest < std::max(quietLevel, matchedLevel) + 0.1); // never past it
    CHECK(quietest < quietLevel - 40.0);                       // by way of a dip
    // Without a click into it or out of it.
    const float louder = std::max(maxStep(x, on - window * block, on),
                                  maxStep(x, off - window * block, off));
    CHECK(maxStep(x, on, on + window * block) <= 1.05f * louder);
    CHECK(maxStep(x, off, off + window * block) <= 1.05f * louder);
  }
}
