#include "Parameters.h"

namespace evil::params
{
namespace
{
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;

    juce::AudioParameterFloatAttributes decibels()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });
    }

    juce::NormalisableRange<float> range (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    }

    std::atomic<float>* raw (juce::AudioProcessorValueTreeState& s, const char* id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Float> (juce::ParameterID { input, 1 }, "Input",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, decibels()));
    layout.add (std::make_unique<Bool> (juce::ParameterID { gateOn, 1 }, "Gate", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { gateThreshold, 1 }, "Gate Threshold",
                                         juce::NormalisableRange<float> (-100.0f, -20.0f, 0.1f), -65.0f, decibels()));
    layout.add (std::make_unique<Choice> (juce::ParameterID { ampMode, 1 }, "Amp",
                                          juce::StringArray { "Off", "Overdrive", "Neural" }, 2));
    layout.add (std::make_unique<Float> (juce::ParameterID { drive, 1 }, "Drive",
                                         juce::NormalisableRange<float> (0.0f, 40.0f, 0.01f), 20.0f, decibels()));
    layout.add (std::make_unique<Float> (juce::ParameterID { tone, 1 }, "Tone",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                             [] (float v, int) { return juce::String (v * 10.0f, 1); })));
    layout.add (std::make_unique<Choice> (juce::ParameterID { oversampling, 1 }, "Oversampling",
                                          juce::StringArray { "Off", "2x", "4x" }, 2));
    layout.add (std::make_unique<Bool> (juce::ParameterID { normalise, 1 }, "Normalise Model", true));
    layout.add (std::make_unique<Bool> (juce::ParameterID { cabOn, 1 }, "Cabinet", true));
    for (auto [id, name] : { std::pair { bass, "Bass" }, { mid, "Mid" }, { treble, "Treble" } })
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, name,
                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, decibels()));
    layout.add (std::make_unique<Float> (juce::ParameterID { output, 1 }, "Output",
                                         range (-40.0f, 12.0f, 0.0f), 0.0f, decibels()));
    return layout;
}

Snapshot::Snapshot (juce::AudioProcessorValueTreeState& s)
    : input (raw (s, params::input)),
      gateOn (raw (s, params::gateOn)),
      gateThreshold (raw (s, params::gateThreshold)),
      ampMode (raw (s, params::ampMode)),
      drive (raw (s, params::drive)),
      tone (raw (s, params::tone)),
      oversampling (raw (s, params::oversampling)),
      normalise (raw (s, params::normalise)),
      cabOn (raw (s, params::cabOn)),
      bass (raw (s, params::bass)),
      mid (raw (s, params::mid)),
      treble (raw (s, params::treble)),
      output (raw (s, params::output))
{
}

SignalChain::Settings Snapshot::read() const noexcept
{
    const auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    SignalChain::Settings s;
    s.inputDb = get (input);
    s.gateOn = get (gateOn) >= 0.5f;
    s.gateThresholdDb = get (gateThreshold);
    s.amp.mode = static_cast<AmpMode> (juce::jlimit (0, 2, juce::roundToInt (get (ampMode))));
    s.amp.driveDb = get (drive);
    s.amp.tone = get (tone);
    s.amp.oversampling = juce::jlimit (0, 2, juce::roundToInt (get (oversampling)));
    s.amp.normalise = get (normalise) >= 0.5f;
    s.cabOn = get (cabOn) >= 0.5f;
    s.bassDb = get (bass);
    s.midDb = get (mid);
    s.trebleDb = get (treble);
    s.outputDb = get (output);
    return s;
}
} // namespace evil::params
