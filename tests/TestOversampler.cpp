#include "TestHelpers.h"
#include "dsp/AnalogStages.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace evil;
using namespace evil::test;

namespace {
double tone(const std::vector<double> &x, double rate, double freq) {
  // Blackman window: the far-off main tone mustn't leak over a -110 dB reading.
  const double w = 2.0 * juce::MathConstants<double>::pi * freq / rate;
  double re = 0.0, im = 0.0, windowSum = 0.0;
  const auto n = x.size();
  for (size_t i = 0; i < n; ++i) {
    const double t = (double)i / (double)(n - 1);
    const double window =
        0.42 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * t) +
        0.08 * std::cos(4.0 * juce::MathConstants<double>::pi * t);
    re += window * x[i] * std::cos(w * (double)i);
    im += window * x[i] * std::sin(w * (double)i);
    windowSum += window;
  }
  return 2.0 * std::sqrt(re * re + im * im) / windowSum;
}
} // namespace

TEST_CASE("The oversampler passes the audio band and keeps images out of it",
          "[oversampler]") {
  analog::Oversampler oversampler;
  constexpr double rate = 48000.0;
  oversampler.prepare(rate);
  REQUIRE(oversampler.getFactor() == 4);

  const auto roundTrip = [&](double freq) {
    oversampler.reset();
    std::vector<double> out((size_t)rate);
    std::array<double, analog::Oversampler::maxFactor> buffer;
    for (size_t i = 0; i < out.size(); ++i) {
      oversampler.up(std::sin(2.0 * juce::MathConstants<double>::pi * freq *
                              (double)i / rate),
                     buffer.data());
      out[i] = oversampler.down(buffer.data());
    }
    return tone({out.begin() + 4800, out.end()}, rate, freq);
  };
  for (const double freq : {50.0, 1000.0, 10000.0, 18000.0})
    CHECK(roundTrip(freq) == Catch::Approx(1.0).margin(1.0e-3));

  // What a clipper would add at the 4x rate must not fold into the audio band.
  const auto folded = [&](double freq) {
    oversampler.reset();
    const double fast = rate * 4.0;
    std::vector<double> out((size_t)rate);
    std::array<double, analog::Oversampler::maxFactor> buffer;
    long long t = 0;
    for (auto &v : out) {
      for (auto &b : buffer)
        b = std::sin(2.0 * juce::MathConstants<double>::pi * freq *
                     (double)(t++) / fast);
      v = oversampler.down(buffer.data());
    }
    const double image = std::fmod(freq, rate);
    const double audible = image > rate / 2.0 ? rate - image : image;
    return tone({out.begin() + 4800, out.end()}, rate, audible);
  };
  for (const double freq : {30000.0, 40000.0, 47000.0, 50000.0, 70000.0, 90000.0})
    CHECK(juce::Decibels::gainToDecibels(folded(freq), -200.0) < -100.0);

  // Rising rates need less of it, and at the top none.
  oversampler.prepare(96000.0);
  CHECK(oversampler.getFactor() == 2);
  oversampler.prepare(192000.0);
  CHECK(oversampler.getFactor() == 1);
}

TEST_CASE("The diode clipper limits at the silicon drop and its antialiased "
          "curve is the true one", "[oversampler]") {
  analog::DiodeClipper clipper;
  clipper.reset();
  constexpr double seriesOhms = 1000.0;

  // Slow enough that averaging between samples doesn't move anything.
  const auto settle = [&](double volts) {
    double y = 0.0;
    for (int i = 0; i < 200; ++i)
      y = clipper.process(volts * (1.0 + (double)i * 1.0e-4), seriesOhms);
    return y;
  };
  // 1 mA through a 1N4148 is about 0.58 V: 1.58 V behind 1k.
  CHECK(settle(1.58) == Catch::Approx(0.584).margin(0.01));
  CHECK(settle(-1.58) == Catch::Approx(-0.584).margin(0.01));
  // Far below the knee the diodes do nothing.
  CHECK(settle(0.1) == Catch::Approx(0.1).margin(0.002));
  // The curve is odd and it keeps rising, logarithmically, instead of flat-topping.
  CHECK(settle(3.0) > settle(1.58));
  CHECK(settle(3.0) < 0.7);
}
