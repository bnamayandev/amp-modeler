#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace evil {
namespace {
constexpr double minAnalysisRate = 20000.0, cutoffHz = 5000.0;
constexpr double windowSeconds = 0.085; // over three cycles of the lowest note
constexpr double minLevel = 0.001;      // -60 dBFS RMS: below this, it's hum
constexpr float maxAperiodicity = 0.3f; // of the difference function's scale
constexpr double holdSeconds = 0.6;
} // namespace

void PitchDetector::prepare(double sampleRate) {
  rate = sampleRate;
  decimation = std::max(1, (int)(sampleRate / minAnalysisRate));
  analysisRate = sampleRate / decimation;

  // Fourth-order Butterworth: pick noise and the highest partials only blur the
  // dip the analysis looks for.
  lowPass[0].setCoeffs(BiquadCoeffs::lowPass(sampleRate, cutoffHz, 0.5412));
  lowPass[1].setCoeffs(BiquadCoeffs::lowPass(sampleRate, cutoffHz, 1.3066));

  windowSize = (std::size_t)std::lround(windowSeconds * analysisRate);
  minPeriod = (std::size_t)(analysisRate / maxHz);
  maxPeriod = (std::size_t)std::ceil(analysisRate / minHz);
  history.assign(windowSize + maxPeriod + 2, 0.0f);
  frame.assign(history.size(), 0.0f);
  difference.assign(maxPeriod + 2, 1.0f);
  reset();
}

void PitchDetector::reset() noexcept {
  for (auto &f : lowPass)
    f.reset();
  untilNextSample = decimation;
  writeIndex = filled = 0;
  recentNext = recentCount = 0;
  steady = silentFor = 0.0;
}

void PitchDetector::push(const float *data, int n) noexcept {
  if (history.empty())
    return;
  for (int i = 0; i < n; ++i) {
    const float x = std::isfinite(data[i]) ? data[i] : 0.0f;
    const float filtered =
        lowPass[1].processSample(lowPass[0].processSample(x));
    if (--untilNextSample > 0)
      continue;
    untilNextSample = decimation;
    history[writeIndex] = filtered;
    writeIndex = (writeIndex + 1) % history.size();
    filled = std::min(filled + 1, history.size());
  }
}

double PitchDetector::detect() {
  if (history.empty() || filled < history.size())
    return 0.0;

  // Oldest sample first.
  const auto split = (std::ptrdiff_t)writeIndex;
  std::copy(history.begin() + split, history.end(), frame.begin());
  std::copy(history.begin(), history.begin() + split, frame.end() - split);

  double energy = 0.0;
  for (std::size_t i = 0; i < windowSize; ++i)
    energy += (double)frame[i] * frame[i];
  if (energy < minLevel * minLevel * (double)windowSize)
    return 0.0;

  // The cumulative mean normalised difference: about 1 in general, and near 0
  // at lags where the signal repeats itself.
  double running = 0.0;
  for (std::size_t lag = 1; lag <= maxPeriod + 1; ++lag) {
    const float *a = frame.data(), *b = frame.data() + lag;
    float sum = 0.0f;
    for (std::size_t i = 0; i < windowSize; ++i) {
      const float d = a[i] - b[i];
      sum += d * d;
    }
    running += (double)sum;
    difference[lag] =
        running > 0.0 ? (float)((double)sum * (double)lag / running) : 1.0f;
  }

  // The deepest dip says whether there's a pitch at all...
  float deepest = 1.0f;
  for (std::size_t lag = minPeriod; lag <= maxPeriod; ++lag)
    deepest = std::min(deepest, difference[lag]);
  if (deepest > maxAperiodicity)
    return 0.0;

  // ...and the first dip nearly as deep is its period. Dips at multiples of the
  // period can be a touch deeper, which would read an octave low.
  const float threshold = std::max(0.1f, deepest + 0.05f);
  auto lag = minPeriod;
  while (lag < maxPeriod && difference[lag] > threshold)
    ++lag;
  while (lag < maxPeriod && difference[lag + 1] < difference[lag])
    ++lag;

  // A parabola through the dip gets the fraction of a sample...
  const auto dipAt = [this](std::size_t dip) {
    const double before = difference[dip - 1], here = difference[dip],
                 after = difference[dip + 1];
    const double curve = before - 2.0 * here + after;
    return (double)dip + (curve > 0.0 ? 0.5 * (before - after) / curve : 0.0);
  };
  double period = dipAt(lag);

  // ...and the same dip some periods along shares that error between them. High
  // notes need it: E6 is only 16 samples long, where a tenth of a sample is 10
  // cents.
  const int periods = std::min(8, (int)((double)maxPeriod / period));
  if (periods > 1) {
    auto far = (std::size_t)std::lround(periods * period);
    while (far > minPeriod && difference[far - 1] < difference[far])
      --far;
    while (far < maxPeriod && difference[far + 1] < difference[far])
      ++far;
    if (difference[far] <= threshold)
      period = dipAt(far) / periods;
  }
  return analysisRate / period;
}

double PitchDetector::track(double secondsSinceLast) {
  const double hz = detect();
  if (hz > 0.0) {
    recent[recentNext] = hz;
    recentNext = (recentNext + 1) % recent.size();
    recentCount = std::min(recentCount + 1, recent.size());
    silentFor = 0.0;

    auto sorted = recent;
    const auto end = sorted.begin() + (std::ptrdiff_t)recentCount;
    const auto middle = sorted.begin() + (std::ptrdiff_t)(recentCount - 1) / 2;
    std::nth_element(sorted.begin(), middle, end);
    steady = *middle;
  } else if (steady > 0.0) {
    silentFor += secondsSinceLast;
    if (silentFor > holdSeconds) {
      steady = 0.0;
      recentCount = recentNext = 0;
    }
  }
  return steady;
}
} // namespace evil
