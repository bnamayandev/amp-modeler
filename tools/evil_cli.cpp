// evil-cli: run the plugin's exact signal chain offline.
//
//   evil-cli render <in.wav> <out.wav> [chain options]
//   evil-cli esr    <in.wav> <target.wav> --model <m.nam>   [--block N] [--out prediction.wav]
//   evil-cli bench  [--seconds S] [--block N] [--rate R] [--csv file] [chain options]
//   evil-cli info   <m.nam>
//   evil-cli devices                (audio device types and devices this build can open)
//
// Chain options:
//   --model <path|builtin:Name>  --ir <path|builtin:Name>  --amp off|overdrive|neural
//   --no-cab  --no-gate  --no-normalise  --input-db X  --output-db X  --drive-db X
//   --oversampling 0|1|2  --block N  --exact-tanh (default: NAM's fast tanh, as in the plugin)
//
// `esr` runs the model alone (no gate, cab, EQ or normalisation) at the file's sample rate,
// so a 44.1 kHz file exercises the resampling path, and compares against the target.
// `bench` times every block against its real-time deadline: the numbers the plugin logs.

#include "dsp/SignalChain.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>

namespace
{
using evil::AmpMode;
using evil::SignalChain;

struct Options
{
    std::vector<juce::String> positional;
    std::map<juce::String, juce::String> values;
    std::vector<juce::String> flags;

    bool has (const juce::String& flag) const { return std::find (flags.begin(), flags.end(), flag) != flags.end(); }
    juce::String get (const juce::String& key, const juce::String& fallback = {}) const
    {
        const auto it = values.find (key);
        return it != values.end() ? it->second : fallback;
    }
};

Options parse (int argc, char** argv)
{
    static const juce::StringArray valued { "--model", "--ir",     "--amp",   "--input-db", "--output-db", "--drive-db",
                                            "--oversampling", "--block", "--seconds", "--rate", "--csv", "--out" };
    Options o;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (valued.contains (arg) && i + 1 < argc)
            o.values[arg] = argv[++i];
        else if (arg.startsWith ("--"))
            o.flags.push_back (arg);
        else
            o.positional.push_back (arg);
    }
    return o;
}

[[noreturn]] void fail (const juce::String& message)
{
    std::cerr << "error: " << message << std::endl;
    std::exit (1);
}

struct Audio
{
    juce::AudioBuffer<float> buffer; // mono
    double sampleRate = 0.0;
};

Audio readMono (const juce::String& path)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (path)));
    if (reader == nullptr)
        fail ("cannot read " + path);
    Audio a;
    a.sampleRate = reader->sampleRate;
    a.buffer.setSize (1, (int) reader->lengthInSamples);
    reader->read (&a.buffer, 0, (int) reader->lengthInSamples, 0, true, false);
    return a;
}

void writeMono (const juce::String& path, const float* data, int n, double sampleRate)
{
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (1)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    if (writer == nullptr)
        fail ("cannot write " + path);
    const float* channels[] = { data };
    writer->writeFromFloatArrays (channels, 1, n);
}

SignalChain::Settings settingsFrom (const Options& o)
{
    SignalChain::Settings s;
    s.inputDb = o.get ("--input-db", "0").getFloatValue();
    s.outputDb = o.get ("--output-db", "0").getFloatValue();
    s.gateOn = ! o.has ("--no-gate");
    s.cabOn = ! o.has ("--no-cab");
    s.amp.normalise = ! o.has ("--no-normalise");
    s.amp.driveDb = o.get ("--drive-db", "20").getFloatValue();
    s.amp.oversampling = o.get ("--oversampling", "2").getIntValue();
    const auto amp = o.get ("--amp", "neural");
    s.amp.mode = amp == "off" ? AmpMode::off : amp == "overdrive" ? AmpMode::overdrive : AmpMode::neural;
    return s;
}

/** Prepares the chain, loads model and IR, and settles the model swap crossfade on silence. */
void setUp (SignalChain& chain, const Options& o, double sampleRate, int block, const SignalChain::Settings& s)
{
    chain.prepare (sampleRate, block);
    juce::String error;
    if (! chain.getCab().load (o.get ("--ir", "builtin:EvilCab4x12"), error))
        fail (error);
    if (! chain.getModels().loadSynchronously (o.get ("--model", "builtin:BlackstarHT1"), error))
        fail (error);

    std::vector<float> silence ((size_t) block, 0.0f);
    for (int done = 0; done < (int) (0.2 * sampleRate); done += block)
    {
        std::fill (silence.begin(), silence.end(), 0.0f);
        chain.process (silence.data(), block, s);
    }
    chain.getModels().collectGarbage();
}

