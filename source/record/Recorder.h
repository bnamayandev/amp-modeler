#pragma once

#include "Mp4Writer.h"
#include "dsp/SampleTap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <optional>

namespace evil {
/** Where the system keeps the user's downloads. It may not exist yet. */
juce::File getDownloadsFolder();

/** Records what the audio thread hands it to an .mp4 file.

    The audio thread only copies samples into a queue, without blocking or
    allocating; a background thread encodes and writes them. While a take is under
    way its file is "<name>.mp4.part", so a file named .mp4 is always a finished
    one that plays. It lands there when the take ends.

    The take stops if the file can't be written, or by stop() or the destructor,
    which all finish the file. Then takeFinished() says how it went. */
class Recorder final : private juce::Thread {
public:
  struct Take {
    juce::File file;           // where it was saved
    juce::String error;        // set if it wasn't
    double seconds = 0.0;
    double droppedSeconds = 0.0; // lost because the disk couldn't keep up
  };

  Recorder();
  ~Recorder() override;

  /** Starts a take in this folder, creating the folder if need be. Returns
      false and says why if it can't. */
  bool start(const juce::File &folder, double sampleRate, juce::String &error);

  /** Ends the take and finishes its file, waiting until that's done. */
  void stop();

  /** A take's rate can't change, so a new device rate ends it. */
  void sampleRateChanged(double newRate);

  bool isRecording() const noexcept { return recording.load(); }
  double getSeconds() const noexcept;

  /** The take that ended since this was last called, if one did. */
  std::optional<Take> takeFinished();

  /** Audio thread. Does nothing unless recording. */
  void push(const float *samples, int numSamples) noexcept;

private:
  void run() override;
  Take finishTake(bool wroteEverything);

  // Five seconds at 48 kHz: longer than a disk should ever stall. On the heap, since
  // a processor may not be.
  using Queue = SampleTap<(1 << 18)>;
  std::unique_ptr<Queue> queue;

  std::atomic<bool> recording{false};
  std::atomic<std::int64_t> samplesQueued{0}, samplesDropped{0};
  std::atomic<double> takeRate{0.0};

  std::unique_ptr<Mp4Writer> writer;
  juce::File partFile, takeFolder;
  juce::String takeName;

  // Neither is ever taken by the audio thread. stop() holds the first while it
  // waits for the background thread, which needs the second to say how it went.
  juce::CriticalSection controlLock, finishedLock;
  std::optional<Take> finished;

  JUCE_DECLARE_NON_COPYABLE(Recorder)
};
} // namespace evil
