#include "TestHelpers.h"
#include "dsp/NamModelRunner.h"
#include "dsp/StreamingResampler.h"

#include <NAM/dsp.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace evil;
using namespace evil::test;

namespace {
std::vector<float> resampleAll(StreamingResampler &r,
                               const std::vector<float> &in, int block) {
  std::vector<float> out;
  std::vector<float> tmp((size_t)r.maxOutputsFor(block));
  for (size_t start = 0; start < in.size(); start += (size_t)block) {
    const int n = (int)std::min<size_t>((size_t)block, in.size() - start);
    r.push(in.data() + start, n);
    int m;
    while ((m = r.pop(tmp.data(), (int)tmp.size())) > 0)
      out.insert(out.end(), tmp.begin(), tmp.begin() + m);
  }
  return out;
}
} // namespace

TEST_CASE("Resampler reproduces an in-band sine at the new rate, without delay",
          "[resampler]") {
  const auto [from, to] =
      GENERATE(std::pair{44100.0, 48000.0}, std::pair{48000.0, 44100.0},
               std::pair{96000.0, 48000.0}, std::pair{48000.0, 88200.0});
  StreamingResampler r;
  r.prepare(from, to, 256);
  const auto in = sine(1000.0, from, (int)from); // 1 s
  const auto out = resampleAll(r, in, 256);

  // Output sample j is the input's value at time j / to: compare against the
  // ideal sine away from the start-up transient and the end (which lacks
  // look-ahead).
  const auto ideal = sine(1000.0, to, (int)out.size());
  float maxError = 0.0f;
  for (size_t j = 200; j + 200 < out.size(); ++j)
    maxError = std::max(maxError, std::abs(out[j] - ideal[j]));
  INFO(from << " -> " << to << " max error " << maxError);
  CHECK(maxError < 1.0e-3f); // better than -60 dB relative to the 0.5 amplitude
  // Everything is produced except the last look-ahead's worth, which needs
  // future input.
  const double expected =
      (double)in.size() * to / from - r.getLookaheadInputSamples() * to / from;
  CHECK(std::abs((double)out.size() - expected) <= 2.0);
}

TEST_CASE("Resampler rejects content that would alias when downsampling",
          "[resampler]") {
  // 23 kHz is above the 22.05 kHz Nyquist of 44.1 kHz: it must be filtered, not
  // folded to 21.1 kHz.
  StreamingResampler r;
  r.prepare(48000.0, 44100.0, 512);
  const auto in = sine(23000.0, 48000.0, 48000, 0.5f);
  const auto out = resampleAll(r, in, 512);
  const double level =
      rms(out.data() + 1000, (int)out.size() - 2000) / (0.5 / std::sqrt(2.0));
  INFO("residual " << juce::Decibels::gainToDecibels(level) << " dB");
  CHECK(level < 0.001); // > 60 dB rejection
}

TEST_CASE("Resampler output does not depend on how input is split into blocks",
          "[resampler]") {
  juce::Random random(1);
  std::vector<float> in(20000);
  for (auto &x : in)
    x = random.nextFloat() - 0.5f;

  StreamingResampler a, b;
  a.prepare(44100.0, 48000.0, 1024);
  b.prepare(44100.0, 48000.0, 1024);
  const auto big = resampleAll(a, in, 1000);
  const auto small = resampleAll(b, in, 37);
  REQUIRE(big.size() == small.size());
  for (size_t i = 0; i < big.size(); ++i)
    REQUIRE(big[i] == small[i]);
}

TEST_CASE(
    "Model runner resamples transparently with exactly the latency it reports",
    "[resampler][runner]") {
  // nam::DSP's base implementation is an identity model, so the runner's output
  // must be the input delayed by getLatencySamples() (band-limited by the round
  // trip).
  const double hostRate = GENERATE(44100.0, 96000.0, 48000.0);
  const int block = GENERATE(128, 333);
  NamModelRunner runner(std::make_unique<nam::DSP>(1, 1, 48000.0), {});
  runner.prepare(hostRate, block);
  CHECK(runner.isResampling() == (hostRate != 48000.0));
  const int latency = runner.getLatencySamples();

  const auto in = sine(997.0, hostRate, (int)hostRate / 2, 0.5f);
  std::vector<float> out(in.size());
  for (size_t start = 0; start < in.size(); start += (size_t)block)
    runner.process(in.data() + start, out.data() + start,
                   (int)std::min<size_t>((size_t)block, in.size() - start));

  float maxError = 0.0f;
  for (size_t i = (size_t)latency + 500; i < in.size(); ++i)
    maxError = std::max(maxError, std::abs(out[i] - in[i - (size_t)latency]));
  INFO("host " << hostRate << " block " << block << " latency " << latency
               << " max error " << maxError);
  CHECK(maxError < 2.0e-3f);
  if (hostRate == 48000.0)
    CHECK(latency == 0);
}
