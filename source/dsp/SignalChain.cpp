#include "SignalChain.h"

#include <algorithm>

namespace evil
{
void SignalChain::prepare (double sampleRate, int maxBlockSize)
{
    maxBlock = maxBlockSize;
    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    gate.prepare (sampleRate);
    amp.prepare (sampleRate, maxBlockSize);
    cab.prepare (sampleRate, maxBlockSize);
    eq.prepare (sampleRate);
    primed = false;
}

void SignalChain::reset() noexcept
{
    gate.reset();
    amp.reset();
    cab.reset();
    eq.reset();
}

void SignalChain::process (float* data, int n, const Settings& settings) noexcept
{
    jassert (maxBlock > 0); // prepare() first
    for (int start = 0; start < n; start += maxBlock)
        processChunk (data + start, std::min (maxBlock, n - start), settings);
}

void SignalChain::processChunk (float* data, int n, const Settings& s) noexcept
{
    const auto in = juce::Decibels::decibelsToGain (s.inputDb);
    const auto out = juce::Decibels::decibelsToGain (s.outputDb);
    if (! primed)
    {
        inputGain.setCurrentAndTargetValue (in);
        outputGain.setCurrentAndTargetValue (out);
        primed = true;
    }
    inputGain.setTargetValue (in);
    outputGain.setTargetValue (out);

    inputGain.applyGain (data, n);
    gate.process (data, n, s.gateThresholdDb, s.gateOn);
    amp.process (data, n, s.amp);
    cab.process (data, n, s.cabOn);
    eq.process (data, n, s.bassDb, s.midDb, s.trebleDb);
    outputGain.applyGain (data, n);
}
} // namespace evil
