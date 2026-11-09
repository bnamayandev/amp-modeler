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

constexpr const char *wrapUpNote =
    "You're almost out of steps for this message. Stop searching and adjusting, "
    "and give your answer now about the amp as it's set.";

/** One thing a turn did, kept so it can carry on in a fresh conversation. */
struct WorkDone {
  juce::String what;
  juce::String result; // a tool's, as the model saw it; empty for anything else
};

/** The opening message of a fresh conversation that picks a turn up where it
    got to. A search's pages can't be carried over, as they're the provider's,
    but everything that came back from the amp and TONE3000 can. Without the
    results it's much shorter, in case the length was what was turned down. */
juce::String carryOn(const juce::String &playerMessage, const std::vector<WorkDone> &work,
                     const std::vector<Source> &sources, const juce::String &why,
                     int searchesLeft, bool withResults) {
  constexpr int maxResultLength = 3000, maxResultsLength = 40000, maxSources = 15;

  // The latest results matter most, so the length budget goes to them first.
  std::vector<juce::String> results(work.size());
  int budget = withResults ? maxResultsLength : 0;
  for (size_t i = work.size(); i-- > 0;) {
    auto result = work[i].result;
    if (result.isEmpty() || budget <= 0)
      continue;
    if (result.length() > maxResultLength)
      result = result.substring(0, maxResultLength) + " [...]";
    budget -= result.length();
    results[i] = result;
  }

  juce::String text;
  text << "[Carrying on. Part-way through this message the provider turned down a "
          "request (\""
       << why
       << "\"), so this is a fresh conversation. Everything you did for this message "
          "so far is listed below and still stands: the amp is set as described at "
          "the end. Pick up from there. Don't call reset_controls or start the tone "
          "again, and don't repeat a tool call whose result is here. The pages your "
          "searches found couldn't be carried over, only what you searched for and "
          "what you cited, so search again only for something you still need. ";
  if (searchesLeft > 0)
    text << "You have " << searchesLeft << " web searches left for this message.";
  else
    text << "You have no web searches left for this message.";
  text << "]\n\nThe player's message:\n" << playerMessage << "\n\n";

  if (work.empty()) {
    text << "You hadn't done anything for it yet.\n";
  } else {
    text << "What you've done for it so far, in order:\n";
    for (size_t i = 0; i < work.size(); ++i) {
      text << juce::String((int)i + 1) << ". " << work[i].what;
      if (results[i].isNotEmpty())
        text << " It returned: " << results[i];
      else if (work[i].result.isNotEmpty())
        text << " (Its result is left out, for length.)";
      text << "\n";
    }
  }
  if (!sources.empty()) {
    text << "\nSources you cited:\n";
    for (size_t i = 0; i < sources.size() && i < (size_t)maxSources; ++i)
      text << "- " << sources[i].title << ": " << sources[i].url << "\n";
  }
  return text;
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
    const auto request = provider->buildRequest();
    const auto response = http->send(request);
    reply = provider->parseResponse(response);
    if (reply.badRequest)
      logTurnedDown(request, response);
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

void Session::logTurnedDown(const HttpRequest &request,
                            const HttpResponse &response) const {
  if (logger == nullptr)
    return;
  // The error says what was wrong; the end of the body is the part that was new.
  constexpr int maxLength = 4000;
  const auto apiKey = settings.getApiKey(activeProvider);
  const auto tidy = [&apiKey](juce::String text) {
    if (apiKey.isNotEmpty())
      text = text.replace(apiKey, "[key]");
    return text.replaceCharacters("\r\n", "  ");
  };
  logger("Agent request turned down: HTTP " + juce::String(response.status) + " " +
         tidy(response.text().substring(0, maxLength)) +
         " | the end of what was sent: " +
         tidy(request.body.getLastCharacters(maxLength)));
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
  const auto earlier = earlierConversation(); // before this turn adds to the chat
  juce::String message;
  if (fresh) {
    prepareProvider(id, apiKey);
    message << earlier;
  }
  message << text << "\n\n" << describeAmpState(amp);
  provider->addUserMessage(message);

  const std::shared_ptr<const juce::XmlElement> before = amp.captureState();
  bool changedAmp = false, finished = false;
  std::vector<Source> sources, suggestions;
  std::vector<WorkDone> work;
  int inputTokens = 0, outputTokens = 0, searches = 0, freshStarts = 0;
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
    if (reply.error.isNotEmpty() && reply.badRequest &&
        freshStarts < maxFreshStartsPerTurn) {
      // Whatever it objected to is in that conversation, so leave it behind and
      // carry on in a new one with the work so far, rather than stop half-done.
      ++freshStarts;
      note(reply.error + " Carrying on from where it got to, in a fresh conversation",
           {});
      prepareProvider(id, apiKey);
      threadIsSound = false;
      auto restart = earlier + carryOn(text, work, sources, reply.error,
                                       juce::jmax(0, maxSearchesPerTurn - searches),
                                       freshStarts == 1);
      if (step >= maxStepsPerTurn - wrapUpSteps) // as it would be on a tool result
        restart << "\n" << wrapUpNote << "\n";
      provider->addUserMessage(restart + "\n" + describeAmpState(amp));
      if (searches >= maxSearchesPerTurn)
        provider->stopSearching();
      setStatus("thinking");
      continue;
    }
    if (reply.error.isNotEmpty()) {
      problem(reply.error);
      break;
    }

    inputTokens += reply.inputTokens;
    outputTokens += reply.outputTokens;
    merge(sources, reply.sources);
    merge(suggestions, reply.searchSuggestions);
    for (const auto &query : reply.searchQueries) {
      note("searched the web: " + query, {});
      work.push_back({"You searched the web for: " + query, {}});
    }
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
      work.push_back({"You told the player: " + words, {}});
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
      work.push_back({"You called " + call.name + " with " +
                          juce::JSON::toString(call.arguments, true) + ".",
                      outcome.result.content});
      results.push_back(std::move(outcome.result));
    }
    if (cancelled())
      break;
    if (step >= maxStepsPerTurn - wrapUpSteps && !results.empty()) {
      // Better a good answer about what's set than nothing at all.
      auto last = juce::JSON::parse(results.back().content);
      if (auto *object = last.getDynamicObject()) {
        object->setProperty("note_from_the_amp", wrapUpNote);
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

  {
    std::lock_guard lock(mutex);
    lastUsage = shortCount(inputTokens) + " tokens in, " + shortCount(outputTokens) + " out";
  }
  if (finished && onAnswered != nullptr)
    onAnswered();
}
} // namespace evil::agent
