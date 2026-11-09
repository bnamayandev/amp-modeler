#include "PitchShifter.h"

#include <cmath>
#include <cstring>

namespace evil {
namespace {
// A peak this good is taken as soon as it's seen: nothing further away could be
// enough better to be worth the extra delay.
constexpr float certainMatch = 0.97f;
// Going down, the tap waits for a longer lag to come within reach unless one
// this good is already in it. A low-E power chord scores ~0.75-0.8 at half its
// period, where splicing it would warble.
constexpr float acceptableMatch = 0.9f;
// Peaks within this fraction of the best are as good as each other.
constexpr float nearBest = 0.9f;
} // namespace

void PitchShifter::prepare(double sampleRate) {
  const auto samples = [&](double seconds) {
    return std::max(1, (int)(seconds * sampleRate));
  };
  shortestLag = samples(0.0025);
  longestLag = samples(0.025); // the notes of a low-E power chord line up at 24 ms
  longestSplice = samples(0.008);
  correlationLength = samples(0.01);
  retryStep = samples(0.001);
  // Comparing at about 12 kHz finds the peak, for the same cost at any rate.
  stride = std::max(1, (int)std::lround(sampleRate / 12000.0));

  // Going up, the tap has up to a shortest lag left to run when it jumps.
  maxDelay = minDelay + (float)(longestLag + shortestLag + 2 * stride + 8);
  // The search compares segments reaching a correlation length past the window.
  line.setMaxDelay((int)std::ceil(maxDelay) + correlationLength + 2 * stride + 8);

  const auto points = (size_t)(correlationLength / stride + 1);
  const auto lags = (size_t)((longestLag - shortestLag) / stride + 8);
  tapSegment.assign(points, 0.0f);
  candidates.assign(points + lags, 0.0f);
  energy.assign(points + lags + 1, 0.0);
  scores.assign(lags, 0.0f);

  fadeLength = std::max(1, (int)(0.01 * sampleRate));
  spliceLength = longestSplice;
  reset();
}

void PitchShifter::reset() noexcept {
  line.clear();
  splicePosition = spliceLength;
  started = false;
  fadePosition = 0;
  primed = false;
}

float PitchShifter::similarity(int from, int candidate, int step) const noexcept {
  double cross = 0.0, candidateEnergy = 1.0e-12;
  for (int m = 0; m < correlationLength; m += step) {
    const double b = line.at(candidate + m);
    cross += (double)line.at(from + m) * b;
    candidateEnergy += b * b;
  }
  return (float)(cross / std::sqrt(candidateEnergy));
}

float PitchShifter::refine(int from, int coarse, int lowest,
                           int highest) const noexcept {
  // To the sample, then between samples: the tap is fractional anyway, and a
  // rounded jump is a small pitch error at every splice.
  const int step = std::max(1, stride / 2);
  int peak = coarse;
  float peakScore = -1.0e30f;
  for (int c = std::max(lowest, coarse - stride);
       c <= std::min(highest, coarse + stride); ++c)
    if (const float s = similarity(from, c, step); s > peakScore) {
      peakScore = s;
      peak = c;
    }
  const float before = similarity(from, peak - 1, step);
  const float after = similarity(from, peak + 1, step);
  const float curve = before - 2.0f * peakScore + after;
  if (curve >= -1.0e-9f)
    return (float)peak;
  return (float)peak + std::clamp(0.5f * (before - after) / curve, -0.5f, 0.5f);
}

std::optional<float> PitchShifter::findSplice(float from, int direction,
                                              int nearLag, int farLag,
                                              bool mustSplice) noexcept {
  const int tap = (int)from;
  const float fraction = from - (float)tap;
  const int points = correlationLength / stride;
  const int count = (farLag - nearLag) / stride + 1;
  const auto delayOf = [&](int lag) {
    return tap + direction * (nearLag + lag * stride);
  };

  float tapEnergy = 0.0f;
  for (int k = 0; k < points; ++k) {
    const float v = line.at(tap + k * stride);
    tapSegment[(size_t)k] = v;
    tapEnergy += v * v;
  }
  if (tapEnergy < 1.0e-9f) // silence: nothing to line up, so stay close
    return (float)delayOf(0) + fraction;

  // Candidates laid out in rising delay, whichever way the tap is jumping.
  const int lowest = direction > 0 ? delayOf(0) : delayOf(count - 1);
  const int total = points + count - 1;
  energy[0] = 0.0;
  for (int k = 0; k < total; ++k) {
    const float v = line.at(lowest + k * stride);
    candidates[(size_t)k] = v;
    energy[(size_t)k + 1] = energy[(size_t)k] + (double)v * v;
  }

  // Normalised correlation at each lag, nearest first.
  float best = 0.0f;
  int chosen = -1;
  for (int lag = 0; lag < count && chosen < 0; ++lag) {
    const int index = direction > 0 ? lag : count - 1 - lag;
    const float *candidate = candidates.data() + index;
    float cross = 0.0f;
    for (int k = 0; k < points; ++k)
      cross += tapSegment[(size_t)k] * candidate[k];
    const double e = energy[(size_t)(index + points)] - energy[(size_t)index];
    const float score =
        cross / std::sqrt(tapEnergy * (float)std::max(e, 1.0e-12));
    scores[(size_t)lag] = score;
    best = std::max(best, score);

    if (lag >= 2) {
      const float previous = scores[(size_t)lag - 1];
      if (previous >= certainMatch && previous >= score &&
          previous >= scores[(size_t)lag - 2])
        chosen = lag - 1;
    }
  }

  if (chosen < 0) {
    const bool aligned = best >= acceptableMatch;
    if (!aligned && !mustSplice)
      return std::nullopt;
    // The nearest good peak, for the least delay. Going down with nothing that
    // lines up, the furthest: it lands closest to the input and the poor
    // splice comes round least often. Going up, a long jump would replay more
    // (an attack twice, say) and take long to drift back, so nearest anyway.
    const auto isPeak = [&](int lag) {
      const float s = scores[(size_t)lag];
      return s >= nearBest * best &&
             (lag == 0 || s >= scores[(size_t)lag - 1]) &&
             (lag + 1 == count || s >= scores[(size_t)lag + 1]);
    };
    chosen = 0;
    if (aligned || direction > 0) {
      for (int lag = 0; lag < count; ++lag)
        if (isPeak(lag)) {
          chosen = lag;
          break;
        }
    } else {
      for (int lag = count - 1; lag >= 0; --lag)
        if (isPeak(lag)) {
          chosen = lag;
          break;
        }
    }
  }

  const int lowestDelay = direction > 0 ? tap + nearLag : tap - farLag;
  const int highestDelay = direction > 0 ? tap + farLag : tap - nearLag;
  return refine(tap, delayOf(chosen), lowestDelay, highestDelay) + fraction;
}

void PitchShifter::startSplice(float newDelay, int length) noexcept {
  fadingDelay = delay;
  delay = std::clamp(newDelay, minDelay, maxDelay);
  spliceLength = length;
  splicePosition = 0;
}

void PitchShifter::shift(float *data, int n, float ratio) noexcept {
  // The tap's delay changes by (1 - ratio) per sample: shrinking delay reads
  // faster than real time (pitch up), growing delay reads slower (pitch down).
  const float drift = 1.0f - ratio;
  const float travel = std::abs(drift);
  // Long fades hide a splice best, but it has to be over by the time the tap
  // has drifted a whole shortest lag, or the next one couldn't start in time.
  const int fade = travel * (float)longestSplice <= (float)shortestLag
                       ? longestSplice
                       : std::max(1, (int)((float)shortestLag / travel));
  const float margin = travel * (float)fade + 1.0f;
  if (!started) { // start close to the input; the first search finds the lag
    delay = minDelay + 1.0f;
    searchAt = (float)(shortestLag + 2 * stride);
    started = true;
  }

  for (int i = 0; i < n; ++i) {
    line.push(data[i]);

    if (splicePosition >= spliceLength) {
      const float space = delay - minDelay;
      if (drift < 0.0f && space <= margin) {
        const int nearLag = std::max(shortestLag, (int)std::ceil(margin));
        if (const auto target = findSplice(delay, 1, nearLag, longestLag, true))
          startSplice(*target, fade);
      } else if (drift > 0.0f && space >= searchAt) {
        // Search once the tap could repeat its last jump (plus enough to see
        // the peak's far side), which lets the delay settle at that lag. If
        // nothing lines up yet, wait for the longer lags to come within reach.
        const int farLag = std::min((int)space - 1, longestLag);
        const auto target =
            findSplice(delay, -1, std::min(shortestLag, farLag), farLag,
                       space >= (float)longestLag);
        if (target) {
          searchAt = std::max((float)shortestLag, delay - *target) +
                     (float)(2 * stride);
          startSplice(*target, fade);
        } else {
          searchAt = space + (float)retryStep;
        }
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
