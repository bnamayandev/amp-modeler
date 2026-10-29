#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <algorithm>

namespace evil {
/** Lock-free single-producer, single-consumer sample queue from the audio
   thread to the GUI, e.g. for an analyser. Never blocks or allocates; when the
   reader falls behind (or no editor is open), new samples are dropped. */
template <int capacity> class SampleTap {
public:
  /** Audio thread. */
  void push(const float *data, int n) noexcept {
    const auto w = fifo.write(n);
    std::copy_n(data, w.blockSize1, buffer.data() + w.startIndex1);
    std::copy_n(data + w.blockSize1, w.blockSize2,
                buffer.data() + w.startIndex2);
  }

  /** Reader thread. Returns how many samples were copied into dest. */
  int pull(float *dest, int maxSamples) noexcept {
    const auto r = fifo.read(maxSamples);
    std::copy_n(buffer.data() + r.startIndex1, r.blockSize1, dest);
    std::copy_n(buffer.data() + r.startIndex2, r.blockSize2,
                dest + r.blockSize1);
    return r.blockSize1 + r.blockSize2;
  }

private:
  juce::AbstractFifo fifo{capacity};
  std::array<float, (size_t)capacity> buffer{};
};
} // namespace evil
