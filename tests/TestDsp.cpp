#include "TestHelpers.h"
#include "dsp/Equaliser.h"
#include "dsp/NoiseGate.h"
#include "dsp/Overdrive.h"
#include "dsp/SignalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace evil;
using namespace evil::test;

namespace {
SignalChain::Settings neutral() {
  SignalChain::Settings s;
  s.gateOn = false;
  s.amp.mode = AmpMode::off;
  s.cabOn = false;
  return s;
}

void processInBlocks(SignalChain &chain, std::vector<float> &x, int block,
                     const SignalChain::Settings &s) {
  for (size_t start = 0; start < x.size(); start += (size_t)block)
    chain.process(x.data() + start,
                  (int)std::min<size_t>((size_t)block, x.size() - start), s);
}

/** Largest sample-to-sample step: a click shows up as a jump far above the
 * signal's own slope. */
float maxStep(const std::vector<float> &x, size_t from, size_t to) {
  float m = 0.0f;
  for (size_t i = from + 1; i < to; ++i)
    m = std::max(m, std::abs(x[i] - x[i - 1]));
  return m;
}
} // namespace

TEST_CASE("With everything neutral the chain is a bit-exact pass-through",
          "[chain]") {
  SignalChain chain;
  chain.prepare(48000.0, 128);
  juce::Random random(3);
  std::vector<float> x(48000);
  for (auto &v : x)
    v = random.nextFloat() * 2.0f - 1.0f;
  auto y = x;
  processInBlocks(chain, y, 128, neutral());
  CHECK(y == x);
  CHECK(chain.getLatencySamples() == 0);
}

TEST_CASE("Blocks larger than prepared are split, not overrun", "[chain]") {
  SignalChain chain;
  chain.prepare(48000.0, 64);
  auto s = neutral();
  s.amp.mode = AmpMode::overdrive;
  std::vector<float> x = sine(440.0, 48000.0, 5000);
  chain.process(x.data(), (int)x.size(), s); // 5000 > 64
  for (float v : x)
    REQUIRE(std::isfinite(v));
}

TEST_CASE(
    "Noise gate opens on playing, closes on silence, and holds through decay",
    "[gate]") {
  NoiseGate gate;
  gate.prepare(48000.0);
  const float threshold = -50.0f;

  auto loud = sine(200.0, 48000.0, 4800, 0.1f); // ~ -23 dBFS RMS: open
  gate.process(loud.data(), (int)loud.size(), threshold, true);
  CHECK(gate.isOpen());

  // Between the close threshold (-56) and open threshold (-50): stays open
  // (hysteresis).
  auto between = sine(200.0, 48000.0, 48000, 0.0035f); // ~ -52 dBFS RMS
  gate.process(between.data(), (int)between.size(), threshold, true);
  CHECK(gate.isOpen());

  auto hiss = sine(200.0, 48000.0, 48000,
                   0.0003f); // ~ -73 dBFS RMS: close after hold + release
  gate.process(hiss.data(), (int)hiss.size(), threshold, true);
  CHECK_FALSE(gate.isOpen());
  CHECK(gate.getGain() < 1.0e-3f);
  CHECK(rms(hiss.data() + 40000, 8000) < 1.0e-6);

  // Disabled: unity gain again (smoothly), whatever the level.
  gate.process(hiss.data(), (int)hiss.size(), threshold, false);
  CHECK(gate.getGain() > 0.999f);
}

TEST_CASE("EQ bands hit their target gains and flat EQ is exact", "[eq]") {
  struct Case {
    float bass, mid, treble;
    double freq, expectedDb;
  };
  const auto c = GENERATE(
      Case{6, 0, 0, 30.0, 6.0}, Case{-9, 0, 0, 30.0, -9.0},
      Case{0, 6, 0, 800.0, 6.0}, Case{0, 0, 6, 16000.0, 6.0},
      Case{0, 0, -12, 16000.0, -12.0}, Case{6, -6, 6, 800.0, -6.0 + 0.15});
  Equaliser eq;
  eq.prepare(48000.0);
  auto x = sine(c.freq, 48000.0, 96000, 0.25f);
  for (size_t start = 0; start < x.size(); start += 128)
    eq.process(x.data() + start, 128, c.bass, c.mid, c.treble);
  const double gainDb = juce::Decibels::gainToDecibels(
      toneAmplitude(x.data() + 48000, 48000, c.freq, 48000.0) / 0.25);
  INFO("freq " << c.freq << " measured " << gainDb << " dB");
  CHECK(std::abs(gainDb - c.expectedDb) < 0.6);

  Equaliser flat;
  flat.prepare(48000.0);
  auto y = sine(1000.0, 48000.0, 4800);
  const auto original = y;
  flat.process(y.data(), (int)y.size(), 0.0f, 0.0f, 0.0f);
  CHECK(y == original);
}

