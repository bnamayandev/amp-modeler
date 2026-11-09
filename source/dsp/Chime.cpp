#include "Chime.h"

#include <cmath>
#include <numbers>

namespace evil {
namespace {
struct Note {
  double start, frequency, amplitude, decaySeconds;
};
// A rising fifth, E5 then B5: low enough not to pierce, and the second note a
// little louder and longer so it reads as finished.
constexpr Note notes[] = {{0.0, 659.26, 0.8, 0.16}, {0.14, 987.77, 1.0, 0.25}};
constexpr double attackSeconds = 0.006, fadeSeconds = 0.2;
// A touch of the octave, dying faster, makes it a bell rather than a test tone.
constexpr double octaveAmount = 0.2, octaveDecaySeconds = 0.06;
} // namespace

void Chime::prepare(double newSampleRate) noexcept {
  sampleRate = newSampleRate;
  length = (int)(lengthSeconds * sampleRate);
  reset();
}

void Chime::reset() noexcept { position = length; }

void Chime::process(float *const *channels, int numChannels, int n) noexcept {
  if (pending.exchange(false))
    position = 0;
  if (position >= length)
    return;

  constexpr double twoPi = 2.0 * std::numbers::pi;
  const double level = std::pow(10.0, levelDb / 20.0);
  const double fadeStart = lengthSeconds - fadeSeconds;
  for (int i = 0; i < n && position < length; ++i, ++position) {
    const double t = position / sampleRate;
    double sample = 0.0;
    for (const auto &note : notes) {
      const double u = t - note.start;
      if (u < 0.0)
        continue;
      const double attack =
          u < attackSeconds ? 0.5 - 0.5 * std::cos(std::numbers::pi * u / attackSeconds)
                            : 1.0;
      const double phase = twoPi * note.frequency * u;
      sample += note.amplitude * attack * std::exp(-u / note.decaySeconds) *
                (std::sin(phase) +
                 octaveAmount * std::exp(-u / octaveDecaySeconds) * std::sin(2.0 * phase));
    }
    if (t > fadeStart) // already far down; this takes it the rest of the way
      sample *= (lengthSeconds - t) / fadeSeconds;
    const auto value = (float)(level * sample);
    for (int ch = 0; ch < numChannels; ++ch)
      channels[ch][i] += value;
  }
}
} // namespace evil
