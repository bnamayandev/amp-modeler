#include "PresetManager.h"

#include "EvilBinaryData.h"
#include "Parameters.h"

namespace evil {
namespace {
constexpr const char *presetTag = "Preset";
constexpr const char *paramTag = "Param";
} // namespace

PresetManager::PresetManager(juce::AudioProcessorValueTreeState &s,
                             std::function<Sources()> get,
                             std::function<void(const Sources &)> set)
    : state(s), getSources(std::move(get)), applySources(std::move(set)) {
  if (auto xml = juce::parseXML(juce::String::createStringFromData(
          EvilBinaryData::factory_presets_xml,
          EvilBinaryData::factory_presets_xmlSize)))
    for (auto *preset : xml->getChildWithTagNameIterator(presetTag))
      factory.add(new juce::XmlElement(*preset));

  jassert(!factory.isEmpty());
  refreshUserPresets();
}

juce::File PresetManager::getUserPresetDirectory() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("Presets");
}

void PresetManager::refreshUserPresets() {
  userFiles = getUserPresetDirectory().findChildFiles(
      juce::File::findFiles, false, juce::String("*") + fileExtension);
  userFiles.sort();
}

juce::StringArray PresetManager::getNames() const {
  juce::StringArray names;
  for (auto *p : factory)
    names.add(p->getStringAttribute("name"));
  for (const auto &f : userFiles)
    names.add(f.getFileNameWithoutExtension());
  return names;
}

juce::String PresetManager::getCurrentName() const {
  return getNames()[currentIndex];
}

bool PresetManager::load(int index) {
  if (index < 0)
    return false;

  if (index < factory.size()) {
    apply(*factory[index]);
    currentIndex = index;
    return true;
  }

  const auto userIndex = index - factory.size();
  if (userIndex >= userFiles.size())
    return false;
  const auto xml = juce::parseXML(userFiles[userIndex]);
  if (xml == nullptr || !xml->hasTagName(presetTag))
    return false;
  apply(*xml);
  currentIndex = index;
  return true;
}

std::unique_ptr<juce::XmlElement>
PresetManager::capture(const juce::String &name) const {
  auto xml = std::make_unique<juce::XmlElement>(presetTag);
  xml->setAttribute("name", name);
  for (auto *p : state.processor.getParameters())
    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p)) {
      if (params::isGlobal(ranged->getParameterID()))
        continue;
      auto *e = xml->createNewChildElement(paramTag);
      e->setAttribute("id", ranged->getParameterID());
      e->setAttribute("value",
                      (double)ranged->convertFrom0to1(ranged->getValue()));
    }
  const auto sources = getSources();
  xml->setAttribute("model", sources.model);
  xml->setAttribute("ir", sources.ir);
  return xml;
}

void PresetManager::apply(const juce::XmlElement &preset) {
  for (auto *p : state.processor.getParameters())
    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p)) {
      if (params::isGlobal(ranged->getParameterID()))
        continue; // the player's transpose and tempo survive a preset change
      float value = ranged->getDefaultValue(); // normalised
      for (auto *e : preset.getChildWithTagNameIterator(paramTag))
        if (e->getStringAttribute("id") == ranged->getParameterID())
          value = ranged->convertTo0to1((float)e->getDoubleAttribute("value"));
      ranged->beginChangeGesture();
      ranged->setValueNotifyingHost(value);
      ranged->endChangeGesture();
    }
  applySources(
      {preset.getStringAttribute("model"), preset.getStringAttribute("ir")});
}

bool PresetManager::saveUserPreset(const juce::String &rawName,
                                   juce::String &error) {
  const auto name = juce::File::createLegalFileName(rawName.trim());
  if (name.isEmpty()) {
    error = "Enter a preset name";
    return false;
  }
  const auto dir = getUserPresetDirectory();
  if (!dir.createDirectory()) {
    error = "Could not create " + dir.getFullPathName();
    return false;
  }
  const auto file = dir.getChildFile(name + fileExtension);
  if (!capture(name)->writeTo(file)) {
    error = "Could not write " + file.getFullPathName();
    return false;
  }
  refreshUserPresets();
  currentIndex = factory.size() + userFiles.indexOf(file);
  return true;
}
} // namespace evil
