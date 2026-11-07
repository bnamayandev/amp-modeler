#include "Provider.h"

#include "Json.h"

namespace evil::agent {
namespace {
constexpr int modelTimeoutMs = 5 * 60 * 1000; // searches run inside the request
constexpr int maxOutputTokens = 16000;

void addSource(std::vector<Source> &sources, const juce::String &title,
               const juce::String &url) {
  if (!isSafeLink(url))
    return;
  for (const auto &s : sources)
    if (s.url == url)
      return;
  sources.push_back({title.isNotEmpty() ? title : juce::URL(url).getDomain(), url});
}

juce::String errorMessageIn(const HttpResponse &response) {
  const auto parsed = juce::JSON::parse(response.text());
  // Google sends [{"error": {...}}]. Copied, not assigned over parsed: that
  // would free the array holding what's being assigned from.
  const juce::var root =
      parsed.isArray() && parsed.size() > 0 ? juce::var(parsed[0]) : parsed;
  const auto error = root["error"];
  if (error.isObject() && error["message"].isString())
    return error["message"].toString();
  if (error.isString())
    return error.toString();
  if (root["message"].isString())
    return root["message"].toString();
  return {};
}

/** A failed request as something the player can act on. The key is scrubbed in
    case a server repeats it back. */
ModelReply failure(const juce::String &who, const juce::String &apiKey,
                   const HttpResponse &response) {
  ModelReply reply;
  const auto status = response.status;
  reply.retryable =
      status == 0 || status == 408 || status == 429 || status >= 500;
  auto detail = errorMessageIn(response);
  if (apiKey.isNotEmpty())
    detail = detail.replace(apiKey, "[key]");
  const auto withDetail = [&detail](const juce::String &message) {
    return detail.isNotEmpty() ? message + " (" + detail + ")" : message;
  };

  // Google reports a bad key as a 400.
  const bool badKey = status == 401 || status == 403 ||
                      response.text().contains("API_KEY_INVALID");
  if (status == 0)
    reply.error = "Couldn't reach " + who + ": " + response.error + ".";
  else if (badKey)
    reply.error = withDetail(who + " rejected the API key. Check it under keys.");
  else if (status == 404)
    reply.error = withDetail(who + " doesn't recognise that model.");
  else if (status == 429)
    reply.error = withDetail(who + " says the rate limit or credit has run out.");
  else if (status >= 500)
    reply.error = withDetail(who + " is having trouble right now.");
  else
    reply.error = withDetail(who + " refused the request (HTTP " +
                             juce::String(status) + ").");
  return reply;
}

ModelReply unreadable(const juce::String &who) {
  ModelReply reply;
  reply.error = who + " sent a reply this version can't read.";
  return reply;
}

/** The tools without the search tool, which these providers list first. */
juce::var withoutFirst(const juce::var &tools) {
  auto rest = json::array();
  for (int i = 1; i < tools.size(); ++i)
    rest.append(tools[i]);
  return rest;
}

HttpRequest jsonPost(const Provider::Config &config, const juce::String &origin,
                     const juce::String &path, const juce::var &body) {
  HttpRequest request;
  request.method = "POST";
  request.url = (config.baseUrl.isNotEmpty() ? config.baseUrl : origin) + path;
  request.body = json::dump(body);
  request.headers.set("Content-Type", "application/json");
  request.timeoutMs = modelTimeoutMs;
  return request;
}

// --- Claude: the Messages API ------------------------------------------------
class Anthropic final : public Provider {
public:
  explicit Anthropic(Config c) : config(std::move(c)) {
    tools = json::array();
    for (const auto &t : config.tools)
      tools.append(json::object({{"name", t.name},
                                 {"description", t.description},
                                 {"input_schema", t.schema}}));
    tools.append(json::object(
        {{"type", webSearchType()}, {"name", "web_search"}, {"max_uses", 8}}));
    messages = json::array();
  }

  void addUserMessage(const juce::String &text) override {
    messages.append(json::object({{"role", "user"}, {"content", text}}));
  }

  void addToolResults(const std::vector<ToolResult> &results) override {
    auto content = json::array();
    for (const auto &r : results)
      content.append(json::object({{"type", "tool_result"},
                                   {"tool_use_id", r.callId},
                                   {"content", r.content},
                                   {"is_error", r.isError}}));
    // All results of a turn belong in one message.
    messages.append(json::object({{"role", "user"}, {"content", content}}));
  }

  void stopSearching() override { searchAllowed = false; }
  void allowSearching() override { searchAllowed = true; }

