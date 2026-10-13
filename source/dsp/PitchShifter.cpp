#include "PitchShifter.h"

#include <cmath>
#include <cstring>

namespace evil {
void PitchShifter::prepare(double sampleRate) {
  maxDelay = minDelay + (float)(0.032 * sampleRate);
  spliceLength = std::max(1, (int)(0.008 * sampleRate));
  searchLength = (int)(0.0125 * sampleRate); // one period of low E (82 Hz)
  correlationLength = (int)(0.01 * sampleRate);
  // The search compares segments reaching a correlation length past the window.
  line.setMaxDelay((int)std::ceil(maxDelay) + correlationLength + 8);
  fadeLength = std::max(1, (int)(0.01 * sampleRate));
  reset();
}

void PitchShifter::reset() noexcept {
  line.clear();
  splicePosition = spliceLength;
  started = false;
  fadePosition = 0;
  primed = false;
}

float PitchShifter::findSplice(float from, float lo, float hi) const noexcept {
  // Best lag in [lo, hi] for continuing the waveform at `from`: maximise the
  // correlation of the segment behind each, normalised by the candidate's
  // energy. A coarse pass at every other sample, then a fine pass around it.
  const int reference = (int)from;
  const float fraction = from - (float)reference;
  const auto score = [&](int candidate, int step) {
    double cross = 0.0, energy = 1.0e-12;
    for (int m = 0; m < correlationLength; m += step) {
      const double b = line.at(candidate + m);
      cross += (double)line.at(reference + m) * b;
      energy += b * b;
    }
    return cross / std::sqrt(energy);
  };

  const int first = (int)std::ceil(lo), last = (int)std::floor(hi);
  int best = first;
  double bestScore = -1.0e300;
  for (int c = first; c <= last; c += 2)
    if (const double s = score(c, 2); s > bestScore) {
      bestScore = s;
      best = c;
    }
  const int coarse = best;
  bestScore = -1.0e300;
  for (int c = std::max(first, coarse - 2); c <= std::min(last, coarse + 2); ++c)
    if (const double s = score(c, 1); s > bestScore) {
      bestScore = s;
      best = c;
    }
  return std::clamp((float)best + fraction, minDelay, maxDelay);
}

void PitchShifter::shift(float *data, int n, float ratio) noexcept {
  // The tap's delay changes by (1 - ratio) per sample: shrinking delay reads
  // faster than real time (pitch up), growing delay reads slower (pitch down).
  const float drift = 1.0f - ratio;
  const float margin = std::abs(drift) * (float)spliceLength + 2.0f;
  if (!started) { // start where the tap has the whole window to travel
    delay = drift < 0.0f ? maxDelay - 1.0f : minDelay + 1.0f;
    started = true;
  }

  for (int i = 0; i < n; ++i) {
    line.push(data[i]);

    if (splicePosition >= spliceLength) {
      const float search = (float)searchLength;
      if (drift < 0.0f && delay - minDelay <= margin) {
        fadingDelay = delay;
        delay = findSplice(delay, maxDelay - search, maxDelay);
        splicePosition = 0;
      } else if (drift > 0.0f && maxDelay - delay <= margin) {
        fadingDelay = delay;
        delay = findSplice(delay, minDelay, minDelay + search);
        splicePosition = 0;
      }
    }

    float y = line.hermite(delay);
    if (splicePosition < spliceLength) {
      const float g = (float)(splicePosition + 1) / (float)spliceLength;
      y = g * y + (1.0f - g) * line.hermite(fadingDelay);
      fadingDelay = std::clamp(fadingDelay + drift, minDelay, maxDelay);
      ++splicePosition;
    }
    delay = std::clamp(delay + drift, minDelay, maxDelay);
    data[i] = y;
  }
}

void PitchShifter::process(float *data, int n, int semitones,
                           float *scratch) noexcept {
  semitones = std::clamp(semitones, -maxSemitones, maxSemitones);
  const bool on = semitones != 0;
  if (!primed) {
    fadePosition = on ? fadeLength : 0;
    primed = true;
  }
  if (!on && fadePosition == 0) {
    // Keep the line filled so engaging starts from real audio, not silence.
    for (int i = 0; i < n; ++i)
      line.push(data[i]);
    started = false;
    return;
  }
  // Leaving: keep the last ratio while the shifted signal fades out.
  const float ratio = on ? std::exp2((float)semitones / 12.0f) : lastRatio;
  lastRatio = ratio;

  if (on && fadePosition == fadeLength) {
    shift(data, n, ratio);
    return;
  }
  std::memcpy(scratch, data, sizeof(float) * (size_t)n);
  shift(data, n, ratio);
  const int step = on ? 1 : -1;
  for (int i = 0; i < n; ++i) {
    fadePosition = std::clamp(fadePosition + step, 0, fadeLength);
    const float t = (float)fadePosition / (float)fadeLength;
    data[i] = scratch[i] + t * (data[i] - scratch[i]);
  }
}
} // namespace evil
