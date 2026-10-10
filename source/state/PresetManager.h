#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace evil
{
/** Factory presets (embedded) and user presets (XML files in the user's app-data folder).

    A preset is every parameter value plus the model and IR sources. Parameters missing
    from a preset fall back to their defaults, so old presets keep working as parameters
    are added. Message thread only. */
class PresetManager
{
public:
    struct Sources
    {
        juce::String model, ir;
    };

    PresetManager (juce::AudioProcessorValueTreeState& state,
                   std::function<Sources()> getSources,
                   std::function<void (const Sources&)> applySources);

    static juce::File getUserPresetDirectory();
    static constexpr const char* fileExtension = ".evilpreset";

    juce::StringArray getNames() const;
    int getNumFactoryPresets() const { return factory.size(); }
    int getCurrentIndex() const { return currentIndex; }
    juce::String getCurrentName() const;

    bool load (int index);
    bool saveUserPreset (const juce::String& name, juce::String& error);
    void refreshUserPresets();

private:
    std::unique_ptr<juce::XmlElement> capture (const juce::String& name) const;
    void apply (const juce::XmlElement& preset);

    juce::AudioProcessorValueTreeState& state;
    std::function<Sources()> getSources;
    std::function<void (const Sources&)> applySources;

    juce::OwnedArray<juce::XmlElement> factory;
    juce::Array<juce::File> userFiles;
    int currentIndex = 0;
};
} // namespace evil
