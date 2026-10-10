#include "StreamingResampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>

namespace evil {
namespace {
constexpr double zeroCrossings = 32.0; // per side, at the cutoff frequency
constexpr double kaiserBeta = 9.0;     // ~90 dB stopband
constexpr double rolloff = 0.92; // cutoff as a fraction of the lower Nyquist
constexpr int phasesPerSample = 512;

double besselI0(double x) {
  double sum = 1.0, term = 1.0;
  for (int k = 1; k < 64; ++k) {
    term *= (x / (2.0 * k)) * (x / (2.0 * k));
    sum += term;
    if (term < sum * 1.0e-16)
      break;
  }
  return sum;
}
} // namespace

void StreamingResampler::prepare(double inputRate, double outputRate,
                                 int maxPushSize) {
  const auto in = std::max<std::int64_t>(1, std::llround(inputRate));
  const auto out = std::max<std::int64_t>(1, std::llround(outputRate));
  const auto g = std::gcd(in, out);
  stepNum = in / g;
  stepDen = out / g;

  // Cutoff in cycles per input sample.
  const double cutoff = 0.5 * rolloff * std::min(1.0, (double)out / (double)in);
  const double halfWidth = zeroCrossings / (2.0 * cutoff);
  halfTaps = (int)std::ceil(halfWidth);
  tablePhases = phasesPerSample;

  table.assign((size_t)halfTaps * (size_t)tablePhases + 2, 0.0f);
  const double i0Beta = besselI0(kaiserBeta);
  for (size_t k = 0; k < table.size(); ++k) {
    const double t = (double)k / tablePhases;
    if (t >= halfWidth)
      break;
    const double x = 2.0 * cutoff * t;
    const double sinc =
        k == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
    const double u = t / halfWidth;
    const double window =
        besselI0(kaiserBeta * std::sqrt(1.0 - u * u)) / i0Beta;
    table[k] = (float)(2.0 * cutoff * sinc * window);
  }

  buffer.assign((size_t)(maxPushSize + 4 * halfTaps + 16), 0.0f);
  reset();
}

void StreamingResampler::reset() noexcept {
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  // Silence before the first real sample, so the first output can be computed
  // as soon as enough look-ahead has arrived.
  bufferStart = -halfTaps;
  bufferLength = halfTaps;
  phaseBase = 0;
  phaseNum = 0;
}

int StreamingResampler::maxOutputsFor(int numInputs) const noexcept {
  return (int)((std::int64_t)numInputs * stepDen / stepNum) + 2;
}

float StreamingResampler::kernelAt(double distance) const noexcept {
  const double pos = std::abs(distance) * tablePhases;
  const auto index = (size_t)pos;
  if (index + 1 >= table.size())
    return 0.0f;
  const auto frac = (float)(pos - (double)index);
  return table[index] + frac * (table[index + 1] - table[index]);
}

void StreamingResampler::compact() noexcept {
  // Oldest sample the next output still needs.
  const auto keepFrom = phaseBase - halfTaps + 1;
  const auto drop =
      (int)std::clamp<std::int64_t>(keepFrom - bufferStart, 0, bufferLength);
  if (drop > 0) {
    std::memmove(buffer.data(), buffer.data() + drop,
                 sizeof(float) * (size_t)(bufferLength - drop));
    bufferStart += drop;
    bufferLength -= drop;
  }
}

void StreamingResampler::push(const float *input, int numSamples) noexcept {
  compact();
  const int space = (int)buffer.size() - bufferLength;
  assert(numSamples <= space && "push() called with more samples than "
                                "prepare() allowed, or pop() not drained");
  numSamples = std::min(numSamples, space);
  std::memcpy(buffer.data() + bufferLength, input,
              sizeof(float) * (size_t)numSamples);
  bufferLength += numSamples;
}

int StreamingResampler::pop(float *output, int maxOutputs) noexcept {
  const auto newest = bufferStart + bufferLength - 1;
  const int taps = 2 * halfTaps;
  int produced = 0;

  while (produced < maxOutputs && phaseBase + halfTaps <= newest) {
    const double frac = (double)phaseNum / (double)stepDen;
    const float *x = buffer.data() + (phaseBase - halfTaps + 1 - bufferStart);

    // Tap m reads input index (phaseBase - halfTaps + 1 + m), at distance
    // (phaseBase + frac) - index = frac + halfTaps - 1 - m from the output
    // time.
    float acc = 0.0f;
    for (int m = 0; m < taps; ++m)
      acc += x[m] * kernelAt(frac + (double)(halfTaps - 1 - m));
    output[produced++] = acc;

    phaseNum += stepNum;
    phaseBase += phaseNum / stepDen;
    phaseNum %= stepDen;
  }
  return produced;
}
} // namespace evil
