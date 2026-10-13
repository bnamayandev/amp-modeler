#include "Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evil {
void Metronome::prepare(double newSampleRate) noexcept {
  sampleRate = newSampleRate;
  clickLength = (int)(0.04 * sampleRate);
  clickDecay = (float)std::exp(-1.0 / (0.007 * sampleRate));
  reset();
}

void Metronome::reset() noexcept {
  beatPosition = 0.0;
  beatInBar = -1;
  clickSample = clickLength; // no click in flight
  wasRunning = false;
  beatInBarForGui.store(-1);
}

void Metronome::process(float *data, int n, const Settings &s) noexcept {
  const bool restart = restartRequested.exchange(false);
  if (s.running && (!wasRunning || restart)) {
    beatPosition = 1.0; // click on the first sample
    beatInBar = -1;
  }
  if (!s.running && wasRunning)
    beatInBarForGui.store(-1);
  wasRunning = s.running;

  const double increment =
      std::clamp(s.bpm, minBpm, maxBpm) / (60.0 * sampleRate);
  const int beats = std::max(1, s.beatsPerBar);
  const float level = std::pow(10.0f, s.levelDb / 20.0f);

  for (int i = 0; i < n; ++i) {
    if (s.running && beatPosition >= 1.0) {
      beatPosition -= 1.0;
      beatInBar = (beatInBar + 1) % beats;
      const bool downbeat = beatInBar == 0;
      clickSample = 0;
      clickPhase = 0.0;
      clickIncrement =
          2.0 * std::numbers::pi * (downbeat ? 1760.0 : 1320.0) / sampleRate;
      clickEnvelope = level * (downbeat ? 1.0f : 0.7f);
      beatInBarForGui.store(beatInBar);
      clickCount.fetch_add(1);
    }
    if (s.running)
      beatPosition += increment;

    if (clickSample < clickLength) { // a click in flight finishes even if stopped
      data[i] += clickEnvelope * (float)std::sin(clickPhase);
      clickPhase += clickIncrement;
      clickEnvelope *= clickDecay;
      ++clickSample;
    }
  }
}
} // namespace evil
