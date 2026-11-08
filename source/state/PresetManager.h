#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace evil {
/** Factory presets (embedded) and user presets (XML files in the user's
   app-data folder).

    A preset is every parameter value plus the model and IR sources. Parameters
   missing from a preset fall back to their defaults, so old presets keep
   working as parameters are added.

    A user preset also carries copies of its model and IR files, so it's one file
   that keeps its sound if the originals move or it goes to another computer. They
   only come back out if the originals aren't there as they were. Message thread
   only. */
class PresetManager {
public:
  struct Sources {
    juce::String model, ir;
  };

  PresetManager(juce::AudioProcessorValueTreeState &state,
                std::function<Sources()> getSources,
                std::function<void(const Sources &)> applySources);

  static juce::File getUserPresetDirectory();
  /** Where a preset's carried files come back out to, when they have to. */
  static juce::File getSavedFilesDirectory();
  static void setUserPresetDirectoryForTesting(const juce::File &);
  static constexpr const char *fileExtension = ".evilpreset";
  /** Beside a file, who made it and what it is. A preset carries that too. */
  static constexpr const char *attributionFileName = "attribution.json";

  juce::StringArray getNames() const;
  int getNumFactoryPresets() const { return factory.size(); }
  int getCurrentIndex() const { return currentIndex; }
  juce::String getCurrentName() const;

  bool load(int index);
  /** False, with error, if nothing was saved. If it was but a file couldn't go
      with it, warning says so. */
  bool saveUserPreset(const juce::String &name, juce::String &error,
                      juce::String *warning = nullptr);
  void refreshUserPresets();

  /** The sound as it is now, in preset form, and putting one back. The agent's
      undo uses these without touching the preset list. */
  std::unique_ptr<juce::XmlElement> capture(const juce::String &name) const;
  void apply(const juce::XmlElement &preset);

private:
  juce::AudioProcessorValueTreeState &state;
  std::function<Sources()> getSources;
  std::function<void(const Sources &)> applySources;

  juce::OwnedArray<juce::XmlElement> factory;
  juce::Array<juce::File> userFiles;
  int currentIndex = 0;
};
} // namespace evil
