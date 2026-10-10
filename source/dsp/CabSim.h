#pragma once

#include <juce_dsp/juce_dsp.h>

#include <mutex>
#include <vector>

namespace evil {
/** Cabinet simulation: zero-latency partitioned convolution with an impulse
   response.

    IRs are read and checked on the calling (non-audio) thread, normalised to
   unit energy so every IR plays at a similar level, then handed to
   juce::dsp::Convolution, which swaps them in on the audio thread with its own
   crossfade - so loading an IR mid-performance doesn't click. Turning the cab
   on or off is crossfaded too. */
class CabSim {
public:
  static constexpr const char *builtinPrefix = "builtin:";
  static constexpr double maxIrSeconds = 2.0;

  void prepare(double sampleRate, int maxBlockSize);
  void reset() noexcept;
  void process(float *data, int n, bool enabled) noexcept;

  /** Non-audio thread. source is a WAV/AIFF/FLAC path or "builtin:<name>". */
  bool load(const juce::String &source, juce::String &error);

  juce::String getSource() const;
  juce::String getDisplayName() const;

  /** Length of the IR the engine is actually using. IRs arrive asynchronously
     after load(), so this lags behind it. Call from the audio (processing)
     thread. */
  int getActiveIrLength() const { return convolution.getCurrentIRSize(); }

  /** The convolution engine runs at zero latency. */
  static int getLatencySamples() noexcept { return 0; }

  static juce::StringArray getBuiltinNames();

private:
  juce::dsp::Convolution
      convolution; // default: zero latency, uniform partitions
  std::vector<float> dry;
  float wetAmount = 1.0f;
  float rampPerSample = 0.001f;
  bool wasEnabled = true;
  bool primed = false; // snap to the first block's on/off state after prepare()

  mutable std::mutex
      loadMutex; // serialises load() callers; never taken by the audio thread
  juce::String source, displayName;
};
} // namespace evil
