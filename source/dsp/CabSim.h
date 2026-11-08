#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <mutex>
#include <vector>

namespace evil {
/** Cabinet simulation: zero-latency partitioned convolution with an impulse
   response.

    IRs are read and checked on the calling (non-audio) thread, normalised to
   unit energy so every IR plays at a similar level, then handed to
   juce::dsp::Convolution, which swaps them in on the audio thread with its own
   crossfade - so loading an IR mid-performance doesn't click. Turning the cab
   on or off is crossfaded too.

    With levels matched, each IR is also scaled so that it changes the tone of
   the model playing into it and not its level, which unit energy alone doesn't
   do: a cabinet with a big low end can be several dB louder on a clean amp than
   on a fizzy one. Changing the model rescales it the same way. */
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

  /** Non-audio thread: whether to scale IRs to keep the level as it is. */
  void setMatchingLevels(bool shouldMatch);
  /** Non-audio thread: what's going into the cabinet, to keep the level of. The
     model's reference output, K-weighted, or null for the bare guitar. */
  void setLevelReference(std::shared_ptr<const std::vector<float>> weightedSignal);
  /** The gain the current IR was loaded with on top of unit energy, in dB. */
  float getMatchGainDb() const;

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

  /** Hands the convolution the IR, scaled to match levels if it should be.
     Holding loadMutex. */
  void install(bool evenIfUnchanged);

  mutable std::mutex
      loadMutex; // serialises load() callers; never taken by the audio thread
  juce::String source, displayName;
  // Guarded by loadMutex.
  juce::AudioBuffer<float> unitIr; // as read, at unit energy
  double irRate = 0.0;
  std::vector<float> irAtLevelRate; // as loudness:: measures it
  std::shared_ptr<const std::vector<float>> levelReference;
  bool matching = false;
  float matchGainDb = 0.0f;
};
} // namespace evil
