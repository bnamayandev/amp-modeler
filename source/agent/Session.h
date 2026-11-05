#pragma once

#include "Tools.h"

#include <atomic>
#include <mutex>

namespace evil::agent {
/** One chat with the agent, for one plugin instance: the transcript, and the
    worker thread that runs the back and forth with the model.

    It starts no thread and opens no connection until the player sends something.
    Everything public is for the message thread, except where noted. Broadcasts
    when the transcript or status changes. */
class Session final : private juce::Thread, public juce::ChangeBroadcaster {
public:
  struct Entry {
    enum class Kind { user, assistant, activity, error };
    Kind kind = Kind::activity;
    juce::String text;
    std::vector<Source> sources;
    std::vector<Source> suggestions; // Google's Search Suggestions
    // The sound before this turn, if the turn changed it: what revert goes to.
    std::shared_ptr<const juce::XmlElement> before;
    bool reverted = false;
  };

  Session(AmpControl &, AgentSettings &, std::unique_ptr<HttpClient>,
          Tone3000::OpenBrowser = {});
  ~Session() override;

  /** False if it's still busy with the last one. Sending while only waiting on
      the player, for a catalog pick or a sign-in, gives up the wait. */
  bool send(const juce::String &text);
  /** Starts the sign-in: getSignInCode() has what to show until it's done. */
  bool connectTone3000();
  /** Opens the TONE3000 catalog in the browser, the one thing that does. If the
      player picks a tone, the agent sets the amp up around it. */
  bool browseTone3000();
  void disconnectTone3000();
  void stop();
  void newChat();
  /** Puts back the sound from before that entry's turn, unless still busy. */
  void revert(size_t entryIndex);

  /** Where to write a record of what the agent does, for looking into a result
      afterwards. Set once, before anything is sent. */
  void setLogger(std::function<void(const juce::String &)> newLogger) {
    logger = std::move(newLogger);
  }

  /** Any thread. */
  std::vector<Entry> getEntries() const;
  bool isBusy() const noexcept { return busy.load(); }
  /** Waiting on the player and nothing else, so a new message can interrupt. */
  bool isOnlyWaitingOnPlayer() const noexcept { return interruptible.load(); }
  /** The code to enter while a sign-in is waiting for one; otherwise empty. */
  Tone3000::SignInCode getSignInCode() const;
  juce::String getStatus() const;
  double getBusySeconds() const;
  /** Tokens the last turn used, since it's the player's money. */
  juce::String getLastUsage() const;

  /** For evil-cli and the tests: blocks until idle or out of time. */
  bool waitUntilIdle(int timeoutMs) const;
  /** Fills the transcript without calling anything, for evil-snapshot. */
  void showForSnapshot(std::vector<Entry>, Tone3000::SignInCode = {});

  static constexpr int maxStepsPerTurn = 24;
  /** How many steps before the end the model is told to wrap up, so it still
      gets to answer. */
  static constexpr int wrapUpSteps = 4;
  /** A message's web searches, which cost the player money on top of the tokens. */
  static constexpr int maxSearchesPerTurn = 15;

private:
  void run() override;
  bool post(std::function<void()> job);
  void runTurn(const juce::String &text);
  ModelReply askWithRetries();
  void prepareProvider(ProviderId, const juce::String &apiKey);
  juce::String earlierConversation() const;
  bool cancelled() const;
  void add(Entry);
  void note(const juce::String &text, const std::vector<Source> &links);
  void setStatus(const juce::String &);

  AmpControl &amp;
  AgentSettings &settings;
  std::unique_ptr<HttpClient> http;
  Tone3000::OpenBrowser customOpener;
  std::function<void(const juce::String &)> logger;
  Tone3000 tone3000;
  ToolBox tools;

  // Worker thread's own.
  std::unique_ptr<Provider> provider;
  ProviderId activeProvider = ProviderId::anthropic;
  juce::String activeModel;
  bool threadIsSound = false; // false: start the provider's thread over

  mutable std::mutex mutex; // guards everything below
  std::vector<Entry> entries;
  std::function<void()> pendingJob;
  juce::String status, lastUsage;
  Tone3000::SignInCode signInCode;
  double busySince = 0.0;

  std::atomic<bool> busy{false}, cancelRequested{false}, resetRequested{false},
      interruptible{false};
};
} // namespace evil::agent
