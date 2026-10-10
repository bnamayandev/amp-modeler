#include "CabSim.h"

#include "EvilBinaryData.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace evil {
namespace {
struct BuiltinIr {
  const char *id;
  const char *resource;
  const char *displayName;
};

constexpr BuiltinIr builtinIrs[] = {
    {"EvilCab4x12", "EvilCab4x12_wav", "Evil 4x12 (built-in)"},
};
} // namespace

juce::StringArray CabSim::getBuiltinNames() {
  juce::StringArray names;
  for (const auto &ir : builtinIrs)
    names.add(ir.id);
  return names;
}

void CabSim::prepare(double sampleRate, int maxBlockSize) {
  convolution.prepare({sampleRate, (juce::uint32)maxBlockSize, 1});
  dry.assign((size_t)maxBlockSize, 0.0f);
  rampPerSample = (float)(1.0 / (0.02 * sampleRate));
  primed = false;
}

void CabSim::reset() noexcept { convolution.reset(); }

void CabSim::process(float *data, int n, bool enabled) noexcept {
  jassert(n <= (int)dry.size());
  const float target = enabled ? 1.0f : 0.0f;
  if (!primed) {
    wetAmount = target;
    wasEnabled = enabled;
    primed = true;
  }

  if (!enabled && juce::exactlyEqual(wetAmount, 0.0f))
    return; // fully bypassed: skip the convolution entirely

  if (enabled && !wasEnabled && juce::exactlyEqual(wetAmount, 0.0f))
    convolution
        .reset(); // don't replay the tail from before it was switched off
  wasEnabled = enabled;

  std::memcpy(dry.data(), data, sizeof(float) * (size_t)n);
  float *channels[] = {data};
  juce::dsp::AudioBlock<float> block(channels, 1, (size_t)n);
  convolution.process(juce::dsp::ProcessContextReplacing<float>(block));

  if (juce::exactlyEqual(wetAmount, 1.0f) && enabled)
    return;

  for (int i = 0; i < n; ++i) {
    wetAmount += std::clamp(target - wetAmount, -rampPerSample, rampPerSample);
    data[i] = dry[(size_t)i] + wetAmount * (data[i] - dry[(size_t)i]);
  }
}

bool CabSim::load(const juce::String &newSource, juce::String &error) {
  std::lock_guard lock(loadMutex);

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader;
  juce::String name;

  if (newSource.startsWith(builtinPrefix)) {
    const auto id =
        newSource.fromFirstOccurrenceOf(builtinPrefix, false, false);
    const auto *entry =
        std::find_if(std::begin(builtinIrs), std::end(builtinIrs),
                     [&](const BuiltinIr &ir) { return id == ir.id; });
    int size = 0;
    const char *data =
        entry != std::end(builtinIrs)
            ? EvilBinaryData::getNamedResource(entry->resource, size)
            : nullptr;
    if (data == nullptr) {
      error = "Unknown built-in IR: " + id;
      return false;
    }
    reader.reset(formats.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(data, (size_t)size, false)));
    name = entry->displayName;
  } else {
    const juce::File file(newSource);
    if (!file.existsAsFile()) {
      error = "IR file not found: " + newSource;
      return false;
    }
    reader.reset(formats.createReaderFor(file));
    name = file.getFileNameWithoutExtension();
  }

  if (reader == nullptr) {
    error = "Not a readable audio file: " + newSource;
    return false;
  }
  if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) {
    error = "The IR file is empty";
    return false;
  }

  // Cab IRs are tens to hundreds of milliseconds; anything longer is truncated.
  const auto length = (int)std::min<juce::int64>(
      reader->lengthInSamples,
      (juce::int64)(maxIrSeconds * reader->sampleRate));
  juce::AudioBuffer<float> ir(1, length);
  if (!reader->read(&ir, 0, length, 0, true, false)) // left/mono channel
  {
    error = "Could not read the IR file";
    return false;
  }

  const auto *samples = ir.getReadPointer(0);
  double energy = 0.0;
  for (int i = 0; i < length; ++i)
    energy += (double)samples[i] * samples[i];
  if (!std::isfinite(energy) || energy < 1.0e-12) {
    error = "The IR is silent";
    return false;
  }
  ir.applyGain((float)(1.0 / std::sqrt(energy)));

  convolution.loadImpulseResponse(
      std::move(ir), reader->sampleRate, juce::dsp::Convolution::Stereo::no,
      juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::no);
  source = newSource;
  displayName = name;
  return true;
}

juce::String CabSim::getSource() const {
  std::lock_guard lock(loadMutex);
  return source;
}

juce::String CabSim::getDisplayName() const {
  std::lock_guard lock(loadMutex);
  return displayName;
}
} // namespace evil
