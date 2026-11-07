#include "Recorder.h"

#include <vector>

namespace evil {
juce::File getDownloadsFolder() {
  const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

#if JUCE_LINUX || JUCE_BSD
  // It goes by the user's language or their choice: "$HOME/Téléchargements".
  const auto config = juce::SystemStats::getEnvironmentVariable(
      "XDG_CONFIG_HOME", home.getChildFile(".config").getFullPathName());
  juce::StringArray lines;
  juce::File(config).getChildFile("user-dirs.dirs").readLines(lines);
  for (const auto &line : lines)
    if (line.trimStart().startsWith("XDG_DOWNLOAD_DIR=")) {
      const auto path = line.fromFirstOccurrenceOf("=", false, false)
                            .trim()
                            .unquoted()
                            .replace("$HOME", home.getFullPathName());
      if (juce::File::isAbsolutePath(path))
        return juce::File(path);
    }
#elif JUCE_WINDOWS
  // Where Explorer has it, if it's been moved to another drive.
  const auto moved = juce::WindowsRegistry::getValue(
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
      "Shell Folders\\{374DE290-123F-4565-9164-39C4925E467B}");
  if (juce::File::isAbsolutePath(moved))
    return juce::File(moved);
#endif

  return home.getChildFile("Downloads");
}

Recorder::Recorder() : juce::Thread("Recorder"), queue(std::make_unique<Queue>()) {}

Recorder::~Recorder() { stop(); }

bool Recorder::start(const juce::File &folder, double sampleRate, juce::String &error) {
  const juce::ScopedLock sl(controlLock);
  if (recording.load())
    return true;
  stopThread(-1); // a take that ended by itself has finished, but not been joined

  if (sampleRate <= 0.0) {
    error = "The audio device isn't running, so there's nothing to record.";
    return false;
  }
  if (const auto made = folder.createDirectory(); made.failed()) {
    error = "Couldn't use " + folder.getFullPathName() + ": " + made.getErrorMessage();
    return false;
  }

  takeFolder = folder;
  takeName = "Evil Amplifier " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
  partFile = folder.getNonexistentChildFile(takeName, ".mp4.part", true);
  writer = Mp4Writer::create(partFile, sampleRate, error);
  if (writer == nullptr) {
    partFile.deleteFile();
    return false;
  }

  // A push under way as the last take stopped can have landed after its final read.
  std::vector<float> stale(8192);
  while (queue->pull(stale.data(), (int)stale.size()) > 0) {
  }

  samplesQueued = 0;
  samplesDropped = 0;
  takeRate = sampleRate;
  if (!startThread()) {
    writer.reset();
    partFile.deleteFile();
    error = "Couldn't start the recording thread.";
    return false;
  }
  recording.store(true);
  return true;
}

void Recorder::stop() {
  const juce::ScopedLock sl(controlLock);
  recording.store(false);
  stopThread(-1); // it reads what's queued and finishes the file first
}

void Recorder::sampleRateChanged(double newRate) {
  if (isRecording() && !juce::approximatelyEqual(newRate, takeRate.load()))
    stop();
}

double Recorder::getSeconds() const noexcept {
  const auto rate = takeRate.load();
  return rate > 0.0 ? (double)samplesQueued.load(std::memory_order_relaxed) / rate : 0.0;
}

std::optional<Recorder::Take> Recorder::takeFinished() {
  const juce::ScopedLock sl(finishedLock);
  auto take = std::move(finished);
  finished.reset();
  return take;
}

void Recorder::push(const float *samples, int numSamples) noexcept {
  if (!recording.load(std::memory_order_acquire))
    return;
  const int queued = queue->push(samples, numSamples);
  samplesQueued.fetch_add(queued, std::memory_order_relaxed);
  if (queued < numSamples)
    samplesDropped.fetch_add(numSamples - queued, std::memory_order_relaxed);
}

void Recorder::run() {
  std::vector<float> block(8192);
  bool wroteEverything = true;
  for (;;) {
    const int n = queue->pull(block.data(), (int)block.size());
    if (n > 0) {
      wroteEverything = writer->write(block.data(), n);
      if (!wroteEverything)
        break;
    } else if (threadShouldExit()) {
      break;
    } else {
      wait(50);
    }
  }

  recording.store(false); // if the disk filled up, nothing else has said so
  auto take = finishTake(wroteEverything);
  const juce::ScopedLock sl(finishedLock);
  finished = std::move(take);
}

Recorder::Take Recorder::finishTake(bool wroteEverything) {
  Take take;
  take.droppedSeconds = (double)samplesDropped.load() / takeRate.load();

  const bool ok = wroteEverything && writer->finish();
  const auto numSamples = writer->getNumSamples();
  const auto fileRate = writer->getFileSampleRate();
  const auto writeError = writer->getError();
  writer.reset(); // closes the file

  if (!ok)
    take.error = writeError + "\nThe recording couldn't be finished, so it wasn't saved.";
  else if (numSamples == 0)
    take.error = "No audio reached the recorder, so nothing was saved.";
  if (take.error.isNotEmpty()) {
    partFile.deleteFile();
    return take;
  }

  take.seconds = (double)numSamples / fileRate;
  const auto destination = takeFolder.getNonexistentChildFile(takeName, ".mp4", true);
  if (partFile.moveFileTo(destination)) {
    take.file = destination;
  } else {
    // It's finished, and only misnamed: leave it for the user to rename.
    take.error = "The recording is in " + partFile.getFullPathName() +
                 ", but couldn't be renamed to end in .mp4.";
  }
  return take;
}
} // namespace evil
