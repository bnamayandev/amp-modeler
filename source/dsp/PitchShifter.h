#pragma once

#include "DelayLine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <vector>

namespace evil {
/** Transposer: polyphonic delay-line pitch shifter with waveform-aligned
   splices, adding only as much delay as the signal needs.

    A read tap moves through recent input at the speed the pitch ratio needs
   (faster than real time to go up, slower to go down). When it runs out of room
   it jumps and the old and new tap crossfade. The delay heard is how far behind
   the input the tap is, which sweeps between nearly nothing and the length of
   the jump, so jumps are kept as short as a clean splice allows: the shortest
   lag at which the two waveforms line up (the normalised correlation of what's
   behind each). That's about a period of what's being played, so ~2-5 ms of
   average delay up the neck and ~6-8 ms on a low E. Chords only line up at
   longer lags, at worst the 24 ms of a low-E power chord (~13 ms on average).

    At 0 semitones the shifter is skipped (bit-exact pass-through); engaging or
   leaving it crossfades. */
class PitchShifter {
public:
  static constexpr int maxSemitones = 12;

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, int semitones, float *scratch) noexcept;

private:
  void shift(float *data, int n, float ratio) noexcept;
  void startSplice(float newDelay, int length) noexcept;
  /** The delay to jump to from `from`, `direction` being +1 for further behind
      the input and -1 for closer to it, by a lag in [nearLag, farLag]. Empty if
      nothing lines up well enough, unless `mustSplice`. */
  std::optional<float> findSplice(float from, int direction, int nearLag,
                                  int farLag, bool mustSplice) noexcept;
  float refine(int from, int coarse, int lowest, int highest) const noexcept;
  float similarity(int from, int candidate, int step) const noexcept;

  DelayLine line;
  static constexpr float minDelay = 2.0f;
  float maxDelay = 1500.0f;
  int shortestLag = 120, longestLag = 1200, retryStep = 48;
  int longestSplice = 384, correlationLength = 480, stride = 4;

  // Preallocated for the search: what's behind the tap, the candidates' audio,
  // their running energy, and each candidate's score (nearest first).
  std::vector<float> tapSegment, candidates, scores;
  std::vector<double> energy;

  float delay = 0.0f, fadingDelay = 0.0f; // current tap, tap being faded out
  float searchAt = 0.0f;  // going down: how far the tap drifts before a search
  int spliceLength = 384; // of the splice running or last run
  int splicePosition = 384; // >= spliceLength: no splice running
  bool started = false;

  int fadeLength = 480, fadePosition = 0; // engage/leave crossfade
  float lastRatio = 1.0f;
  bool primed = false;
};
} // namespace evil
