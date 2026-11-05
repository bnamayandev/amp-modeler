#include "ToneMeter.h"

#include <juce_dsp/juce_dsp.h>

#include <numbers>

namespace evil::agent {
namespace {
constexpr double rate = 48000.0;
constexpr int blockSize = 256;
constexpr int chordSamples = (int)(1.2 * rate), noteSamples = (int)(0.4 * rate);
constexpr int settleSamples = (int)(0.1 * rate);
constexpr double noteHz = 220.0;
// A guitar's chord peaks at about this at +12 dBu full scale. The hard chord and
// note are 12 dB above the gentle ones.
constexpr float gentlePeak = 0.05f, hardPeak = 0.2f, gentleNote = 0.0125f, hardNote = 0.05f;

/** An open chord, every string a decaying stack of harmonics, its top end falling
    away faster: not a real guitar, but with its balance. The same every time. */
std::vector<float> chord(float peak) {
  std::vector<float> out((size_t)chordSamples, 0.0f);
  for (const double hz : {82.41, 123.47, 164.81, 207.65, 246.94, 329.63}) {
    for (int k = 1; k <= 20 && hz * k < 12000.0; ++k) {
      const double partial = hz * k, amplitude = 1.0 / std::pow((double)k, 1.1);
      const double decay = 0.8 + 0.5 * k, phase = 0.7 * k * hz;
      for (int i = 0; i < chordSamples; ++i) {
        const double t = i / rate;
        out[(size_t)i] += (float)(amplitude * std::exp(-decay * t) *
                                  std::sin(2.0 * std::numbers::pi * partial * t + phase));
      }
    }
  }
  float highest = 0.0f;
  for (const auto x : out)
    highest = juce::jmax(highest, std::abs(x));
  for (auto &x : out)
    x *= peak / highest;
  return out;
}

float rmsDb(const float *data, int n) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i)
    sum += (double)data[i] * data[i];
  return (float)(10.0 * std::log10(sum / juce::jmax(1, n) + 1.0e-12));
}

/** Harmonic distortion of a steady tone at noteHz, in percent. */
float distortionPercent(const float *data, int n) {
  const auto magnitude = [&](double hz) {
    double re = 0.0, im = 0.0, window = 0.0;
    for (int i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n);
      const double angle = 2.0 * std::numbers::pi * hz * i / rate;
      re += w * data[i] * std::cos(angle);
      im += w * data[i] * std::sin(angle);
      window += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / window;
  };
  const double fundamental = magnitude(noteHz);
  double harmonics = 0.0;
  for (int k = 2; k <= 10; ++k)
    harmonics += std::pow(magnitude(noteHz * k), 2.0);
  return (float)(100.0 * std::sqrt(harmonics) / juce::jmax(fundamental, 1.0e-9));
}

struct Result {
  std::array<float, Equaliser::numBands> bands{};
  float level = 0.0f, touch = 0.0f, gentle = 0.0f, hard = 0.0f;
  juce::String error;
};

Result play(const SignalChain::Settings &settings, const juce::String &modelSource,
            const juce::String &irSource) {
  Result result;
  auto chain = std::make_unique<SignalChain>();
  if (irSource.isNotEmpty() && !chain->getCab().load(irSource, result.error))
    return result;
  if (modelSource.isNotEmpty() &&
      !chain->getModels().loadSynchronously(modelSource, result.error))
    return result;
  result.error = {};
  chain->prepare(rate, blockSize);

  const auto run = [&](std::vector<float> samples) {
    samples.insert(samples.begin(), (size_t)settleSamples, 0.0f);
    for (size_t start = 0; start < samples.size(); start += blockSize)
      chain->process(samples.data() + start,
                     (int)juce::jmin((size_t)blockSize, samples.size() - start), settings);
    samples.erase(samples.begin(), samples.begin() + settleSamples);
    return samples;
  };
  const auto steady = [](float amplitude) {
    std::vector<float> note((size_t)noteSamples);
    for (int i = 0; i < noteSamples; ++i)
      note[(size_t)i] = amplitude * (float)std::sin(2.0 * std::numbers::pi * noteHz * i / rate);
    return note;
  };

  const auto gentleChord = run(chord(gentlePeak));
  const auto hardChord = run(chord(hardPeak));
  const auto gentleTone = run(steady(gentleNote));
  const auto hardTone = run(steady(hardNote));

  // The opening of a chord, while it's ringing.
  const int ringing = (int)(0.5 * rate);
  result.level = rmsDb(hardChord.data(), ringing);
  result.touch = result.level - rmsDb(gentleChord.data(), ringing);
  // Past any pick-up transient, on a settled tone.
  const int skip = noteSamples / 4;
  result.gentle = distortionPercent(gentleTone.data() + skip, noteSamples - skip);
  result.hard = distortionPercent(hardTone.data() + skip, noteSamples - skip);

  constexpr int order = 14, size = 1 << order;
  juce::dsp::FFT fft(order);
  juce::dsp::WindowingFunction<float> window((size_t)size,
                                             juce::dsp::WindowingFunction<float>::hann);
  std::vector<float> buffer(2 * (size_t)size, 0.0f);
  std::copy_n(hardChord.begin(), size, buffer.begin());
  window.multiplyWithWindowingTable(buffer.data(), (size_t)size);
  fft.performFrequencyOnlyForwardTransform(buffer.data());
  for (size_t b = 0; b < result.bands.size(); ++b) {
    const double centre = Equaliser::frequencies[b];
    const int low = (int)(centre / std::sqrt(2.0) * size / rate);
    const int high = (int)(centre * std::sqrt(2.0) * size / rate);
    double energy = 1.0e-12;
    for (int i = juce::jmax(1, low); i <= juce::jmin(high, size / 2); ++i)
      energy += (double)buffer[(size_t)i] * buffer[(size_t)i];
    result.bands[b] = (float)(10.0 * std::log10(energy));
  }
  return result;
}
} // namespace

ToneReading measureTone(SignalChain::Settings settings, const juce::String &modelSource,
                        const juce::String &irSource) {
  settings.phaser.on = settings.flanger.on = settings.chorus.on = settings.tremolo.on =
      settings.delay.on = settings.reverb.on = false;
  settings.transposeSemitones = 0;

  auto alone = settings;
  alone.wah.on = alone.compressor.on = alone.fuzz.on = alone.overdrive.on =
      alone.distortion.on = alone.boost.on = false;
  alone.eqDb = {};
  alone.inputDb = 0.0f;
  alone.outputDb = 0.0f;

  ToneReading reading;
  const auto asSet = play(settings, modelSource, irSource);
  const auto bare = play(alone, modelSource, irSource);
  reading.error = asSet.error.isNotEmpty() ? asSet.error : bare.error;
  if (reading.error.isNotEmpty())
    return reading;

  // Spectra relative to the capture alone's loudest band, so they read as a shape.
  const float top = *std::max_element(bare.bands.begin(), bare.bands.end());
  for (size_t b = 0; b < reading.bandDb.size(); ++b) {
    reading.bandDb[b] = asSet.bands[b] - top;
    reading.bandDbCaptureAlone[b] = bare.bands[b] - top;
  }
  reading.levelDb = asSet.level;
  reading.levelDbCaptureAlone = bare.level;
  reading.touchDb = asSet.touch;
  reading.touchDbCaptureAlone = bare.touch;
  reading.distortionLightPercent = asSet.gentle;
  reading.distortionHardPercent = asSet.hard;
  reading.distortionLightCaptureAlone = bare.gentle;
  reading.distortionHardCaptureAlone = bare.hard;
  return reading;
}
} // namespace evil::agent
