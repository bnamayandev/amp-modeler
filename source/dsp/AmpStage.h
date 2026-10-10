#pragma once

#include "ModelManager.h"
#include "Overdrive.h"

#include <atomic>
#include <vector>

namespace evil
{
enum class AmpMode
{
    off = 0,       // clean pass-through
    overdrive = 1, // gray-box waveshaper (oversampled)
    neural = 2     // NAM model at its native rate
};

/** The nonlinear stage: either the gray-box overdrive or the neural model. Switching mode
    crossfades between the two outputs over 20 ms. */
class AmpStage
{
public:
    struct Settings
    {
        AmpMode mode = AmpMode::neural;
        float driveDb = 20.0f;
        float tone = 0.5f;
        int oversampling = 2;
        bool normalise = true;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (float* data, int n, const Settings& settings) noexcept;

    /** Latency of the current mode, as last seen by the audio thread. */
    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    /** Latency the given settings will have (for prepareToPlay, before audio runs). */
    int computeLatencySamples (const Settings& settings) const noexcept;

    ModelManager& getModels() noexcept { return models; }

private:
    void render (AmpMode mode, float* data, int n, const Settings& settings) noexcept;

    Overdrive overdrive;
    ModelManager models;
    AmpMode currentMode = AmpMode::neural, previousMode = AmpMode::neural;
    int fadePosition = 0, fadeLength = 960;
    std::vector<float> fadeBuffer;
    std::atomic<int> latency { 0 };
    bool primed = false; // snap to the first block's mode after prepare()
};
} // namespace evil
