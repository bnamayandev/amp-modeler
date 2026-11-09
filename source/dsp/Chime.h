#pragma once

#include <atomic>

namespace evil {
/** A soft two-note chime mixed into the output: the agent has answered. Well under
    a guitar's level, with a gentle attack so it never clicks, and silent the rest
    of the time. */
class Chime {
public:
  /** The louder note's peak. */
  static constexpr float levelDb = -26.0f;
  /** From the first note to silence. */
  static constexpr double lengthSeconds = 1.14;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /** Any thread: the next block starts the chime (over, if one is sounding). */
  void trigger() noexcept { pending.store(true); }

  /** Adds the chime, while one is sounding, to every channel (which keep their
      content). */
  void process(float *const *channels, int numChannels, int n) noexcept;

private:
  double sampleRate = 48000.0;
  int position = 0, length = 0; // position == length: nothing sounding
  std::atomic<bool> pending{false};
};
} // namespace evil
