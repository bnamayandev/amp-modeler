#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <mutex>

namespace evil::agent {
enum class ProviderId { anthropic, openai, gemini };

/** The agent's preferences and secrets: LLM API keys, the TONE3000 key and
    tokens, and which provider and model to use.

    Kept in their own file, readable only by this user on Linux and macOS and
    in the per-user %APPDATA% on Windows. Nothing here goes into presets or a
    host's session, because those get shared. Thread safe, and shared by every
    instance through a SharedResourcePointer. Broadcasts when a value changes. */
class AgentSettings final : public juce::ChangeBroadcaster {
public:
  AgentSettings();

  static juce::File getDefaultFile();
  /** Moves to another file and reloads, for tests and evil-snapshot. */
  void useFile(const juce::File &);

  juce::String get(const juce::String &key) const;
  void set(const juce::String &key, const juce::String &value);
  /** Applies until the process exits and is never written out, for evil-cli. */
  void setForThisRun(const juce::String &key, const juce::String &value);
  /** Picks up what another process has saved: the standalone, a DAW and evil-cli
      all share this file. */
  void reloadIfChanged();

  ProviderId getProvider() const;
  void setProvider(ProviderId);
  juce::String getModel(ProviderId) const;
  void setModel(ProviderId, const juce::String &);
  juce::String getApiKey(ProviderId) const;
  void setApiKey(ProviderId, const juce::String &);

  /** evil-cli only: where to send model requests instead of the provider. */
  static constexpr const char *testBaseUrl = "test_base_url";

  static juce::String providerName(ProviderId);
  static juce::String apiKeyName(ProviderId);
  /** Suggested models, default first. The model box also takes any other name,
      as these lists date quickly. */
  static juce::StringArray suggestedModels(ProviderId);

  // TONE3000
  static constexpr const char *tone3000Key = "tone3000_publishable_key";
  static constexpr const char *tone3000Access = "tone3000_access_token";
  static constexpr const char *tone3000Refresh = "tone3000_refresh_token";
  static constexpr const char *tone3000Expiry = "tone3000_expires_at_ms";

private:
  void load();
  bool save() const;

  mutable std::mutex mutex;
  juce::File file;
  juce::String loadedText; // compared, as file times only resolve to a second
  std::map<juce::String, juce::String> values, forThisRun;
};
} // namespace evil::agent
