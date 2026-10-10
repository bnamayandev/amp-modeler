#pragma once

#include "Biquad.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace evil
{
/** Gray-box overdrive: pre-emphasis -> drive -> asymmetric tanh -> DC block -> tone filter.

    Only the waveshaper creates new harmonics, so only it runs oversampled (none, 2x or 4x,
    using JUCE's low-latency polyphase IIR half-band filters). All three oversamplers are
    built in prepare(), so switching factor at runtime never allocates; the switch is
    crossfaded over a few milliseconds. */
class Overdrive
{
public:
    static constexpr int numOversamplingChoices = 3; // 1x, 2x, 4x

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** driveDb 0..40, tone 0..1 (dark..bright), oversamplingIndex 0..2. */
    void process (float* data, int n, float driveDb, float tone, int oversamplingIndex) noexcept;

    int getLatencySamples (int oversamplingIndex) const noexcept;

private:
    void shape (float* data, int n, int oversamplingIndex) noexcept;

    double sampleRate = 48000.0;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, numOversamplingChoices> oversamplers;

    Biquad preHighPass, preEmphasis, dcBlocker, toneLowPass, fizzLowPass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain;
    juce::SmoothedValue<float> toneSmoothed;

    int currentOversampling = 2, previousOversampling = 2;
    int switchFadePosition = 0, switchFadeLength = 256;
    std::vector<float> preShape, switchBuffer, makeupGain;
    bool primed = false; // snap to the first block's settings after reset()
};
} // namespace evil
