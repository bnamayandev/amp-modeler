#pragma once

#include "DelayLine.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace evil {
/** Transposer: polyphonic delay-line pitch shifter with waveform-aligned
   splices.

    A read tap moves through a 32 ms window of recent input at the speed the
   pitch ratio needs (faster than real time to go up, slower to go down). When
   it nears the end of the window it jumps back by about a window, and the old
   and new tap crossfade over 8 ms. The jump length is chosen within a period
   of the lowest guitar note to line the two waveforms up (the lag with the
   best normalised correlation), so splices don't beat or warble the way
   fixed-length grains do. Costs ~16 ms of average delay.

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
  float findSplice(float from, float lo, float hi) const noexcept;

  DelayLine line;
  float minDelay = 4.0f, maxDelay = 1540.0f;
  int spliceLength = 384, searchLength = 600, correlationLength = 480;

  float delay = 0.0f, fadingDelay = 0.0f; // current tap, tap being faded out
  int splicePosition = 0;                 // == spliceLength: no splice running
  bool started = false;

  int fadeLength = 480, fadePosition = 0; // engage/leave crossfade
  float lastRatio = 1.0f;
  bool primed = false;
};
} // namespace evil