/** Runs `in` through the chain block by block; returns output aligned to the input (latency removed). */
std::vector<float> run (SignalChain& chain, const float* in, int n, int block, const SignalChain::Settings& s)
{
    const int latency = chain.getLatencySamples();
    std::vector<float> io ((size_t) (n + latency), 0.0f);
    std::copy (in, in + n, io.begin());
    for (int start = 0; start < (int) io.size(); start += block)
        chain.process (io.data() + start, std::min (block, (int) io.size() - start), s);
    return { io.begin() + latency, io.end() };
}

int render (const Options& o)
{
    if (o.positional.size() != 2)
        fail ("usage: evil-cli render <in.wav> <out.wav> [options]");
    const auto in = readMono (o.positional[0]);
    const int block = o.get ("--block", "128").getIntValue();
    const auto s = settingsFrom (o);
    SignalChain chain;
    setUp (chain, o, in.sampleRate, block, s);
    const auto out = run (chain, in.buffer.getReadPointer (0), in.buffer.getNumSamples(), block, s);
    writeMono (o.positional[1], out.data(), (int) out.size(), in.sampleRate);
    std::cout << "rendered " << out.size() << " samples at " << in.sampleRate << " Hz, latency "
              << chain.getLatencySamples() << " samples (compensated)" << std::endl;
    return 0;
}

int esr (const Options& o)
{
    if (o.positional.size() != 2)
        fail ("usage: evil-cli esr <in.wav> <target.wav> --model <m.nam>");
    const auto in = readMono (o.positional[0]);
    const auto target = readMono (o.positional[1]);
    if (! juce::exactlyEqual (in.sampleRate, target.sampleRate))
        fail ("input and target sample rates differ");

    SignalChain::Settings s;
    s.gateOn = false;
    s.cabOn = false;
    s.amp.mode = AmpMode::neural;
    s.amp.normalise = false;
    const int block = o.get ("--block", "128").getIntValue();

    SignalChain chain;
    setUp (chain, o, in.sampleRate, block, s);
    const auto status = chain.getModels().getStatus();
    const int n = std::min (in.buffer.getNumSamples(), target.buffer.getNumSamples());
    const auto out = run (chain, in.buffer.getReadPointer (0), n, block, s);

    double err = 0.0, sig = 0.0;
    const auto* y = target.buffer.getReadPointer (0);
    for (int i = 0; i < n; ++i)
    {
        err += ((double) y[i] - out[(size_t) i]) * ((double) y[i] - out[(size_t) i]);
        sig += (double) y[i] * y[i];
    }
    const double value = err / std::max (sig, 1.0e-20);
    std::printf ("model %s (%.0f Hz) on %.1f s at %.0f Hz%s, block %d: ESR %.6f\n",
                 status.info.name.toRawUTF8(), status.info.sampleRate, n / in.sampleRate, in.sampleRate,
                 ! juce::exactlyEqual (in.sampleRate, status.info.sampleRate) ? " (resampled)" : "", block, value);
    if (o.values.count ("--out"))
        writeMono (o.get ("--out"), out.data(), n, in.sampleRate);
    return 0;
}

