#pragma once

#include "NamModelRunner.h"

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace evil
{
/** Owns the neural amp model and moves new ones onto the audio thread without blocking it.

    Loading (file I/O, JSON parsing, building and prewarming the network) happens on a
    worker thread. The finished model is handed over through a single atomic pointer that
    the audio thread swaps in at the start of a block, crossfading from the old model over
    ~40 ms so the change doesn't click. The old model is passed back through a lock-free
    FIFO and deleted on the message thread by collectGarbage(), never on the audio thread.

    Threads: process() is audio-thread only; everything else is for non-audio threads. */
class ModelManager : public juce::ChangeBroadcaster
{
public:
    enum class State { empty, loading, ready, error };

    struct Status
    {
        State state = State::empty;
        ModelInfo info;        // valid when ready
        juce::String message;  // error text, or the source being loaded
    };

    static constexpr const char* builtinPrefix = "builtin:";

    ModelManager();
    ~ModelManager() override;

    /** Call from prepareToPlay (audio stopped). Re-prepares the current and pending models. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Starts loading asynchronously. source is a .nam path or "builtin:<name>". */
    void requestLoad (const juce::String& source);
    void requestUnload();

    /** Loads on the calling thread and hands the model to the audio thread. For tests/CLI. */
    bool loadSynchronously (const juce::String& source, juce::String& error);

    /** The source most recently requested (what presets and plugin state should save). */
    juce::String getRequestedSource() const;
    Status getStatus() const;

    /** Deletes models the audio thread has finished with. Call regularly on the message thread. */
    void collectGarbage();

    /** Audio thread: renders the active model (or passes dry if none). in/out may alias. */
    void process (const float* in, float* out, int n, bool normalise) noexcept;

    /** Latency of the model currently playing; written by the audio thread. */
    int getActiveLatencySamples() const noexcept { return activeLatency.load (std::memory_order_relaxed); }

    static juce::StringArray getBuiltinNames();

    /** Switch NAM Core to its rational tanh approximation (max error 9e-4), as the official
        NAM plugin does. tanh dominates WaveNet inference, so this is several times faster.
        Process-wide and one-way: call before the first model is loaded. */
    static void enableFastTanh();

private:
    struct LoadResult
    {
        std::unique_ptr<NamModelRunner> runner;
        juce::String error;
    };

    static LoadResult buildRunner (const juce::String& source);
    void workerLoop();
    void post (std::unique_ptr<NamModelRunner> runner, std::uint64_t requestId);
    void setStatus (Status newStatus);

    // Audio-thread helpers
    void pickUpPending() noexcept;
    void render (NamModelRunner* runner, const float* in, float* out, int n,
                 float normaliseStart, float normaliseEnd) noexcept;
    bool retire (NamModelRunner* runner) noexcept;

    // Handover: the loader stores, the audio thread exchanges. unloadToken means "go dry".
    std::atomic<NamModelRunner*> pending { nullptr };
    static NamModelRunner* unloadToken() noexcept;

    // Audio-thread state (the spec copies are written by prepare(), while audio is stopped)
    double specRateSeenByAudio = 0.0;
    int specBlockSeenByAudio = 0;
    NamModelRunner* active = nullptr;
    NamModelRunner* fadingOut = nullptr;
    bool fading = false;
    int fadePosition = 0, fadeLength = 2048;
    float normaliseAmount = 1.0f;
    bool primed = false; // snap the normalisation amount on the first block after prepare()
    std::vector<float> inputCopy, fadeBuffer;
    std::atomic<int> activeLatency { 0 };

    // Retired runners, audio thread -> message thread.
    juce::AbstractFifo trashFifo { 32 };
    std::array<NamModelRunner*, 32> trash {};

    // Spec shared by prepare() and the loader. Never touched by the audio thread.
    std::mutex specMutex;
    double specRate = 0.0;
    int specBlockSize = 0;

    mutable std::mutex statusMutex;
    Status status;
    juce::String requestedSource;

    // Worker
    std::mutex requestMutex;
    std::condition_variable requestCondition;
    juce::String requestSource;
    bool hasRequest = false, quit = false;
    std::atomic<std::uint64_t> latestRequestId { 0 };
    std::uint64_t requestIdForWorker = 0;
    std::thread worker;
};
} // namespace evil
