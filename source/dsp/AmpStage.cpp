#include "AmpStage.h"

#include <algorithm>
#include <cstring>

namespace evil {
void AmpStage::prepare(double sampleRate, int maxBlockSize) {
  overdrive.prepare(sampleRate, maxBlockSize);
  models.prepare(sampleRate, maxBlockSize);
  fadeBuffer.assign((size_t)maxBlockSize, 0.0f);
  fadeLength = std::max(1, (int)(0.02 * sampleRate));
  reset();
}

void AmpStage::reset() noexcept {
  overdrive.reset();
  fadePosition = fadeLength;
  previousMode = currentMode;
  primed = false;
}

int AmpStage::computeLatencySamples(const Settings &settings) const noexcept {
  switch (settings.mode) {
  case AmpMode::overdrive:
    return overdrive.getLatencySamples(settings.oversampling);
  case AmpMode::neural:
    return models.getActiveLatencySamples();
  case AmpMode::off:
    break;
  }
  return 0;
}

void AmpStage::render(AmpMode mode, float *data, int n,
                      const Settings &s) noexcept {
  switch (mode) {
  case AmpMode::overdrive:
    overdrive.process(data, n, s.driveDb, s.tone, s.oversampling);
    break;
  case AmpMode::neural:
    models.process(data, data, n, s.normalise);
    break;
  case AmpMode::off:
    break;
  }
}

void AmpStage::process(float *data, int n, const Settings &settings) noexcept {
  if (!primed) {
    currentMode = previousMode = settings.mode;
    primed = true;
  }
  if (settings.mode != currentMode) {
    previousMode = currentMode;
    currentMode = settings.mode;
    fadePosition = 0;
  }

  if (fadePosition < fadeLength) {
    std::memcpy(fadeBuffer.data(), data, sizeof(float) * (size_t)n);
    render(previousMode, fadeBuffer.data(), n, settings);
  }

  render(currentMode, data, n, settings);

  if (fadePosition < fadeLength) {
    for (int i = 0; i < n; ++i) {
      const float t =
          std::min(1.0f, (float)(fadePosition + i + 1) / (float)fadeLength);
      data[i] = fadeBuffer[(size_t)i] + t * (data[i] - fadeBuffer[(size_t)i]);
    }
    fadePosition += n;
  }

  latency.store(computeLatencySamples(settings), std::memory_order_relaxed);
}
} // namespace evil
