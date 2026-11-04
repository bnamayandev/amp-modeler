#pragma once

#include "Biquad.h"

#include <array>
#include <cstddef>
#include <vector>

namespace evil {
/** Finds the pitch of a single note with YIN (de Cheveigne & Kawahara, 2002),
   for the tuner. Not for the audio thread: an analysis takes a millisecond or
   so, so the GUI pulls samples from a SampleTap and does the work there.

    Covers 38 to 1400 Hz, from a baritone's lowest string to the top frets, at
   any sample rate: content above the pitches it looks for is filtered out and
   the rest analysed at 20-40 kHz. */
class PitchDetector {
public:
  static constexpr double minHz = 38.0, maxHz = 1400.0;

  void prepare(double sampleRate);
  void reset() noexcept;
  double getSampleRate() const noexcept { return rate; }

  /** Ignored until prepare() has been called. */
  void push(const float *data, int n) noexcept;

  /** The pitch of the latest samples in Hz, or 0 if they don't have one: too
      quiet or not periodic enough, or not enough samples yet. */
  double detect();

  /** detect(), steadied for a display. A median rides out the odd wrong frame,
      and a note that has died away is held briefly before this returns 0. */
  double track(double secondsSinceLast);

private:
  double rate = 0.0, analysisRate = 0.0;
  int decimation = 1, untilNextSample = 1;
  std::array<Biquad, 2> lowPass;

  std::size_t windowSize = 0, minPeriod = 0, maxPeriod = 0;
  std::vector<float> history; // ring buffer of decimated samples
  std::size_t writeIndex = 0, filled = 0;
  std::vector<float> frame, difference; // detect()'s working space

  std::array<double, 5> recent{};
  std::size_t recentNext = 0, recentCount = 0;
  double steady = 0.0, silentFor = 0.0;
};
} // namespace evil
