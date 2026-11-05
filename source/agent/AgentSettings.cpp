#include "AgentSettings.h"

#if !JUCE_WINDOWS
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace evil::agent {
namespace {
constexpr const char *rootTag = "AgentSettings";
constexpr const char *valueTag = "Value";

const char *providerKey(ProviderId id) {
  switch (id) {
  case ProviderId::anthropic:
    return "anthropic";
  case ProviderId::openai:
    return "openai";
  case ProviderId::gemini:
    return "gemini";
  }
  return "anthropic";
}

/** Replaces the file in one step, and at no point is any of it readable by
    other users, including a half-written temporary. */
bool writePrivately(const juce::File &file, const juce::String &text) {
  file.getParentDirectory().createDirectory();
#if JUCE_WINDOWS
  return file.replaceWithText(text); // %APPDATA% is already per-user
#else
  const auto temporary =
      file.getSiblingFile(file.getFileName() + ".tmp" +
                          juce::String(juce::Random::getSystemRandom().nextInt64()));
  const auto fd = ::open(temporary.getFullPathName().toRawUTF8(),
                         O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
  if (fd < 0)
    return false;
  const auto *data = text.toRawUTF8();
  const auto size = (ssize_t)text.getNumBytesAsUTF8();
  const bool written = ::write(fd, data, (size_t)size) == size;
  const bool closed = ::close(fd) == 0;
  if (!written || !closed ||
      ::rename(temporary.getFullPathName().toRawUTF8(),
               file.getFullPathName().toRawUTF8()) != 0) {
    temporary.deleteFile();
    return false;
  }
  return true;
#endif
}
} // namespace

AgentSettings::AgentSettings() : file(getDefaultFile()) { load(); }

juce::File AgentSettings::getDefaultFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("agent.xml");
}

void AgentSettings::useFile(const juce::File &newFile) {
  {
    std::lock_guard lock(mutex);
    file = newFile;
    values.clear();
    forThisRun.clear();
    load();
  }
  sendChangeMessage();
}

void AgentSettings::load() {
  values.clear();
  loadedText = file.loadFileAsString();
  // A missing or damaged file just means starting with no keys.
  const auto xml = juce::parseXML(loadedText);
  if (xml == nullptr || !xml->hasTagName(rootTag))
    return;
  for (const auto *e : xml->getChildWithTagNameIterator(valueTag))
    values[e->getStringAttribute("key")] = e->getStringAttribute("value");
}

bool AgentSettings::save() const {
  juce::XmlElement xml(rootTag);
  for (const auto &[key, value] : values) {
    auto *e = xml.createNewChildElement(valueTag);
    e->setAttribute("key", key);
    e->setAttribute("value", value);
  }
  return writePrivately(file, xml.toString());
}

void AgentSettings::reloadIfChanged() {
  bool changed = false;
  {
    std::lock_guard lock(mutex);
    if (file.loadFileAsString() != loadedText) {
      load();
      changed = true;
    }
  }
  if (changed)
    sendChangeMessage();
}

juce::String AgentSettings::get(const juce::String &key) const {
  std::lock_guard lock(mutex);
  if (const auto it = forThisRun.find(key); it != forThisRun.end())
    return it->second;
  if (const auto it = values.find(key); it != values.end())
    return it->second;
  return {};
}

void AgentSettings::set(const juce::String &key, const juce::String &value) {
  {
    std::lock_guard lock(mutex);
    load(); // or this would overwrite whatever another process saved since
    if (value.isEmpty())
      values.erase(key);
    else
      values[key] = value;
    if (save())
      loadedText = file.loadFileAsString();
  }
  sendChangeMessage();
}

void AgentSettings::setForThisRun(const juce::String &key,
                                  const juce::String &value) {
  std::lock_guard lock(mutex);
  forThisRun[key] = value;
}

ProviderId AgentSettings::getProvider() const {
  const auto name = get("provider");
  if (name == providerKey(ProviderId::openai))
    return ProviderId::openai;
  if (name == providerKey(ProviderId::gemini))
    return ProviderId::gemini;
  return ProviderId::anthropic;
}

void AgentSettings::setProvider(ProviderId id) {
  set("provider", providerKey(id));
}

juce::String AgentSettings::getModel(ProviderId id) const {
  const auto model = get(juce::String("model_") + providerKey(id));
  return model.isNotEmpty() ? model : suggestedModels(id)[0];
}

void AgentSettings::setModel(ProviderId id, const juce::String &model) {
  set(juce::String("model_") + providerKey(id), model.trim());
}

juce::String AgentSettings::apiKeyName(ProviderId id) {
  return juce::String("api_key_") + providerKey(id);
}

juce::String AgentSettings::getApiKey(ProviderId id) const {
  return get(apiKeyName(id));
}

void AgentSettings::setApiKey(ProviderId id, const juce::String &key) {
  set(apiKeyName(id), key.trim());
}

juce::String AgentSettings::providerName(ProviderId id) {
  switch (id) {
  case ProviderId::anthropic:
    return "Claude";
  case ProviderId::openai:
    return "ChatGPT";
  case ProviderId::gemini:
    return "Gemini";
  }
  return {};
}

juce::StringArray AgentSettings::suggestedModels(ProviderId id) {
  switch (id) {
  case ProviderId::anthropic:
    return {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5"};
  case ProviderId::openai:
    return {"gpt-6.1-sol", "gpt-6-astra", "gpt-6.1-terra", "gpt-6-luna"};
  case ProviderId::gemini:
    return {"gemini-3.8-flash", "gemini-4-argon", "gemini-3.6-flash",
            "gemini-3.5-flash-lite"};
  }
  return {};
}
} // namespace evil::agent
