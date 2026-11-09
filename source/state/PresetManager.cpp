#include "PresetManager.h"

#include "EvilBinaryData.h"
#include "Parameters.h"

#include <juce_cryptography/juce_cryptography.h>

namespace evil {
namespace {
constexpr const char *presetTag = "Preset";
constexpr const char *paramTag = "Param";
constexpr const char *fileTag = "File";
// Far past any real capture or IR, which are under a megabyte or so. A preset this
// big would stall the message thread every time it was picked.
constexpr juce::int64 maxCarriedBytes = 64 * 1024 * 1024;

juce::File userPresetDirectoryOverride;

bool isModelRole(const juce::String &role) { return role == "model"; }

/** Copies a preset's file into it. Empty if that went fine or there's nothing to
    carry, as a built-in comes with the amp; otherwise why not. */
juce::String carryFile(juce::XmlElement &preset, const char *role) {
  const auto source = preset.getStringAttribute(role);
  if (!juce::File::isAbsolutePath(source))
    return {};
  const juce::File file(source);
  const juce::String what = isModelRole(role) ? "capture" : "IR";
  juce::MemoryBlock data;
  if (!file.existsAsFile())
    return what + " (" + file.getFileName() + " is no longer there)";
  if (file.getSize() > maxCarriedBytes)
    return what + " (" + file.getFileName() + " is too big)";
  if (!file.loadFileAsData(data) || data.isEmpty())
    return what + " (" + file.getFileName() + " couldn't be read)";

  auto *carried = preset.createNewChildElement(fileTag);
  carried->setAttribute("for", role);
  carried->setAttribute("name", file.getFileName());
  const auto attribution = file.getSiblingFile(PresetManager::attributionFileName);
  if (attribution.existsAsFile())
    carried->setAttribute("attribution", attribution.loadFileAsString());
  carried->addTextElement(juce::Base64::toBase64(data.getData(), data.getSize()));
  return {};
}

/** A carried file's name, made safe, since a preset can come from anyone. Empty
    if it isn't the kind of file this role loads. */
juce::String carriedFileName(const juce::XmlElement &carried, const juce::String &role) {
  const auto name =
      juce::File::createLegalFileName(carried.getStringAttribute("name")).trim();
  const auto extension =
      name.fromLastOccurrenceOf(".", true, false).toLowerCase();
  const juce::StringArray allowed =
      isModelRole(role) ? juce::StringArray{".nam"}
                        : juce::StringArray{".wav", ".aif", ".aiff", ".flac"};
  if (!allowed.contains(extension))
    return {};
  // Kept to a length any filesystem takes, and always with its extension.
  return name.dropLastCharacters(extension.length()).substring(0, 80).trim() + extension;
}

/** Points the preset at its carried files, putting them back on disk if the
    originals aren't there as they were. Where they go is named by what's in them,
    so nothing in a preset chooses a path and the same file is only kept once. If
    one is damaged, the preset is left to try the original path as before. */
void restoreCarriedFiles(juce::XmlElement &preset) {
  for (auto *carried : preset.getChildWithTagNameIterator(fileTag)) {
    const auto role = carried->getStringAttribute("for");
    if (!isModelRole(role) && role != "ir")
      continue;
    const auto name = carriedFileName(*carried, role);
    juce::MemoryOutputStream decoded;
    if (name.isEmpty() || !juce::Base64::convertFromBase64(
                              decoded, carried->getAllSubText().removeCharacters(" \t\r\n")))
      continue;
    const juce::MemoryBlock data(decoded.getData(), decoded.getDataSize());
    if (data.isEmpty())
      continue;

    const auto source = preset.getStringAttribute(role);
    if (juce::File::isAbsolutePath(source)) {
      const juce::File original(source);
      juce::MemoryBlock existing;
      if (original.getSize() == (juce::int64)data.getSize() &&
          original.loadFileAsData(existing) && existing == data)
        continue; // still there, and where its credit is
    }

    const auto dir = PresetManager::getSavedFilesDirectory().getChildFile(
        juce::SHA256(data.getData(), data.getSize()).toHexString().substring(0, 32));
    const auto file = dir.getChildFile(name);
    if (!dir.createDirectory())
      continue;
    if (file.getSize() != (juce::int64)data.getSize() &&
        !file.replaceWithData(data.getData(), data.getSize()))
      continue;
    const auto attribution = carried->getStringAttribute("attribution").substring(0, 4000);
    if (attribution.isNotEmpty())
      dir.getChildFile(PresetManager::attributionFileName).replaceWithText(attribution);
    preset.setAttribute(role, file.getFullPathName());
  }
}
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
  if (userPresetDirectoryOverride != juce::File())
    return userPresetDirectoryOverride;
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("Presets");
}

juce::File PresetManager::getSavedFilesDirectory() {
  return getUserPresetDirectory().getChildFile("Files");
}

void PresetManager::setUserPresetDirectoryForTesting(const juce::File &dir) {
  userPresetDirectoryOverride = dir;
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
  restoreCarriedFiles(*xml);
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

void PresetManager::apply(const juce::XmlElement &saved) {
  juce::XmlElement preset(saved);
  params::upgradeLegacyEq(preset, paramTag);
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

bool PresetManager::saveUserPreset(const juce::String &rawName, juce::String &error,
                                   juce::String *warning) {
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
  const auto preset = capture(name);
  juce::StringArray left;
  for (const auto *role : {"model", "ir"})
    if (const auto why = carryFile(*preset, role); why.isNotEmpty())
      left.add(why);
  if (!preset->writeTo(file)) {
    error = "Could not write " + file.getFullPathName();
    return false;
  }
  // The settings are still worth keeping, and the file may be back.
  if (warning != nullptr && !left.isEmpty())
    *warning = "It doesn't carry its " + left.joinIntoString(" or its ") +
               ", so it only sounds right where that file can be found.";
  refreshUserPresets();
  currentIndex = factory.size() + userFiles.indexOf(file);
  return true;
}
} // namespace evil
