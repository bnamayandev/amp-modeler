#include "AllocationTracker.h"
#include "TestHelpers.h"
#include "dsp/SignalChain.h"

#include <catch2/catch_test_macros.hpp>

using namespace evil;
using namespace evil::test;

TEST_CASE ("The audio path never allocates or frees, even while models and IRs are swapped", "[realtime]")
{
    const auto dir = juce::File::createTempFile ("evil-rt");
    dir.createDirectory();
    const auto altIr = dir.getChildFile ("alt.wav");
    {
        std::vector<float> ir (2048);
        for (size_t i = 0; i < ir.size(); ++i)
            ir[i] = std::exp (-(float) i / 300.0f) * std::sin ((float) i * 0.3f);
        writeWav (altIr, ir, 44100.0); // a different rate: the convolution resamples it
    }

    constexpr int block = 128;
    SignalChain chain;
    chain.prepare (48000.0, block);
    juce::String error;
    REQUIRE (chain.getCab().load ("builtin:EvilCab4x12", error));
    REQUIRE (chain.getModels().loadSynchronously ("builtin:BlackstarHT1", error));

    SignalChain::Settings s;
    std::vector<float> buffer (block);
    juce::Random random (9);
    double phase = 0.0;

    long allocations = 0, frees = 0, blocks = 0;
    for (int b = 0; b < 6000; ++b) // ~16 s of audio
    {
        // Control-thread work between blocks, as a GUI or host would do it (not tracked).
        if (b % 500 == 250)
            REQUIRE (chain.getCab().load ((b / 500) % 2 == 0 ? altIr.getFullPathName() : juce::String ("builtin:EvilCab4x12"), error));
        if (b % 700 == 350)
            REQUIRE (chain.getModels().loadSynchronously ((b / 700) % 2 == 0 ? dataFile ("parity_wavenet.nam").getFullPathName()
                                                                              : juce::String ("builtin:BlackstarHT1"),
                                                          error));
        if (b % 900 == 450)
            chain.getModels().requestUnload();
        if (b % 300 == 0)
        {
            s.amp.mode = static_cast<AmpMode> (random.nextInt (3));
            s.amp.oversampling = random.nextInt (3);
            s.cabOn = random.nextBool();
            s.gateOn = random.nextBool();
            s.amp.normalise = random.nextBool();
            s.bassDb = random.nextFloat() * 24.0f - 12.0f;
            s.trebleDb = random.nextFloat() * 24.0f - 12.0f;
            s.inputDb = random.nextFloat() * 12.0f;
            s.amp.driveDb = random.nextFloat() * 40.0f;
        }
        if (b % 100 == 0)
            chain.getModels().collectGarbage();

        for (auto& x : buffer)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * 196.0 / 48000.0;
            x = 0.2f * (float) std::sin (phase) + 0.01f * (random.nextFloat() - 0.5f);
        }

        // The audio-thread part: tracked.
        {
            ScopedAllocationTracking tracking;
            chain.process (buffer.data(), block, s);
            allocations += tracking.allocationCount();
            frees += tracking.deallocationCount();
        }
        ++blocks;
        for (float x : buffer)
            REQUIRE (std::isfinite (x));
    }

    INFO ("C allocator interposed: " << (interposesCAllocator() ? "yes" : "no (C++ new/delete only)"));
    CHECK (blocks == 6000);
    CHECK (allocations == 0);
    CHECK (frees == 0);
    chain.getModels().collectGarbage();
    dir.deleteRecursively();
}

TEST_CASE ("The neural chain at 128 samples / 48 kHz runs well inside its deadline", "[realtime][cpu]")
{
    // The brief's target is < 25% of the deadline per instance. This runs NAM's exact tanh (the
    // plugin uses the ~2.5x faster approximation), so it is a conservative bound. Timing on
    // shared CI machines is noisy: CI skips this with "~[cpu]". Debug builds aren't checked.
    constexpr int block = 128;
    SignalChain chain;
    chain.prepare (48000.0, block);
    juce::String error;
    REQUIRE (chain.getModels().loadSynchronously ("builtin:BlackstarHT1", error));
    REQUIRE (chain.getCab().load ("builtin:EvilCab4x12", error));
    SignalChain::Settings s;

    auto x = sine (110.0, 48000.0, 48000 * 10, 0.2f);
    std::vector<double> times;
    for (size_t start = 0; start < x.size(); start += block)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        chain.process (x.data() + start, block, s);
        times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
    }
    std::sort (times.begin(), times.end());
    const double deadline = block / 48000.0;
    const double median = times[times.size() / 2] / deadline;
    INFO ("median block time " << 100.0 * median << "% of deadline, p99 " << 100.0 * times[times.size() * 99 / 100] / deadline << "%");
#if defined(NDEBUG)
    CHECK (median < 0.25);
#else
    SUCCEED ("debug build: timing reported, not checked");
#endif
}
