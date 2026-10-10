#include "TestHelpers.h"
#include "dsp/ModelManager.h"

#include <NAM/get_dsp.h>

#include <catch2/catch_test_macros.hpp>

using namespace evil;
using namespace evil::test;

namespace
{
/** Loads a model through the plugin's manager and renders `in` exactly as the plugin would. */
std::vector<float> renderThroughManager (const juce::String& source, const std::vector<float>& in, double rate, int block)
{
    ModelManager models;
    models.prepare (rate, block);
    juce::String error;
    REQUIRE (models.loadSynchronously (source, error));

    // Let the dry->model crossfade finish on silence first (the model is already prewarmed on silence).
    const std::vector<float> silence ((size_t) block, 0.0f);
    std::vector<float> scratch ((size_t) block);
    for (int i = 0; i < (int) (0.1 * rate) / block + 1; ++i)
        models.process (silence.data(), scratch.data(), block, false);
    REQUIRE (models.getActiveLatencySamples() == 0);

    std::vector<float> out (in.size());
    for (size_t start = 0; start < in.size(); start += (size_t) block)
        models.process (in.data() + start, out.data() + start, (int) std::min<size_t> ((size_t) block, in.size() - start), false);
    return out;
}
} // namespace

TEST_CASE ("A model exported by the PyTorch trainer matches PyTorch in NAM Core", "[nam][parity]")
{
    // tests/data was written by `python -m evilamp.golden`: a random WaveNet, an input, and
    // PyTorch's output. Any mistake in the exporter's weight order shows up here.
    double rate = 0.0;
    const auto in = readWav (dataFile ("parity_input.wav"), &rate);
    const auto expected = readWav (dataFile ("parity_output.wav"));
    REQUIRE (in.size() == expected.size());
    REQUIRE (rate == 48000.0);

    const auto out = renderThroughManager (dataFile ("parity_wavenet.nam").getFullPathName(), in, rate, 128);

    float maxError = 0.0f;
    for (size_t i = 0; i < in.size(); ++i)
        maxError = std::max (maxError, std::abs (out[i] - expected[i]));
    const double e = esr (expected.data(), out.data(), (int) in.size());
    INFO ("max abs error " << maxError << ", ESR " << e << ", output rms " << rms (expected.data(), (int) expected.size()));
    CHECK (maxError < 1.0e-4f);
    CHECK (e < 1.0e-8);
}

TEST_CASE ("Every example model shipped with NAM Core loads and runs", "[nam]")
{
    // These are published .nam files covering WaveNet (A1 and A2), LSTM, slimmable and
    // container architectures: what "load existing NAM model files" has to handle.
    const auto files = juce::File (NAM_EXAMPLE_MODELS_DIR).findChildFiles (juce::File::findFiles, false, "*.nam");
    REQUIRE (files.size() >= 3);
    for (const auto& file : files)
    {
        DYNAMIC_SECTION (file.getFileName())
        {
            ModelManager models;
            models.prepare (48000.0, 128);
            juce::String error;
            if (! models.loadSynchronously (file.getFullPathName(), error))
            {
                // Multi-channel models are rejected on purpose; anything else is a failure.
                INFO (error);
                CHECK (error.contains ("mono"));
                continue;
            }
            auto in = sine (220.0, 48000.0, 48000, 0.3f);
            for (size_t start = 0; start < in.size(); start += 128)
                models.process (in.data() + start, in.data() + start, 128, true);
            bool finite = true;
            for (float x : in)
                finite = finite && std::isfinite (x);
            CHECK (finite);
        }
    }
}

TEST_CASE ("The bundled capture loads with its metadata", "[nam]")
{
    ModelManager models;
    juce::String error;
    REQUIRE (models.loadSynchronously ("builtin:BlackstarHT1", error));
    const auto info = models.getStatus().info;
    CHECK (info.architecture == "WaveNet");
    CHECK (info.sampleRate == 48000.0);
    CHECK (info.hasLoudness);
    CHECK (info.testEsr > 0.0);
    CHECK (info.testEsr < 0.05);
}

TEST_CASE ("Bad model files fail with a message instead of crashing", "[nam]")
{
    const auto dir = juce::File::createTempFile ("evil-models");
    dir.createDirectory();
    const auto notJson = dir.getChildFile ("garbage.nam");
    notJson.replaceWithText ("this is not json");
    const auto wrongVersion = dir.getChildFile ("version.nam");
    wrongVersion.replaceWithText (R"({"version":"9.0.0","architecture":"WaveNet","config":{},"weights":[]})");

    ModelManager models;
    for (const auto& f : { notJson, wrongVersion, dir.getChildFile ("missing.nam") })
    {
        juce::String error;
        CHECK_FALSE (models.loadSynchronously (f.getFullPathName(), error));
        CHECK (error.isNotEmpty());
        CHECK (models.getStatus().state == ModelManager::State::error);
    }
    dir.deleteRecursively();
}

TEST_CASE ("Async loads hand over to the audio thread and superseded loads are dropped", "[nam]")
{
    ModelManager models;
    models.prepare (48000.0, 64);
    models.requestLoad (dataFile ("parity_wavenet.nam").getFullPathName());
    models.requestLoad ("builtin:BlackstarHT1"); // supersedes the first

    std::vector<float> buffer (64, 0.0f);
    for (int i = 0; i < 2000 && models.getStatus().state != ModelManager::State::ready; ++i)
    {
        juce::Thread::sleep (5);
        models.process (buffer.data(), buffer.data(), 64, true);
    }
    REQUIRE (models.getStatus().state == ModelManager::State::ready);
    CHECK (models.getStatus().info.source == "builtin:BlackstarHT1");
    CHECK (models.getRequestedSource() == "builtin:BlackstarHT1");
    models.collectGarbage();
}
