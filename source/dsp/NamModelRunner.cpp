#include "NamModelRunner.h"

#include <NAM/dsp.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace evil {
NamModelRunner::NamModelRunner(std::unique_ptr<nam::DSP> m, ModelInfo i)
    : model(std::move(m)), info(std::move(i)) {
  jassert(model != nullptr);
  const auto expected = model->GetExpectedSampleRate();
  // NAM's convention: a model that doesn't state its rate was trained at 48
  // kHz.
  modelRate = expected > 0.0 ? expected : 48000.0;
  info.sampleRate = modelRate;

  if (model->HasLoudness()) {
    info.hasLoudness = true;
    info.loudnessDb = model->GetLoudness();
  }
}

NamModelRunner::~NamModelRunner() = default;

void NamModelRunner::prepare(double hostSampleRate, int maxHostBlockSize) {
  jassert(hostSampleRate > 0.0 && maxHostBlockSize > 0);
  preparedHostRate = hostSampleRate;
  preparedBlockSize = maxHostBlockSize;
  inputCopy.assign((size_t)maxHostBlockSize, 0.0f);

  resampling = std::abs(hostSampleRate - modelRate) > 0.5 &&
               !model->SupportsArbitrarySampleRate();

  if (!resampling) {
    latency = 0;
    model->Reset(hostSampleRate, maxHostBlockSize); // prewarms
    modelIn.clear();
    modelOut.clear();
    pending.clear();
    pendingCount = 0;
    return;
  }

  toModelRate.prepare(hostSampleRate, modelRate, maxHostBlockSize);
  maxModelBlock = toModelRate.maxOutputsFor(maxHostBlockSize);
  toHostRate.prepare(modelRate, hostSampleRate, maxModelBlock);
  modelIn.assign((size_t)maxModelBlock, 0.0f);
  modelOut.assign((size_t)maxModelBlock, 0.0f);
  model->Reset(modelRate, maxModelBlock);

  // After T host samples in, the chain can always produce at least T - D
  // samples out, with D = 1 + L1 + (1 + L2) * host/model (L1, L2 = kernel
  // look-ahead of each stage, in that stage's input samples). Holding back
  // `latency` >= D samples of silence at the start therefore guarantees a full
  // block out for every block in.
  const double hostPerModel = hostSampleRate / modelRate;
  latency = (int)std::ceil(1.0 + toModelRate.getLookaheadInputSamples() +
                           (1.0 + toHostRate.getLookaheadInputSamples()) *
                               hostPerModel) +
            1;

  pending.assign((size_t)(maxHostBlockSize + latency +
                          toHostRate.maxOutputsFor(maxModelBlock) + 16),
                 0.0f);
  pendingCount = latency; // already zero
}

bool NamModelRunner::isPreparedFor(double hostSampleRate,
                                   int maxHostBlockSize) const noexcept {
  return juce::exactlyEqual(preparedHostRate, hostSampleRate) &&
         preparedBlockSize == maxHostBlockSize;
}

void NamModelRunner::runModel(float *in, float *out, int n) noexcept {
  float *inputs[] = {in};
  float *outputs[] = {out};
  model->process(inputs, outputs, n);
}

void NamModelRunner::process(const float *in, float *out, int n) noexcept {
  jassert(n <= preparedBlockSize);
  n = std::min(n, preparedBlockSize);
  if (n <= 0)
    return;

  std::memcpy(inputCopy.data(), in, sizeof(float) * (size_t)n);

  if (!resampling) {
    runModel(inputCopy.data(), out, n);
    return;
  }

  toModelRate.push(inputCopy.data(), n);
  for (;;) {
    const int m = toModelRate.pop(modelIn.data(), maxModelBlock);
    if (m == 0)
      break;
    runModel(modelIn.data(), modelOut.data(), m);
    toHostRate.push(modelOut.data(), m);
    pendingCount += toHostRate.pop(pending.data() + pendingCount,
                                   (int)pending.size() - pendingCount);
  }

  const int available = std::min(n, pendingCount);
  jassert(available == n); // guaranteed by the latency computed in prepare()
  std::memcpy(out, pending.data(), sizeof(float) * (size_t)available);
  std::fill(out + available, out + n, 0.0f);
  pendingCount -= available;
  std::memmove(pending.data(), pending.data() + available,
               sizeof(float) * (size_t)pendingCount);
}
} // namespace evil
