#pragma once

#include <cstdint>
#include <vector>

namespace evil
{
/** Streaming band-limited resampler for an arbitrary (rational) rate ratio.

    Each output sample j represents input time j * inRate / outRate exactly and is computed
    with a Kaiser-windowed sinc whose cutoff sits just below the lower of the two Nyquist
    frequencies, so downsampling is anti-aliased and upsampling is anti-imaged. The step is
    tracked as an exact integer fraction, so there is no drift however long it runs.

    Input history before the first pushed sample is silence. An output can be popped once
    the input it needs (up to `getLookaheadInputSamples()` past its time) has been pushed.

    prepare() allocates; push(), pop() and reset() do not. Single-threaded. */
class StreamingResampler
{
public:
    /** Rates are rounded to whole Hz to form the exact ratio. maxPushSize is the largest
        number of samples that will be pushed between two pops that drain the output. */
    void prepare (double inputRate, double outputRate, int maxPushSize);
    void reset() noexcept;

    void push (const float* input, int numSamples) noexcept;

    /** Writes up to maxOutputs samples and returns how many were produced. */
    int pop (float* output, int maxOutputs) noexcept;

    /** How far past an output's time the kernel reads, in input samples. */
    int getLookaheadInputSamples() const noexcept { return halfTaps; }

    /** Largest number of outputs that n pushed inputs can make available. */
    int maxOutputsFor (int numInputs) const noexcept;

private:
    float kernelAt (double distance) const noexcept;
    void compact() noexcept;

    std::int64_t stepNum = 1, stepDen = 1; // input samples per output sample = stepNum / stepDen
    int halfTaps = 0;                      // kernel half-width in input samples
    int tablePhases = 0;
    std::vector<float> table;              // kernel sampled every 1/tablePhases input samples

    std::vector<float> buffer;
    std::int64_t bufferStart = 0;          // absolute input index of buffer[0]
    int bufferLength = 0;

    std::int64_t phaseBase = 0;            // absolute input index of the current output's integer time
    std::int64_t phaseNum = 0;             // fractional part of that time, in units of 1/stepDen
};
} // namespace evil
