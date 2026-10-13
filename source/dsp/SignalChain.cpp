#include "SignalChain.h"

#include <algorithm>

namespace evil {
void SignalChain::prepare(double sampleRate, int maxBlockSize) {
  maxBlock = maxBlockSize;
  inputGain.reset(sampleRate, 0.02);
  outputGain.reset(sampleRate, 0.02);
  gate.prepare(sampleRate);
  transposer.prepare(sampleRate);
  compressor.prepare(sampleRate);
  fuzz.prepare(sampleRate);
  overdrive.prepare(sampleRate);
  amp.prepare(sampleRate, maxBlockSize);
  cab.prepare(sampleRate, maxBlockSize);
  eq.prepare(sampleRate);
  chorus.prepare(sampleRate);
  delay.prepare(sampleRate);
  reverb.prepare(sampleRate);
  for (auto &f : footswitches)
    f.prepare(sampleRate);
  scratch.assign((size_t)maxBlockSize, 0.0f);
  primed = false;
}

void SignalChain::reset() noexcept {
  gate.reset();
  transposer.reset();
  compressor.reset();
  fuzz.reset();
  overdrive.reset();
  amp.reset();
  cab.reset();
  eq.reset();
  chorus.reset();
  delay.reset();
  reverb.reset();
  for (auto &f : footswitches)
    f.reset();
}

void SignalChain::process(float *data, int n,
                          const Settings &settings) noexcept {
  jassert(maxBlock > 0); // prepare() first
  for (int start = 0; start < n; start += maxBlock)
    processChunk(data + start, std::min(maxBlock, n - start), settings);
}

void SignalChain::processChunk(float *data, int n, const Settings &s) noexcept {
  const auto in = juce::Decibels::decibelsToGain(s.inputDb);
  const auto out = juce::Decibels::decibelsToGain(s.outputDb);
  if (!primed) {
    inputGain.setCurrentAndTargetValue(in);
    outputGain.setCurrentAndTargetValue(out);
    primed = true;
  }
  inputGain.setTargetValue(in);
  outputGain.setTargetValue(out);

  inputGain.applyGain(data, n);
  gate.process(data, n, s.gateThresholdDb, s.gateOn);
  auto *dry = scratch.data();
  transposer.process(data, n, s.transposeSemitones, dry);
  footswitches[0].process(compressor, data, n, s.compressor, dry);
  footswitches[1].process(fuzz, data, n, s.fuzz, dry);
  footswitches[2].process(overdrive, data, n, s.overdrive, dry);
  amp.process(data, n, s.amp);
  cab.process(data, n, s.cabOn);
  eq.process(data, n, s.eqDb);
  footswitches[3].process(chorus, data, n, s.chorus, dry);
  footswitches[4].process(delay, data, n, s.delay, dry);
  footswitches[5].process(reverb, data, n, s.reverb, dry);
  outputGain.applyGain(data, n);
}
} // namespace evil