  HttpRequest buildRequest() const override {
    // Its search tool stays declared, as the history has its results in it, and
    // the cap on it drops to the lowest the API allows.
    if (auto *search = tools[tools.size() - 1].getDynamicObject())
      search->setProperty("max_uses", searchAllowed ? 8 : 1);
    auto body = json::object({{"model", config.model},
                              {"max_tokens", maxOutputTokens},
                              {"system", config.systemPrompt},
                              {"tools", tools},
                              {"messages", messages},
                              // Reuses the prompt and history between requests.
                              {"cache_control", json::object({{"type", "ephemeral"}})}});
    if (isCurrentGeneration())
      body.getDynamicObject()->setProperty(
          "output_config", json::object({{"effort", "medium"}}));
    if (hasServerFallback())
      body.getDynamicObject()->setProperty("fallbacks", "default");

    auto request = jsonPost(config, "https://api.anthropic.com", "/v1/messages", body);
    request.headers.set("x-api-key", config.apiKey);
    request.headers.set("anthropic-version", "2026-10-21");
    if (hasServerFallback())
      request.headers.set("anthropic-beta", "server-side-fallback-2026-07-01");
    return request;
  }

  ModelReply parseResponse(const HttpResponse &response) override {
    if (!response.ok())
      return failure("Claude", config.apiKey, response);
    const auto root = juce::JSON::parse(response.text());
    const auto content = root["content"];
    const auto stop = root["stop_reason"].toString();
    if (stop == "refusal") {
      ModelReply reply;
      reply.error = "Claude declined this request. Try rewording it.";
      return reply;
    }
    if (!content.isArray())
      return unreadable("Claude");

    ModelReply reply;
    for (const auto &block : json::items(content)) {
      const auto type = block["type"].toString();
      if (type == "text") {
        reply.text << block["text"].toString();
        for (const auto &citation : json::items(block["citations"]))
          addSource(reply.sources, citation["title"].toString(),
                    citation["url"].toString());
      } else if (type == "tool_use") {
        reply.toolCalls.push_back({block["id"].toString(),
                                   block["name"].toString(), block["input"]});
      } else if (type == "server_tool_use" &&
                 block["name"].toString() == "web_search") {
        reply.searchQueries.addIfNotAlreadyThere(
            block["input"]["query"].toString());
      }
    }
    reply.searchQueries.removeEmptyStrings();

    // Verbatim: thinking and search blocks are only valid unchanged.
    messages.append(json::object({{"role", "assistant"}, {"content", content}}));

    // The server's search loop hit its limit; sending the thread as it is
    // resumes it. No new user message, or the search is lost.
    reply.shouldResume = stop == "pause_turn";
    if (stop == "max_tokens" && reply.toolCalls.empty())
      reply.text << "\n\n[The reply was cut off at the length limit.]";

    const auto usage = root["usage"];
    reply.inputTokens = (int)usage["input_tokens"] +
                        (int)usage["cache_read_input_tokens"] +
                        (int)usage["cache_creation_input_tokens"];
    reply.outputTokens = (int)usage["output_tokens"];
    return reply;
  }

private:
  bool isCurrentGeneration() const {
    return config.model.startsWith("claude-opus-5") ||
           config.model.startsWith("claude-sonnet-5") ||
           config.model.startsWith("claude-haiku-5") ||
           config.model.startsWith("claude-fable-5");
  }
  bool hasServerFallback() const {
    return config.model.startsWith("claude-opus-5") ||
           config.model.startsWith("claude-sonnet-5-5") ||
           config.model.startsWith("claude-fable-5");
  }
  juce::String webSearchType() const {
    // Dynamic filtering is on these; Haiku and older models have the basic tool.
    for (const auto *family : {"claude-opus-5", "claude-opus-4-8", "claude-opus-4-7",
                               "claude-opus-4-6", "claude-sonnet-5", "claude-sonnet-4-6"})
      if (config.model.startsWith(family))
        return "web_search_20260209";
    return "web_search_20250305";
  }

  Config config;
  juce::var tools, messages;
  bool searchAllowed = true;
};

// --- ChatGPT: the Responses API ----------------------------------------------
class OpenAi final : public Provider {
public:
  explicit OpenAi(Config c) : config(std::move(c)) {
    tools = json::array({json::object({{"type", "web_search"}})});
    for (const auto &t : config.tools)
      tools.append(json::object({{"type", "function"},
                                 {"name", t.name},
                                 {"description", t.description},
                                 {"parameters", t.schema},
                                 // Checked when run, so a mistake comes back
                                 // to the model as a result it can fix.
                                 {"strict", false}}));
    input = json::array();
  }

