#include "Loudness.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evil::loudness {
namespace {
constexpr int chordSamples = (int)(1.2 * rate);
// About halfway, in dB, between the tone meter's gentle (0.05) and hard (0.2) chords.
constexpr float everydayPeak = 0.1f;

/** Adds amplitude * e^(-decay t) * sin(2 pi hz t + phase) by turning a phasor, as a
    chord has a hundred-odd of these and each is a whole chord long. */
void addPartial(std::vector<float> &out, double hz, double amplitude, double decay,
                double phase) {
  const double shrink = std::exp(-decay / rate), angle = 2.0 * std::numbers::pi * hz / rate;
  const double stepRe = shrink * std::cos(angle), stepIm = shrink * std::sin(angle);
  double re = amplitude * std::cos(phase), im = amplitude * std::sin(phase);
  for (auto &x : out) {
    x += (float)im;
    const double nextRe = re * stepRe - im * stepIm;
    im = re * stepIm + im * stepRe;
    re = nextRe;
  }
}

/** A biquad in transposed direct form II, with a0 = 1. */
void filter(std::vector<float> &x, double b0, double b1, double b2, double a1, double a2) {
  double z1 = 0.0, z2 = 0.0;
  for (auto &sample : x) {
    const double in = sample, out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    sample = (float)out;
  }
}
} // namespace

/* A guitar also has a little below the low E, from the body's thump, and above
   those harmonics, from the pick. They're too quiet to change how hard anything is
   driven (0.002 dB), but without them the EQ's 50 Hz and 12.8k bands would have
   nothing in them to read, and any distortion would look like a huge rise there. */
const std::vector<float> &openChord() {
  static const auto chord = [] {
    std::vector<float> out((size_t)chordSamples, 0.0f);
    for (const double hz : {82.41, 123.47, 164.81, 207.65, 246.94, 329.63})
      for (int k = 1; k <= 20; ++k)
        addPartial(out, hz * k, 1.0 / std::pow((double)k, 1.1), 0.8 + 0.5 * k, 0.7 * k * hz);

    int index = 0;
    for (const double hz : {40.0, 45.0, 50.0, 55.0, 60.0, 65.0})
      addPartial(out, hz, 0.03, 2.0, 0.7 * index++);
    index = 0;
    for (double hz = 7000.0; hz < 17000.0; hz *= 1.1)
      addPartial(out, hz, 0.015 * std::pow(7000.0 / hz, 1.5), 6.0, 0.7 * index++);

    float highest = 0.0f;
    for (const auto x : out)
      highest = std::max(highest, std::abs(x));
    for (auto &x : out)
      x /= highest;
    return out;
  }();
  return chord;
}

const std::vector<float> &referenceGuitar() {
  static const auto guitar = [] {
    auto out = openChord();
    for (auto &x : out)
      x *= everydayPeak;
    return out;
  }();
  return guitar;
}

std::vector<float> kWeighted(std::vector<float> signal) {
  // BS.1770-4's two stages, whose coefficients it gives for 48 kHz: a shelf of
  // about +4 dB from 1.5 kHz up, for the head, then a high-pass at about 38 Hz.
  static_assert((int)rate == 48000);
  filter(signal, 1.53512485958697, -2.69169618940638, 1.19839281085285,
         -1.69065929318241, 0.73248077421585);
  filter(signal, 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621);
  return signal;
}

const std::vector<float> &weightedReferenceGuitar() {
  static const auto weighted = kWeighted(referenceGuitar());
  return weighted;
}

double levelDb(const std::vector<float> &signal) {
  double sum = 0.0;
  for (const auto x : signal)
    sum += (double)x * x;
  return 10.0 * std::log10(sum / (double)std::max<size_t>(1, signal.size()) + 1.0e-20);
}

double gainThroughDb(const std::vector<float> &signal,
                     const std::vector<float> &impulseResponse) {
  if (signal.empty() || impulseResponse.empty())
    return 0.0;
  // By Parseval, the energy of a convolution is its spectra's product summed, as
  // long as the transform is long enough that the circular one is the linear one.
  const auto needed = signal.size() + impulseResponse.size() - 1;
  int order = 1;
  while (((size_t)1 << order) < needed)
    ++order;
  const auto size = (size_t)1 << order;
  juce::dsp::FFT fft(order);
  const auto spectrum = [&](const std::vector<float> &x) {
    std::vector<float> bins(2 * size, 0.0f);
    std::copy(x.begin(), x.end(), bins.begin());
    fft.performRealOnlyForwardTransform(bins.data(), true);
    return bins;
  };
  const auto s = spectrum(signal), h = spectrum(impulseResponse);

  double through = 0.0, direct = 0.0;
  for (size_t k = 0; k <= size / 2; ++k) {
    // Every bin but DC and Nyquist stands for its negative frequency too.
    const double both = k == 0 || k == size / 2 ? 1.0 : 2.0;
    const double power = (double)s[2 * k] * s[2 * k] + (double)s[2 * k + 1] * s[2 * k + 1];
    const double response =
        (double)h[2 * k] * h[2 * k] + (double)h[2 * k + 1] * h[2 * k + 1];
    direct += both * power;
    through += both * power * response;
  }
  if (!(direct > 0.0) || !std::isfinite(through))
    return 0.0;
  return 10.0 * std::log10(through / direct + 1.0e-20);
}

std::vector<float> atRate(const float *impulseResponse, int length, double sampleRate) {
  if (length <= 0)
    return {};
  if (juce::approximatelyEqual(sampleRate, rate))
    return {impulseResponse, impulseResponse + length};

  // As juce::dsp::Convolution resamples an IR, so this reads what it plays.
  const double factor = sampleRate / rate;
  juce::AudioBuffer<float> original(1, length);
  original.copyFrom(0, 0, impulseResponse, length);
  juce::MemoryAudioSource memory(original, false);
  juce::ResamplingAudioSource resampling(&memory, false, 1);
  const auto size = juce::roundToInt(std::max(1.0, length / factor));
  resampling.setResamplingRatio(factor);
  resampling.prepareToPlay(size, sampleRate);
  juce::AudioBuffer<float> result(1, size);
  resampling.getNextAudioBlock({&result, 0, size});
  // And scales it, when it isn't normalising, to keep its frequency response.
  result.applyGain((float)factor);
  return {result.getReadPointer(0), result.getReadPointer(0) + size};
}
} // namespace evil::loudness
