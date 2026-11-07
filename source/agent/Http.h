#pragma once

#include <juce_core/juce_core.h>

#include <mutex>

namespace evil::agent {
struct HttpRequest {
  juce::String method{"GET"};
  juce::String url; // absolute, including any query
  juce::StringPairArray headers;
  juce::String body;
  // On Linux this also limits how long a response may stall, so it has to
  // cover the time a model takes to think and search.
  int timeoutMs = 30000;
  juce::int64 maxResponseBytes = 8 * 1024 * 1024;
};

struct HttpResponse {
  int status = 0;      // 0 if no response arrived
  juce::String error;  // why not, or why the body is incomplete
  juce::MemoryBlock body;

  bool ok() const noexcept {
    return status >= 200 && status < 300 && error.isEmpty();
  }
  juce::String text() const { return body.toString(); }
};

/** Everything the agent sends goes through this, so tests can script replies. */
class HttpClient {
public:
  virtual ~HttpClient() = default;

  /** Blocks until the exchange finishes, fails or is aborted. */
  virtual HttpResponse send(const HttpRequest &) = 0;

  /** Any thread. While set, send() fails at once and any send in progress is
      cut off. */
  virtual void setAborted(bool) {}
};

/** HTTPS through JUCE: libcurl on Linux, the system's stack elsewhere. */
class WebHttpClient final : public HttpClient {
public:
  HttpResponse send(const HttpRequest &) override;
  void setAborted(bool) override;

private:
  std::mutex mutex;
  juce::WebInputStream *current = nullptr;
  bool aborted = false;
};
} // namespace evil::agent
