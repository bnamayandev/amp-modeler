#pragma once

#include "dsp/SignalChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evil::params
{
// Parameter IDs are part of saved state and host automation: never rename them.
inline constexpr const char* input = "input";
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";
inline constexpr const char* ampMode = "amp_mode";
inline constexpr const char* drive = "drive";
inline constexpr const char* tone = "tone";
inline constexpr const char* oversampling = "oversampling";
inline constexpr const char* normalise = "normalise";
inline constexpr const char* cabOn = "cab_on";
inline constexpr const char* bass = "bass";
inline constexpr const char* mid = "mid";
inline constexpr const char* treble = "treble";
inline constexpr const char* output = "output";

// Non-parameter state saved alongside the parameters.
inline constexpr const char* modelSourceProperty = "modelSource";
inline constexpr const char* irSourceProperty = "irSource";
inline constexpr const char* defaultModelSource = "builtin:BlackstarHT1";
inline constexpr const char* defaultIrSource = "builtin:EvilCab4x12";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Cached raw parameter pointers, read lock-free on the audio thread. */
class Snapshot
{
public:
    explicit Snapshot (juce::AudioProcessorValueTreeState& state);
    SignalChain::Settings read() const noexcept;

private:
    std::atomic<float>* input;
    std::atomic<float>* gateOn;
    std::atomic<float>* gateThreshold;
    std::atomic<float>* ampMode;
    std::atomic<float>* drive;
    std::atomic<float>* tone;
    std::atomic<float>* oversampling;
    std::atomic<float>* normalise;
    std::atomic<float>* cabOn;
    std::atomic<float>* bass;
    std::atomic<float>* mid;
    std::atomic<float>* treble;
    std::atomic<float>* output;
};
} // namespace evil::params