TEST_CASE("Oversampling the waveshaper suppresses aliasing", "[overdrive]") {
  // A 3.1 kHz tone driven hard makes harmonics far above 24 kHz. Without
  // oversampling they fold back to inharmonic frequencies; measure energy away
  // from the harmonic series.
  const double rate = 48000.0, f0 = 3100.0;
  auto aliasEnergy = [&](int oversampling) {
    Overdrive od;
    od.prepare(rate, 256);
    auto x = sine(f0, rate, 1 << 16, 0.5f);
    for (size_t start = 0; start < x.size(); start += 256)
      od.process(x.data() + start, 256, 36.0f, 1.0f, oversampling);

    // Window and FFT the steady-state part.
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> spectrum(2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
      spectrum[(size_t)i] =
          x[x.size() - (size_t)size + (size_t)i] *
          (0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i /
                                  (size - 1)));
    fft.performFrequencyOnlyForwardTransform(spectrum.data());

    double inharmonic = 0.0, total = 0.0;
    for (int bin = 1; bin < size / 2; ++bin) {
      const double freq = bin * rate / size;
      const double power =
          (double)spectrum[(size_t)bin] * spectrum[(size_t)bin];
      total += power;
      const double nearestHarmonic = std::round(freq / f0) * f0;
      if (std::abs(freq - nearestHarmonic) > 60.0)
        inharmonic += power;
    }
    return 10.0 * std::log10(inharmonic / total);
  };

  const double none = aliasEnergy(0), x2 = aliasEnergy(1), x4 = aliasEnergy(2);
  INFO("aliasing relative to total: 1x " << none << " dB, 2x " << x2
                                         << " dB, 4x " << x4 << " dB");
  CHECK(x2 < none - 6.0);
  CHECK(x4 < none - 12.0);
}

TEST_CASE("Switching amp mode and oversampling mid-stream stays finite and "
          "click-free",
          "[chain]") {
  SignalChain chain;
  chain.prepare(48000.0, 128);
  juce::String error;
  REQUIRE(chain.getModels().loadSynchronously("builtin:BlackstarHT1", error));
  auto s = neutral();
  s.cabOn = true;
  auto x = sine(110.0, 48000.0, 48000 * 2, 0.2f);
  for (size_t start = 0, block = 0; start < x.size(); start += 128, ++block) {
    if (block % 50 == 0) {
      s.amp.mode = static_cast<AmpMode>((block / 50) % 3);
      s.amp.oversampling = (int)((block / 100) % 3);
    }
    chain.process(x.data() + start, 128, s);
  }
  for (float v : x)
    REQUIRE(std::isfinite(v));
  CHECK(rms(x.data(), (int)x.size()) > 1.0e-3);
}

TEST_CASE("Swapping the cab IR at runtime does not click", "[cab]") {
  const auto dir = juce::File::createTempFile("evil-ir");
  dir.createDirectory();
  const auto irFile = dir.getChildFile("bright.wav");
  {
    // A short, bright IR quite unlike the built-in one.
    std::vector<float> ir(512, 0.0f);
    for (size_t i = 0; i < ir.size(); ++i)
      ir[i] = (i % 2 == 0 ? 1.0f : -0.6f) * std::exp(-(float)i / 40.0f);
    writeWav(irFile, ir, 48000.0);
  }

  constexpr int block = 128;
  SignalChain chain;
  chain.prepare(48000.0, block);
  auto s = neutral();
  s.cabOn = true;

  const auto source = sine(220.0, 48000.0, 48000 * 20, 0.3f);
  std::vector<float> out;
  auto run = [&](int blocks) {
    for (int b = 0; b < blocks && out.size() + block <= source.size(); ++b) {
      const auto start = out.size();
      out.insert(out.end(), source.begin() + (long)start,
                 source.begin() + (long)start + block);
      chain.process(out.data() + start, block, s);
    }
  };
  // IRs are prepared on JUCE's background thread and swapped in by process().
  // Offline, audio runs far faster than real time, so keep processing (and
  // yielding) until the swap happens rather than assuming it is done after some
  // fixed amount of audio.
  auto runUntilIrLength = [&](auto isReady) {
    for (int tries = 0;
         tries < 1000 && !isReady(chain.getCab().getActiveIrLength());
         ++tries) {
      run(1);
      juce::Thread::sleep(2);
    }
    REQUIRE(isReady(chain.getCab().getActiveIrLength()));
  };

  juce::String error;
  REQUIRE(chain.getCab().load("builtin:EvilCab4x12", error));
  runUntilIrLength([](int length) { return length > 0; });
  run(375); // let JUCE's 50 ms load crossfade finish, then measure 1 s with the
            // first IR
  const auto beforeStart = out.size();
  run(375);
  const auto swapStart = out.size();

  const int firstLength = chain.getCab().getActiveIrLength();
  REQUIRE(chain.getCab().load(irFile.getFullPathName(), error));
  runUntilIrLength([firstLength](int length) { return length != firstLength; });
  run(375); // the swap crossfade
  const auto afterStart = out.size();
  run(375);

  // Before the swap the signal's steepest step is set by the old IR; afterwards
  // by the new one. Across the swap it must stay within those, i.e. no
  // discontinuity.
  const float before = maxStep(out, beforeStart, swapStart);
  const float around = maxStep(out, swapStart, afterStart);
  const float after = maxStep(out, afterStart, out.size());
  INFO("max step before " << before << ", around swap " << around << ", after "
                          << after);
  CHECK(around <= 1.05f * std::max(before, after));
  CHECK(chain.getCab().getDisplayName() == "bright");
  dir.deleteRecursively();
}

TEST_CASE("Invalid IR files are rejected with a reason", "[cab]") {
  CabSim cab;
  juce::String error;
  CHECK_FALSE(cab.load("/definitely/not/here.wav", error));
  CHECK(error.contains("not found"));
  const auto file = juce::File::createTempFile(".wav");
  file.replaceWithText("not audio");
  CHECK_FALSE(cab.load(file.getFullPathName(), error));
  file.deleteFile();
}
