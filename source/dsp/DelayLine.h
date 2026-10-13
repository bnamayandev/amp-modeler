#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

namespace evil {
/** Power-of-two circular delay line. Sized once in setMaxDelay(); reads and
   writes never allocate. Delays count pushes back from the latest sample
   (delay 0 is the sample just pushed). */
class DelayLine {
public:
  void setMaxDelay(int maxDelaySamples) {
    buffer.assign((size_t)juce::nextPowerOfTwo(std::max(8, maxDelaySamples + 4)),
                  0.0f);
    mask = (int)buffer.size() - 1;
    write = 0;
  }

  void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); }

  void push(float x) noexcept {
    write = (write + 1) & mask;
    buffer[(size_t)write] = x;
  }

  float at(int delay) const noexcept {
    return buffer[(size_t)((write - delay) & mask)];
  }

  float linear(float delay) const noexcept {
    const int i = (int)delay;
    const float f = delay - (float)i;
    const float a = at(i);
    return a + f * (at(i + 1) - a);
  }

  /** 4-point cubic Hermite interpolation. Needs delay >= 1. */
  float hermite(float delay) const noexcept {
    const int i = (int)delay;
    const float f = delay - (float)i;
    const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
  }

private:
  std::vector<float> buffer = std::vector<float>(8, 0.0f);
  int mask = 7, write = 0;
};

/** Fades a delay line's input in after it was cleared, so the first audio
   doesn't arrive at the read tap as a step from silence (a click). */
class OnsetRamp {
public:
  void prepare(double sampleRate) noexcept {
    length = std::max(1, (int)(0.01 * sampleRate));
    position = 0;
  }
  void restart() noexcept { position = 0; }
  float process(float x) noexcept {
    if (position >= length)
      return x;
    return x * (float)++position / (float)length;
  }

private:
  int length = 480, position = 0;
};
} // namespace evil