  void addUserMessage(const juce::String &text) override {
    input.append(json::object({{"role", "user"}, {"content", text}}));
  }

  void addToolResults(const std::vector<ToolResult> &results) override {
    for (const auto &r : results)
      input.append(json::object({{"type", "function_call_output"},
                                 {"call_id", r.callId},
                                 {"output", r.content}}));
  }

  void stopSearching() override { searchAllowed = false; }
  void allowSearching() override { searchAllowed = true; }

  HttpRequest buildRequest() const override {
    auto body = json::object({{"model", config.model},
                              {"instructions", config.systemPrompt},
                              {"tools", searchAllowed ? tools : withoutFirst(tools)},
                              {"input", input},
                              {"max_output_tokens", maxOutputTokens}});
    // The server keeps the history, including reasoning we must not lose.
    // Instructions and tools aren't carried over, so they're sent every time.
    if (previousResponse.isNotEmpty())
      body.getDynamicObject()->setProperty("previous_response_id",
                                           previousResponse);
    auto request = jsonPost(config, "https://api.openai.com", "/v1/responses", body);
    request.headers.set("Authorization", "Bearer " + config.apiKey);
    return request;
  }

  ModelReply parseResponse(const HttpResponse &response) override {
    if (!response.ok())
      return failure("ChatGPT", config.apiKey, response);
    const auto root = juce::JSON::parse(response.text());
    const auto status = root["status"].toString();
    if (status == "failed") {
      ModelReply reply;
      reply.error = "ChatGPT couldn't finish: " +
                    root["error"]["message"].toString();
      return reply;
    }
    if (!root["output"].isArray() || root["id"].toString().isEmpty())
      return unreadable("ChatGPT");

    ModelReply reply;
    for (const auto &item : json::items(root["output"])) {
      const auto type = item["type"].toString();
      if (type == "function_call") {
        reply.toolCalls.push_back({item["call_id"].toString(),
                                   item["name"].toString(),
                                   juce::JSON::parse(item["arguments"].toString())});
      } else if (type == "web_search_call") {
        const auto action = item["action"];
        reply.searchQueries.addIfNotAlreadyThere(action["query"].toString());
        for (const auto &query : json::items(action["queries"]))
          reply.searchQueries.addIfNotAlreadyThere(query.toString());
      } else if (type == "message") {
        for (const auto &part : json::items(item["content"])) {
          if (part["type"].toString() == "refusal") {
            reply.error = "ChatGPT declined: " + part["refusal"].toString();
            return reply;
          }
          reply.text << part["text"].toString();
          for (const auto &note : json::items(part["annotations"]))
            if (note["type"].toString() == "url_citation")
              addSource(reply.sources, note["title"].toString(),
                        note["url"].toString());
        }
      }
    }
    reply.searchQueries.removeEmptyStrings();
    if (status == "incomplete" && reply.toolCalls.empty())
      reply.text << "\n\n[The reply was cut off: "
                 << root["incomplete_details"]["reason"].toString() << ".]";

    previousResponse = root["id"].toString();
    input = json::array();
    reply.inputTokens = (int)root["usage"]["input_tokens"];
    reply.outputTokens = (int)root["usage"]["output_tokens"];
    return reply;
  }

private:
  Config config;
  juce::var tools, input;
  juce::String previousResponse;
  bool searchAllowed = true;
};

// --- Gemini: the Interactions API --------------------------------------------
// generateContent isn't where Google documents using Search together with your
// own functions; Interactions is.
class Gemini final : public Provider {
public:
  explicit Gemini(Config c) : config(std::move(c)) {
    tools = json::array({json::object({{"type", "google_search"}})});
    for (const auto &t : config.tools)
      tools.append(json::object({{"type", "function"},
                                 {"name", t.name},
                                 {"description", t.description},
                                 {"parameters", t.schema}}));
  }

  void addUserMessage(const juce::String &text) override { input = text; }

  void addToolResults(const std::vector<ToolResult> &results) override {
    input = json::array();
    for (const auto &r : results)
      input.append(json::object(
          {{"type", "function_result"},
           {"name", r.name},
           {"call_id", r.callId},
           {"result", json::array({json::object(
                          {{"type", "text"}, {"text", r.content}})})}}));
  }

  void stopSearching() override { searchAllowed = false; }
  void allowSearching() override { searchAllowed = true; }

