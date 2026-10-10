#pragma once

#include <algorithm>
#include <cmath>

namespace evil {
/** Guitar noise gate with hysteresis and hold.

    A fast RMS detector opens the gate when the level rises above the threshold
   and lets it close only after the level has stayed below (threshold -
   hysteresis) for the hold time, so sustained notes decaying around the
   threshold don't chatter. Gain changes are smoothed: ~0.5 ms to open (keeps
   pick attack), ~100 ms to close. */
class NoiseGate {
public:
  static constexpr float hysteresisDb = 6.0f;
  static constexpr float holdSeconds = 0.05f;

  void prepare(double sampleRate) noexcept {
    const auto coeff = [sampleRate](double seconds) {
      return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
    };
    detectorCoeff = coeff(0.002);
    attackCoeff = coeff(0.0005);
    releaseCoeff = coeff(0.1);
    holdSamples = static_cast<int>(holdSeconds * sampleRate);
    reset();
  }

  void reset() noexcept {
    meanSquare = 0.0f;
    gain = 1.0f; // start open so the first notes are never swallowed
    open = true;
    holdCounter = holdSamples;
  }

  /** Processes in place. When disabled the gate keeps tracking the input so
     re-enabling it doesn't click, but its gain smoothly returns to unity. */
  void process(float *data, int numSamples, float thresholdDb,
               bool enabled) noexcept {
    const float openMs = dbToPower(thresholdDb);
    const float closeMs = dbToPower(thresholdDb - hysteresisDb);

    for (int i = 0; i < numSamples; ++i) {
      const float x = data[i];
      meanSquare += detectorCoeff * (x * x - meanSquare);

      if (meanSquare >= openMs) {
        open = true;
        holdCounter = holdSamples;
      } else if (open && meanSquare < closeMs) {
        if (holdCounter > 0)
          --holdCounter;
        else
          open = false;
      }

      const float target = (open || !enabled) ? 1.0f : 0.0f;
      gain += (target > gain ? attackCoeff : releaseCoeff) * (target - gain);
      data[i] = x * gain;
    }

    // Keep the smoothed gain out of the denormal range once the gate is shut.
    if (gain < 1.0e-6f)
      gain = 0.0f;
  }

  bool isOpen() const noexcept { return open; }
  float getGain() const noexcept { return gain; }

private:
  static float dbToPower(float db) noexcept {
    return std::pow(10.0f, db / 10.0f);
  }

  float detectorCoeff = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
  int holdSamples = 0, holdCounter = 0;
  float meanSquare = 0.0f, gain = 1.0f;
  bool open = true;
};
} // namespace evil