int bench (const Options& o)
{
    const double rate = o.get ("--rate", "48000").getDoubleValue();
    const int block = o.get ("--block", "128").getIntValue();
    const double seconds = o.get ("--seconds", "60").getDoubleValue();
    const auto s = settingsFrom (o);

    SignalChain chain;
    setUp (chain, o, rate, block, s);

    // A guitar-like test signal: decaying plucked partials with a little noise, -18 dBFS-ish peaks.
    juce::Random random (42);
    const int numBlocks = (int) std::ceil (seconds * rate / block);
    std::vector<float> buffer ((size_t) block);
    std::vector<double> times;
    times.reserve ((size_t) numBlocks);
    double phase = 0.0, env = 0.0, freq = 110.0;
    long long t = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < block; ++i, ++t)
        {
            if (t % (long long) (0.5 * rate) == 0)
            {
                env = 0.25;
                freq = 82.41 * std::pow (2.0, random.nextInt (24) / 12.0);
            }
            env *= 0.99993;
            phase += juce::MathConstants<double>::twoPi * freq / rate;
            buffer[(size_t) i] = (float) (env * (std::sin (phase) + 0.3 * std::sin (2.0 * phase) + 0.15 * std::sin (3.0 * phase))
                                          + 0.0005 * (random.nextFloat() - 0.5f));
        }
        const auto start = juce::Time::getHighResolutionTicks();
        chain.process (buffer.data(), block, s);
        times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
    }

    const double deadline = block / rate;
    std::vector<double> sorted = times;
    std::sort (sorted.begin(), sorted.end());
    const auto pct = [&] (double p) { return 100.0 * sorted[(size_t) std::min ((double) sorted.size() - 1, p * sorted.size())] / deadline; };
    const double mean = std::accumulate (times.begin(), times.end(), 0.0) / (double) times.size();
    const auto overruns = std::count_if (times.begin(), times.end(), [&] (double x) { return x > deadline; });
    const auto status = chain.getModels().getStatus();

    std::printf ("chain: amp=%s model=%s cab=%s, %.0f Hz, %d-sample blocks (deadline %.3f ms), %.0f s of audio\n",
                 o.get ("--amp", "neural").toRawUTF8(), status.info.name.toRawUTF8(), s.cabOn ? "on" : "off", rate, block,
                 1000.0 * deadline, seconds);
    std::printf ("time per block as %% of deadline: mean %.2f  p50 %.2f  p99 %.2f  p99.9 %.2f  max %.2f\n",
                 100.0 * mean / deadline, pct (0.5), pct (0.99), pct (0.999), 100.0 * sorted.back() / deadline);
    std::printf ("blocks over deadline: %lld of %zu; latency %d samples\n", (long long) overruns, times.size(),
                 chain.getLatencySamples());

    if (o.values.count ("--csv"))
    {
        const auto csv = juce::File::getCurrentWorkingDirectory().getChildFile (o.get ("--csv"));
        csv.deleteFile();
        juce::FileOutputStream out (csv);
        if (! out.openedOk())
            fail ("cannot write " + csv.getFullPathName());
        out << "block,seconds,fraction_of_deadline\n";
        for (size_t i = 0; i < times.size(); ++i)
            out << (int) i << "," << juce::String (times[i], 9) << "," << juce::String (times[i] / deadline, 6) << "\n";
    }
    return 0;
}

int devices()
{
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes (types);
    for (auto* type : types)
    {
        type->scanForDevices();
        std::printf ("%s\n", type->getTypeName().toRawUTF8());
        for (bool input : { true, false })
        {
            const auto names = type->getDeviceNames (input);
            std::printf ("  %s: %s\n", input ? "inputs " : "outputs",
                         names.isEmpty() ? "(none)" : names.joinIntoString (", ").toRawUTF8());
        }
    }
    return 0;
}

int info (const Options& o)
{
    if (o.positional.size() != 1)
        fail ("usage: evil-cli info <model.nam|builtin:Name>");
    evil::ModelManager models;
    juce::String error;
    if (! models.loadSynchronously (o.positional[0], error))
        fail (error);
    const auto i = models.getStatus().info;
    std::printf ("name:          %s\ngear:          %s\narchitecture:  %s\nweights:       %d\nsample rate:   %.0f Hz\n",
                 i.name.toRawUTF8(), i.gear.toRawUTF8(), i.architecture.toRawUTF8(), i.numWeights, i.sampleRate);
    std::printf ("loudness:      %s\nvalidation ESR %s\nheld-out ESR: %s\n",
                 i.hasLoudness ? (juce::String (i.loudnessDb, 2) + " dB").toRawUTF8() : "unknown",
                 i.validationEsr >= 0 ? juce::String (i.validationEsr, 5).toRawUTF8() : "unknown",
                 i.testEsr >= 0 ? juce::String (i.testEsr, 5).toRawUTF8() : "unknown");
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedNoDenormals noDenormals; // as the plugin's processBlock does
    juce::ScopedJuceInitialiser_GUI juce; // message manager for ChangeBroadcaster
    if (argc < 2)
    {
        std::cerr << "usage: evil-cli render|esr|bench|info ...  (see tools/evil_cli.cpp)" << std::endl;
        return 1;
    }
    const juce::String command (argv[1]);
    const auto options = parse (argc, argv);
    if (! options.has ("--exact-tanh"))
        evil::ModelManager::enableFastTanh(); // what the plugin runs
    if (command == "render") return render (options);
    if (command == "esr") return esr (options);
    if (command == "bench") return bench (options);
    if (command == "info") return info (options);
    if (command == "devices") return devices();
    fail ("unknown command " + command);
}
