#pragma once

#include "AgentSettings.h"
#include "Http.h"

#include <memory>
#include <vector>

namespace evil::agent {
struct ToolSpec {
  juce::String name, description;
  juce::var schema; // JSON Schema for the arguments
};

struct ToolCall {
  juce::String id, name;
  juce::var arguments;
};

struct ToolResult {
  juce::String callId, name;
  juce::String content; // JSON text
  bool isError = false;
};

struct Source {
  juce::String title, url;
};

/** What one request to a model came back with. */
struct ModelReply {
  juce::String text;
  std::vector<ToolCall> toolCalls;
  std::vector<Source> sources;           // the pages the answer cites
  std::vector<Source> searchSuggestions; // Google's, which must be shown
  juce::StringArray searchQueries;
  bool shouldResume = false; // the model paused: send the same thread again
  int inputTokens = 0, outputTokens = 0;
  juce::String error; // set if this reply can't be used
  bool retryable = false;
  /** The API turned the request itself down (a 400 or the like, not the key or
      the model): sending it again won't help, but the same work in a new
      conversation, shaped differently, may get past it. */
  bool badRequest = false;
};

/** One provider's API and one conversation with it.

    A conversation's history has to go back in exactly the provider's own shape,
    including signed thinking and search results we can't read, so each
    implementation holds its own and hands back only what's common to all three.
    Not thread safe; the session's worker thread owns it. */
class Provider {
public:
  struct Config {
    juce::String apiKey, model, systemPrompt;
    std::vector<ToolSpec> tools;
    juce::String baseUrl; // empty for the provider's own; evil-cli can point it
                          // at a local stand-in, for testing without a key
  };

  virtual ~Provider() = default;

  virtual void addUserMessage(const juce::String &) = 0;
  virtual void addToolResults(const std::vector<ToolResult> &) = 0;
  /** Takes web search away from the model for the rest of this conversation's
      requests, as it's the one thing a model can keep spending on by itself. */
  virtual void stopSearching() = 0;
  /** Gives it back, for the next message. */
  virtual void allowSearching() = 0;
  /** The next request, from the conversation so far. */
  virtual HttpRequest buildRequest() const = 0;
  /** Reads the response to that request and adds the model's turn to the
      conversation. */
  virtual ModelReply parseResponse(const HttpResponse &) = 0;
};

std::unique_ptr<Provider> makeProvider(ProviderId, Provider::Config);

/** Links Google's Search Suggestions HTML offers, as title and URL. The build
    has no web view to render the snippet itself. */
std::vector<Source> parseSearchSuggestions(const juce::String &html);

/** Only these are opened in a browser. A link can come from a web page by way
    of a model, and other schemes can run things or read local files. */
bool isSafeLink(const juce::String &url);
} // namespace evil::agent
