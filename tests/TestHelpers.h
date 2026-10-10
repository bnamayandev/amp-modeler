#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

namespace evil::test
{
inline juce::File dataFile (const char* name)
{
    return juce::File (EVIL_TEST_DATA_DIR).getChildFile (name);
}

inline std::vector<float> readWav (const juce::File& file, double* sampleRate = nullptr)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return {};
    if (sampleRate != nullptr)
        *sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false);
    return { buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples() };
}

inline void writeWav (const juce::File& file, const std::vector<float>& data, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (1)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    const float* channels[] = { data.data() };
    writer->writeFromFloatArrays (channels, 1, (int) data.size());
}

inline std::vector<float> sine (double freq, double sampleRate, int n, float amplitude = 0.5f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate);
    return x;
}

inline double rms (const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / std::max (1, n));
}

inline double esr (const float* target, const float* prediction, int n)
{
    double err = 0.0, sig = 0.0;
    for (int i = 0; i < n; ++i)
    {
        err += ((double) target[i] - prediction[i]) * ((double) target[i] - prediction[i]);
        sig += (double) target[i] * target[i];
    }
    return err / std::max (sig, 1.0e-30);
}

/** Amplitude of the component at `freq`, by correlation with sin/cos over whole cycles-ish. */
inline double toneAmplitude (const float* x, int n, double freq, double sampleRate)
{
    double s = 0.0, c = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate;
        s += x[i] * std::sin (w);
        c += x[i] * std::cos (w);
    }
    return 2.0 * std::sqrt (s * s + c * c) / n;
}
} // namespace evil::test
