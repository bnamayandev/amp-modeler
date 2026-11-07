#include "Session.h"

namespace evil::agent {
namespace {
void openInBrowser(const juce::URL &url) {
  juce::MessageManager::callAsync([url] { url.launchInDefaultBrowser(); });
}

juce::String shortCount(int tokens) {
  return tokens < 1000 ? juce::String(tokens)
                       : juce::String(tokens / 1000.0, 1) + "k";
}

void merge(std::vector<Source> &into, const std::vector<Source> &more) {
  for (const auto &source : more) {
    bool known = false;
    for (const auto &existing : into)
      known = known || existing.url == source.url;
    if (!known)
      into.push_back(source);
  }
}
} // namespace

Session::Session(AmpControl &a, AgentSettings &s, std::unique_ptr<HttpClient> client,
                 Tone3000::OpenBrowser opener)
    : juce::Thread("Evil Amplifier agent"), amp(a), settings(s),
      http(std::move(client)), customOpener(std::move(opener)),
      tone3000(*http, settings,
               [this](const juce::URL &url) {
                 // The link is a fallback if the browser doesn't come up.
                 note("opening TONE3000's catalog in your browser. Pick a tone, or "
                      "type here to carry on without one",
                      {{"open the catalog", url.toString(true)}});
                 setStatus("waiting for your pick");
                 if (customOpener != nullptr)
                   customOpener(url);
                 else
                   openInBrowser(url);
               },
               [this](const Tone3000::SignInCode &code) {
                 {
                   std::lock_guard lock(mutex);
                   signInCode = code;
                 }
                 note("to use TONE3000, sign in once: enter " + code.userCode + " at " +
                          code.verificationUri.fromFirstOccurrenceOf("://", false, false) +
                          ", on your phone or any browser. This carries on by itself",
                      {{"open it here", code.linkWithCode}});
                 setStatus("waiting for the code");
               }),
      tools(amp, tone3000,
            [this](const juce::String &text, const std::vector<Source> &links) {
              note(text, links);
            },
            [this] { return cancelled(); }) {}

Session::~Session() {
  signalThreadShouldExit();
  stop();
  notify();
  stopThread(10000);
}

bool Session::post(std::function<void()> job) {
  if (busy.exchange(true))
    return false;
  cancelRequested = false;
  http->setAborted(false);
  {
    std::lock_guard lock(mutex);
    pendingJob = std::move(job);
    busySince = juce::Time::getMillisecondCounterHiRes();
  }
  if (!isThreadRunning())
    startThread(juce::Thread::Priority::low); // never competes with audio
  else
    notify();
  return true;
}

bool Session::send(const juce::String &rawText) {
  const auto text = rawText.trim();
  if (text.isEmpty())
    return false;
  if (interruptible.load()) { // they've moved on
    stop();
    waitUntilIdle(2000);
  }
  return post([this, text] {
    Entry entry;
    entry.kind = Entry::Kind::user;
    entry.text = text;
    add(std::move(entry));
    runTurn(text);
  });
}

bool Session::connectTone3000() {
  return post([this] {
    interruptible = true;
    tools.allowSignInPrompt();
    const auto error =
        tone3000.signInWithCode([this] { return cancelled(); });
    interruptible = false;
    {
      std::lock_guard lock(mutex);
      signInCode = {};
    }
    if (error.isEmpty()) {
      note("signed in to TONE3000. It stays signed in from here", {});
    } else if (error != "cancelled") {
      Entry entry;
      entry.kind = Entry::Kind::error;
      entry.text = "TONE3000: " + error + ".";
      add(std::move(entry));
    }
  });
}

bool Session::browseTone3000() {
  return post([this] {
    interruptible = true;
    const auto pick =
        tone3000.browseCatalog([this] { return cancelled(); });
    interruptible = false;
    if (!pick.connected) {
      if (pick.error != "cancelled") {
        Entry entry;
        entry.kind = Entry::Kind::error;
        entry.text = "TONE3000: " + pick.error + ".";
        add(std::move(entry));
      }
      return;
    }
    if (pick.toneId.isEmpty()) // closed without picking
      return;
    const auto text = "I picked this tone on TONE3000: " + pick.toneId +
                      ". Load it and set the amp up around it.";
    Entry entry;
    entry.kind = Entry::Kind::user;
    entry.text = text;
    add(std::move(entry));
    runTurn(text);
  });
}

void Session::disconnectTone3000() { tone3000.disconnect(); }

void Session::stop() {
  cancelRequested = true;
  http->setAborted(true);
  notify(); // out of any wait between retries
}

bool Session::cancelled() const {
  return cancelRequested.load() || threadShouldExit();
}

void Session::newChat() {
  if (busy.load())
    return;
  resetRequested = true;
  {
    std::lock_guard lock(mutex);
    entries.clear();
    lastUsage = {};
  }
  sendChangeMessage();
}

void Session::revert(size_t index) {
  std::shared_ptr<const juce::XmlElement> before;
  {
    std::lock_guard lock(mutex);
    if (index >= entries.size() || entries[index].before == nullptr)
      return;
    before = entries[index].before;
  }
  // On the worker, as it waits for the old model to come back.
  post([this, index, before] {
    setStatus("going back");
    amp.restoreState(*before);
    {
      std::lock_guard lock(mutex);
      entries[index].reverted = true;
    }
    note("went back to the sound from before that", {});
  });
}

std::vector<Session::Entry> Session::getEntries() const {
  std::lock_guard lock(mutex);
  return entries;
}

Tone3000::SignInCode Session::getSignInCode() const {
  std::lock_guard lock(mutex);
  return signInCode;
}

juce::String Session::getStatus() const {
  std::lock_guard lock(mutex);
  return status;
}

double Session::getBusySeconds() const {
  std::lock_guard lock(mutex);
  return busy.load()
             ? (juce::Time::getMillisecondCounterHiRes() - busySince) / 1000.0
             : 0.0;
}

juce::String Session::getLastUsage() const {
  std::lock_guard lock(mutex);
  return lastUsage;
}

bool Session::waitUntilIdle(int timeoutMs) const {
  const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
  while (busy.load()) {
    if (juce::Time::getMillisecondCounter() > deadline)
      return false;
    juce::Thread::sleep(5);
  }
  return true;
}

void Session::showForSnapshot(std::vector<Entry> newEntries,
                              Tone3000::SignInCode code) {
  {
    std::lock_guard lock(mutex);
    entries = std::move(newEntries);
    signInCode = std::move(code);
  }
  sendChangeMessage();
}

void Session::add(Entry entry) {
  if (logger != nullptr)
    logger("Agent " +
           juce::String(entry.kind == Entry::Kind::user        ? "you: "
                        : entry.kind == Entry::Kind::assistant ? "says: "
                        : entry.kind == Entry::Kind::error     ? "problem: "
                                                               : "did: ") +
           entry.text.replaceCharacters("\n", " "));
  {
    std::lock_guard lock(mutex);
    entries.push_back(std::move(entry));
  }
  sendChangeMessage();
}

void Session::note(const juce::String &text, const std::vector<Source> &links) {
  Entry entry;
  entry.kind = Entry::Kind::activity;
  entry.text = text;
  entry.sources = links;
  add(std::move(entry));
}

void Session::setStatus(const juce::String &text) {
  {
    std::lock_guard lock(mutex);
    status = text;
  }
  sendChangeMessage();
}

void Session::run() {
  while (!threadShouldExit()) {
    std::function<void()> job;
    {
      std::lock_guard lock(mutex);
      std::swap(job, pendingJob);
    }
    if (job == nullptr) {
      wait(-1);
      continue;
    }
    job();
    setStatus({});
    busy = false;
    sendChangeMessage();
  }
}

juce::String Session::earlierConversation() const {
  constexpr int maxEntries = 12, maxLength = 1000;
  juce::StringArray lines;
  for (const auto &entry : getEntries()) {
    if (entry.kind == Entry::Kind::user)
      lines.add("Player: " + entry.text.substring(0, maxLength));
    else if (entry.kind == Entry::Kind::assistant)
      lines.add("You: " + entry.text.substring(0, maxLength));
  }
  lines.removeRange(lines.size() - 1, 1); // the message being sent now
  if (lines.isEmpty())
    return {};
  lines.removeRange(0, juce::jmax(0, lines.size() - maxEntries));
  return "[Earlier in this chat, for context:\n" + lines.joinIntoString("\n") +
         "]\n\n";
}

void Session::prepareProvider(ProviderId id, const juce::String &apiKey) {
  provider = makeProvider(
      id, {apiKey, settings.getModel(id), buildSystemPrompt(amp),
           ToolBox::specs(), settings.get(AgentSettings::testBaseUrl)});
  activeProvider = id;
  activeModel = settings.getModel(id);
  threadIsSound = true;
}

ModelReply Session::askWithRetries() {
  // One short wait, then a longer one: long enough for a brief overload to pass,
  // short enough that nobody sits wondering.
  constexpr int waitsMs[] = {2000, 6000};
  ModelReply reply;
  for (int attempt = 0;; ++attempt) {
    reply = provider->parseResponse(http->send(provider->buildRequest()));
    if (reply.error.isEmpty() || !reply.retryable || cancelled() ||
        attempt >= (int)std::size(waitsMs))
      return reply;
    setStatus("no luck, trying again");
    wait(waitsMs[attempt]);
    if (cancelled())
      return reply;
    setStatus("thinking");
  }
}

void Session::runTurn(const juce::String &text) {
  const auto id = settings.getProvider();
  const auto apiKey = settings.getApiKey(id);
  if (apiKey.isEmpty()) {
    Entry entry;
    entry.kind = Entry::Kind::error;
    entry.text = "Add your " + AgentSettings::providerName(id) +
                 " API key under keys, and send this again.";
    add(std::move(entry));
    return;
  }

  // A turn that failed part-way can leave the provider's side expecting tool
  // results that will never come. Rather than patch it up, start its side over
  // with the chat so far as text.
  const bool newChatStarted = resetRequested.exchange(false);
  if (newChatStarted)
    tools.allowSignInPrompt();
  const bool fresh = provider == nullptr || !threadIsSound || activeProvider != id ||
                     activeModel != settings.getModel(id) || newChatStarted;
  juce::String message;
  if (fresh) {
    prepareProvider(id, apiKey);
    message << earlierConversation();
  }
  message << text << "\n\n" << describeAmpState(amp);
  provider->addUserMessage(message);

  const std::shared_ptr<const juce::XmlElement> before = amp.captureState();
  bool changedAmp = false, finished = false;
  std::vector<Source> sources, suggestions;
  int inputTokens = 0, outputTokens = 0, searches = 0;
  provider->allowSearching();
  threadIsSound = false; // until this turn gets all the way through

  const auto problem = [this](const juce::String &what) {
    Entry entry;
    entry.kind = Entry::Kind::error;
    entry.text = what;
    add(std::move(entry));
  };

  setStatus("thinking");
  for (int step = 0; step < maxStepsPerTurn && !finished; ++step) {
    const auto reply = askWithRetries();
    if (cancelled())
      break;
    if (reply.error.isNotEmpty()) {
      problem(reply.error);
      break;
    }

    inputTokens += reply.inputTokens;
    outputTokens += reply.outputTokens;
    merge(sources, reply.sources);
    merge(suggestions, reply.searchSuggestions);
    for (const auto &query : reply.searchQueries)
      note("searched the web: " + query, {});
    searches += reply.searchQueries.size();
    if (searches >= maxSearchesPerTurn)
      provider->stopSearching();

    const auto words = tidyForDisplay(reply.text);
    if (reply.shouldResume) {
      setStatus("searching");
      continue;
    }
    if (reply.toolCalls.empty()) {
      if (words.isEmpty()) {
        problem("The model finished without saying anything. Try again?");
        break;
      }
      Entry entry;
      entry.kind = Entry::Kind::assistant;
      entry.text = words;
      entry.sources = sources;
      entry.suggestions = suggestions;
      // Google's terms allow up to five with an answer; the latest are nearest it.
      if (entry.suggestions.size() > 5)
        entry.suggestions.erase(entry.suggestions.begin(), entry.suggestions.end() - 5);
      if (changedAmp)
        entry.before = before;
      add(std::move(entry));
      finished = true;
      break;
    }
    if (words.isNotEmpty()) { // said something on the way to its next step
      Entry entry;
      entry.kind = Entry::Kind::assistant;
      entry.text = words;
      add(std::move(entry));
    }

    std::vector<ToolResult> results;
    for (const auto &call : reply.toolCalls) {
      if (cancelled())
        break;
      setStatus("working on the amp");
      auto outcome = tools.execute(call);
      {
        std::lock_guard lock(mutex);
        signInCode = {}; // if it asked for one, it's over by now
      }
      changedAmp = changedAmp || outcome.changedAmp;
      results.push_back(std::move(outcome.result));
    }
    if (cancelled())
      break;
    if (step >= maxStepsPerTurn - wrapUpSteps && !results.empty()) {
      // Better a good answer about what's set than nothing at all.
      auto last = juce::JSON::parse(results.back().content);
      if (auto *object = last.getDynamicObject()) {
        object->setProperty("note_from_the_amp",
                            "You're almost out of steps for this message. Stop searching "
                            "and adjusting, and give your answer now about the amp as "
                            "it's set.");
        results.back().content = juce::JSON::toString(last, true);
      }
    }
    provider->addToolResults(results);
    setStatus("thinking");
  }

  threadIsSound = finished;
  if (!finished) {
    // Stopped, failed or ran out of steps with the amp changed: revert still has
    // to be possible.
    Entry entry;
    entry.kind = Entry::Kind::activity;
    entry.text = cancelled() ? "stopped" : "that didn't get all the way through";
    if (changedAmp)
      entry.before = before;
    add(std::move(entry));
  }

  std::lock_guard lock(mutex);
  lastUsage = shortCount(inputTokens) + " tokens in, " + shortCount(outputTokens) + " out";
}
} // namespace evil::agent