  HttpRequest buildRequest() const override {
    auto body = json::object({{"model", config.model},
                              {"system_instruction", config.systemPrompt},
                              {"tools", searchAllowed ? tools : withoutFirst(tools)},
                              {"input", input}});
    // As with ChatGPT, only the history carries over: the rest goes every time.
    if (previousInteraction.isNotEmpty())
      body.getDynamicObject()->setProperty("previous_interaction_id",
                                           previousInteraction);
    auto request = jsonPost(config, "https://generativelanguage.googleapis.com",
                            "/v1beta/interactions", body);
    request.headers.set("x-goog-api-key", config.apiKey);
    request.headers.set("Api-Revision", "2026-05-20");
    return request;
  }

  ModelReply parseResponse(const HttpResponse &response) override {
    if (!response.ok())
      return failure("Gemini", config.apiKey, response);
    const auto root = juce::JSON::parse(response.text());
    if (!root["steps"].isArray() || root["id"].toString().isEmpty())
      return unreadable("Gemini");

    ModelReply reply;
    for (const auto &step : json::items(root["steps"])) {
      const auto type = step["type"].toString();
      if (type == "function_call") {
        reply.toolCalls.push_back({step["id"].toString(), step["name"].toString(),
                                   step["arguments"]});
      } else if (type == "google_search_call") {
        for (const auto &query : json::items(step["arguments"]["queries"]))
          reply.searchQueries.addIfNotAlreadyThere(query.toString());
      } else if (type == "google_search_result") {
        for (const auto &result : json::items(step["result"]))
          for (const auto &chip : parseSearchSuggestions(
                   result["search_suggestions"].toString()))
            addSource(reply.searchSuggestions, chip.title, chip.url);
      } else if (type == "model_output") {
        for (const auto &part : json::items(step["content"])) {
          reply.text << part["text"].toString();
          for (const auto &note : json::items(part["annotations"]))
            if (note["type"].toString() == "url_citation")
              addSource(reply.sources, note["title"].toString(),
                        note["url"].toString());
        }
      }
    }
    reply.searchQueries.removeEmptyStrings();

    previousInteraction = root["id"].toString();
    input = juce::var();
    const auto usage = root["usage"];
    reply.inputTokens = (int)usage["total_input_tokens"];
    reply.outputTokens = (int)usage["total_output_tokens"];
    return reply;
  }

private:
  Config config;
  juce::var tools, input;
  juce::String previousInteraction;
  bool searchAllowed = true;
};

juce::String decodeHtml(juce::String text) {
  return text.replace("&amp;", "&")
      .replace("&lt;", "<")
      .replace("&gt;", ">")
      .replace("&quot;", "\"")
      .replace("&#39;", "'");
}

juce::String stripTags(const juce::String &html) {
  juce::String out;
  bool inTag = false;
  for (const auto c : html) {
    if (c == '<')
      inTag = true;
    else if (c == '>')
      inTag = false;
    else if (!inTag)
      out << c;
  }
  return out;
}
} // namespace

std::unique_ptr<Provider> makeProvider(ProviderId id, Provider::Config config) {
  switch (id) {
  case ProviderId::anthropic:
    return std::make_unique<Anthropic>(std::move(config));
  case ProviderId::openai:
    return std::make_unique<OpenAi>(std::move(config));
  case ProviderId::gemini:
    return std::make_unique<Gemini>(std::move(config));
  }
  return nullptr;
}

std::vector<Source> parseSearchSuggestions(const juce::String &html) {
  std::vector<Source> chips;
  int position = 0;
  while (chips.size() < 5) {
    const int open = html.indexOfIgnoreCase(position, "<a ");
    if (open < 0)
      break;
    const int tagEnd = html.indexOfChar(open, '>');
    const int close = html.indexOfIgnoreCase(open, "</a>");
    if (tagEnd < 0 || close < tagEnd)
      break;
    position = close + 4;

    const auto tag = html.substring(open, tagEnd);
    const int href = tag.indexOfIgnoreCase("href=\"");
    if (href < 0)
      continue;
    const auto url = decodeHtml(tag.substring(href + 6).upToFirstOccurrenceOf(
        "\"", false, false));
    const auto title = decodeHtml(stripTags(html.substring(tagEnd + 1, close))).trim();
    if (title.isNotEmpty() && isSafeLink(url))
      chips.push_back({title, url});
  }
  return chips;
}

bool isSafeLink(const juce::String &url) {
  const auto trimmed = url.trim();
  return trimmed == url && (url.startsWithIgnoreCase("https://") ||
                            url.startsWithIgnoreCase("http://"));
}
} // namespace evil::agent
