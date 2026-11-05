#include "Http.h"

#include <array>

namespace evil::agent {
namespace {
/** JUCE dereferences a null pointer if libcurl can't be loaded, so check first.
    The names are the ones JUCE's lazy loader tries. */
bool networkAvailable() {
#if JUCE_LINUX && JUCE_LOAD_CURL_SYMBOLS_LAZILY
  static const bool available = [] {
    juce::DynamicLibrary library;
    for (const auto *name : {"libcurl.so", "libcurl.so.4", "libcurl.so.3",
                             "libcurl-gnutls.so.4", "libcurl-gnutls.so.3"})
      if (library.open(name))
        return true;
    return false;
  }();
  return available;
#else
  return true;
#endif
}

const juce::String cancelledMessage{"cancelled"};
} // namespace

HttpResponse WebHttpClient::send(const HttpRequest &request) {
  HttpResponse response;
  if (!networkAvailable()) {
    response.error = "libcurl isn't installed, and the agent needs it for HTTPS "
                     "(on Debian or Ubuntu: sudo apt install libcurl4)";
    return response;
  }

  juce::URL url(request.url);
  const bool post = request.method == "POST";
  if (post)
    url = url.withPOSTData(request.body);

  juce::WebInputStream stream(url, post);
  juce::String headers;
  for (const auto &key : request.headers.getAllKeys())
    headers << key << ": " << request.headers[key] << "\r\n";
  stream.withExtraHeaders(headers)
      .withConnectionTimeout(request.timeoutMs)
      .withNumRedirectsToFollow(5);

  {
    std::lock_guard lock(mutex);
    if (aborted) {
      response.error = cancelledMessage;
      return response;
    }
    current = &stream;
  }
  const juce::ScopeGuard forget{[this] {
    std::lock_guard lock(mutex);
    current = nullptr;
  }};

  const bool connected = stream.connect(nullptr);
  response.status = stream.getStatusCode();
  const auto wasAborted = [this] {
    std::lock_guard lock(mutex);
    return aborted;
  };
  if (!connected || response.status == 0) {
    response.status = 0;
    response.error = wasAborted()
                         ? cancelledMessage
                         : "no connection to " + url.getDomain();
    return response;
  }

  std::array<char, 16384> chunk;
  juce::MemoryOutputStream out(response.body, false);
  while (!stream.isExhausted()) {
    const int n = stream.read(chunk.data(), (int)chunk.size());
    if (n <= 0)
      break;
    out.write(chunk.data(), (size_t)n);
    if ((juce::int64)out.getDataSize() > request.maxResponseBytes) {
      response.error = "the response was larger than expected";
      break;
    }
    if (wasAborted()) {
      response.error = cancelledMessage;
      break;
    }
  }
  out.flush();
  if (response.error.isEmpty() && stream.isError())
    response.error = wasAborted() ? cancelledMessage
                                  : "the connection dropped mid-response";
  return response;
}

void WebHttpClient::setAborted(bool shouldAbort) {
  std::lock_guard lock(mutex);
  aborted = shouldAbort;
  if (shouldAbort && current != nullptr)
    current->cancel();
}
} // namespace evil::agent
