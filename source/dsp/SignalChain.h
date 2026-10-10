#pragma once

#include "AmpStage.h"
#include "CabSim.h"
#include "Equaliser.h"
#include "NoiseGate.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace evil
{
/** Everything the plugin does to the guitar signal, independent of the plugin wrapper so
    tests and the CLI run exactly the same code:

        input gain -> noise gate -> amp (overdrive | neural) -> cab IR -> EQ -> output level

    Mono. process() splits blocks larger than the prepared maximum, so a host that sends
    more samples than it announced can't overrun any internal buffer. */
class SignalChain
{
public:
    struct Settings
    {
        float inputDb = 0.0f;
        bool gateOn = true;
        float gateThresholdDb = -65.0f;
        AmpStage::Settings amp;
        bool cabOn = true;
        float bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f;
        float outputDb = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (float* data, int n, const Settings& settings) noexcept;

    /** Total latency as last processed. */
    int getLatencySamples() const noexcept { return amp.getLatencySamples() + CabSim::getLatencySamples(); }
    int computeLatencySamples (const Settings& s) const noexcept { return amp.computeLatencySamples (s.amp) + CabSim::getLatencySamples(); }

    ModelManager& getModels() noexcept { return amp.getModels(); }
    CabSim& getCab() noexcept { return cab; }
    bool isGateOpen() const noexcept { return gate.isOpen(); }

private:
    void processChunk (float* data, int n, const Settings& settings) noexcept;

    int maxBlock = 0;
    bool primed = false; // snap gains to the first block's settings after prepare()
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;
    NoiseGate gate;
    AmpStage amp;
    CabSim cab;
    Equaliser eq;
};
} // namespace evil
