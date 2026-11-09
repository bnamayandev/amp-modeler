#include "Tone3000.h"

#include "Json.h"
#include "state/PresetManager.h"

#include <juce_cryptography/juce_cryptography.h>

#include <cstring>
#include <mutex>
#include <random>

namespace evil::agent {
namespace {
constexpr const char *origin = "https://www.tone3000.com";
constexpr juce::int64 maxFileBytes = 64 * 1024 * 1024;
constexpr const char *creditFileName = PresetManager::attributionFileName;

juce::File libraryOverride;

juce::String base64Url(const void *data, size_t size) {
  return juce::Base64::toBase64(data, size)
      .replaceCharacter('+', '-')
      .replaceCharacter('/', '_')
      .removeCharacters("=");
}

/** std::random_device draws on the OS's entropy on every platform we build for,
    unlike juce::Random, which is predictable. */
juce::String randomToken(size_t numBytes) {
  std::random_device entropy;
  juce::MemoryBlock bytes(numBytes);
  for (size_t i = 0; i < numBytes; ++i)
    bytes[i] = (char)(entropy() & 0xff);
  return base64Url(bytes.getData(), numBytes);
}

juce::String formBody(std::initializer_list<std::pair<const char *, juce::String>> fields) {
  juce::StringArray parts;
  for (const auto &[key, value] : fields)
    parts.add(juce::String(key) + "=" + juce::URL::addEscapeChars(value, true));
  return parts.joinIntoString("&");
}

HttpRequest formPost(const juce::String &url, const juce::String &body) {
  HttpRequest request;
  request.method = "POST";
  request.url = url;
  request.body = body;
  request.headers.set("Content-Type", "application/x-www-form-urlencoded");
  return request;
}

/** The little page the browser lands on after signing in. Nothing in it comes
    from the request. */
void answerBrowser(juce::StreamingSocket &socket, int status,
                   const juce::String &message) {
  const juce::String html =
      "<!doctype html><meta charset=utf-8><title>Evil Amplifier</title>"
      "<body style=\"background:#2b2a27;color:#ece6d8;font:16px sans-serif;"
      "display:flex;align-items:center;justify-content:center;height:90vh\">"
      "<p>" + message + "</p></body>";
  juce::String response;
  response << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Bad Request")
           << "\r\nContent-Type: text/html; charset=utf-8"
           << "\r\nContent-Length: " << (int)html.getNumBytesAsUTF8()
           << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"
           << html;
  socket.write(response.toRawUTF8(), (int)response.getNumBytesAsUTF8());
}

/** The target of the HTTP request on the socket, e.g. "/callback?code=1". */
juce::String readRequestTarget(juce::StreamingSocket &socket) {
  juce::MemoryOutputStream head;
  char buffer[1024];
  while (!head.toString().contains("\r\n") && head.getDataSize() < 16 * 1024) {
    if (socket.waitUntilReady(true, 2000) != 1)
      break;
    const int n = socket.read(buffer, (int)sizeof(buffer), false);
    if (n <= 0)
      break;
    head.write(buffer, (size_t)n);
  }
  const auto line = head.toString().upToFirstOccurrenceOf("\r\n", false, false);
  if (!line.startsWith("GET "))
    return {};
  return line.fromFirstOccurrenceOf(" ", false, false)
      .upToFirstOccurrenceOf(" ", false, false);
}

juce::String httpFailureMessage(const HttpResponse &response) {
  switch (response.status) {
  case 0:
    return "couldn't reach TONE3000: " + response.error;
  case 401:
    return "TONE3000 didn't accept the sign-in";
  case 403:
    return "TONE3000 doesn't allow this";
  case 404:
    return "TONE3000 has no such tone, or it's private";
  case 429:
    return "TONE3000's rate limit was reached; try again in a minute";
  default:
    return "TONE3000 answered with HTTP " + juce::String(response.status);
  }
}
} // namespace

Tone3000::Tone3000(HttpClient &client, AgentSettings &s, OpenBrowser opener,
                   ShowCode shower)
    : http(client), settings(s), openBrowser(std::move(opener)),
      showCode(std::move(shower)) {}

bool Tone3000::hasKey() const {
  settings.reloadIfChanged();
  return settings.get(AgentSettings::tone3000Key).isNotEmpty();
}

bool Tone3000::isConnected() const {
  settings.reloadIfChanged(); // another copy of the amp may have signed in
  return settings.get(AgentSettings::tone3000Refresh).isNotEmpty() ||
         settings.get(AgentSettings::tone3000Access).isNotEmpty();
}

void Tone3000::disconnect() {
  settings.set(AgentSettings::tone3000Access, {});
  settings.set(AgentSettings::tone3000Refresh, {});
  settings.set(AgentSettings::tone3000Expiry, {});
}

juce::String Tone3000::signInWithCode(const ShouldCancel &shouldCancel, int maxWaitMs) {
  const auto clientId = settings.get(AgentSettings::tone3000Key);
  if (clientId.isEmpty())
    return "no TONE3000 key has been added yet";

  const auto started = http.send(
      formPost(juce::String(origin) + "/api/v1/oauth/device_authorization",
               formBody({{"client_id", clientId}})));
  const auto code = juce::JSON::parse(started.text());
  if (!started.ok()) {
    if (code["error"].toString() == "invalid_client")
      return "TONE3000 doesn't recognise that key. It's the publishable one from "
             "your settings there";
    return httpFailureMessage(started);
  }
  const auto deviceCode = code["device_code"].toString();
  const auto userCode = code["user_code"].toString();
  if (deviceCode.isEmpty() || userCode.isEmpty())
    return "TONE3000 sent no sign-in code";
  if (showCode != nullptr)
    showCode({userCode, code["verification_uri"].toString(),
              code["verification_uri_complete"].toString()});

  // Both from the response, as the spec has them, with its defaults if absent.
  auto intervalMs =
      1000 * (code.hasProperty("interval") ? (int)code["interval"] : 5);
  auto lifetimeMs =
      (juce::int64)1000 * (code.hasProperty("expires_in") ? (int)code["expires_in"] : 600);
  if (maxWaitMs >= 0)
    lifetimeMs = juce::jmin(lifetimeMs, (juce::int64)maxWaitMs);
  const auto deadline = juce::Time::currentTimeMillis() + lifetimeMs;

  for (;;) {
    for (const auto wakeAt = juce::Time::currentTimeMillis() + intervalMs;
         juce::Time::currentTimeMillis() < wakeAt;) {
      if (shouldCancel != nullptr && shouldCancel())
        return "cancelled";
      if (juce::Time::currentTimeMillis() >= deadline)
        return "the code ran out of time";
      juce::Thread::sleep(20);
    }
    if (shouldCancel != nullptr && shouldCancel())
      return "cancelled";
    if (juce::Time::currentTimeMillis() >= deadline)
      return "the code ran out of time";

    const auto response = http.send(formPost(
        juce::String(origin) + "/api/v1/oauth/token",
        formBody({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                  {"device_code", deviceCode},
                  {"client_id", clientId}})));
    if (response.ok())
      return storeTokens(juce::JSON::parse(response.text()))
                 ? juce::String()
                 : juce::String("TONE3000 sent no sign-in");

    const auto error = juce::JSON::parse(response.text())["error"].toString();
    if (error == "access_denied")
      return "the sign-in was declined";
    if (error == "expired_token")
      return "the code ran out of time";
    if (error == "invalid_client" || error == "invalid_grant")
      return "TONE3000 refused the sign-in (" + error + ")";
    // Not yet, or a network blip while they're typing: neither ends the wait.
    intervalMs = nextPollIntervalMs(intervalMs, error);
  }
}

int Tone3000::nextPollIntervalMs(int intervalMs, const juce::String &pollError) {
  return pollError == "slow_down" ? intervalMs + 5000 : intervalMs;
}

Tone3000::CatalogPick Tone3000::browseCatalog(const ShouldCancel &shouldCancel,
                                              int timeoutMs) {
  CatalogPick result;
  const auto clientId = settings.get(AgentSettings::tone3000Key);
  if (clientId.isEmpty()) {
    result.error = "no TONE3000 key has been added yet";
    return result;
  }

  // Only reachable from this machine. TONE3000 accepts any loopback port
  // without it being registered.
  juce::StreamingSocket listener;
  if (!listener.createListener(0, "127.0.0.1")) {
    result.error = "couldn't open a local port for the sign-in";
    return result;
  }
  const auto redirectUri =
      "http://127.0.0.1:" + juce::String(listener.getBoundPort()) + "/callback";

  const auto pkce = makePkce();
  openBrowser(juce::URL(juce::String(origin) + "/api/v1/oauth/authorize")
                  .withParameter("client_id", clientId)
                  .withParameter("redirect_uri", redirectUri)
                  .withParameter("response_type", "code")
                  .withParameter("code_challenge", pkce.challenge)
                  .withParameter("code_challenge_method", "S256")
                  .withParameter("state", pkce.state)
                  .withParameter("prompt", "select_tone")
                  .withParameter("menubar", "true")); // its close button works

  Callback callback;
  const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
  for (;;) {
    if (shouldCancel != nullptr && shouldCancel()) {
      result.error = "cancelled";
      return result;
    }
    if (juce::Time::getMillisecondCounter() > deadline) {
      result.error = "nothing was picked in time";
      return result;
    }
    if (listener.waitUntilReady(true, 200) != 1)
      continue;
    const std::unique_ptr<juce::StreamingSocket> connection(
        listener.waitForNextConnection());
    if (connection == nullptr)
      continue;

    const auto target = readRequestTarget(*connection);
    if (target.upToFirstOccurrenceOf("?", false, false) != "/callback") {
      answerBrowser(*connection, 400, "Not here."); // a favicon request, say
      continue;
    }
    const auto candidate =
        parseCallback(target.fromFirstOccurrenceOf("?", false, false));
    if (candidate.state != pkce.state) {
      // Not our flow: anything on this machine can hit the port. Keep waiting.
      answerBrowser(*connection, 400, "That sign-in didn't come from this window.");
      continue;
    }
    callback = candidate;
    answerBrowser(*connection, 200,
                  "All done. You can close this tab and go back to Evil Amplifier.");
    break;
  }

  if (callback.error.isNotEmpty()) {
    result.error = "TONE3000 said: " + callback.error;
    return result;
  }
  if (callback.code.isEmpty()) { // closed before signing in
    result.error = "cancelled";
    return result;
  }

  const auto response = http.send(
      formPost(juce::String(origin) + "/api/v1/oauth/token",
               formBody({{"grant_type", "authorization_code"},
                         {"code", callback.code},
                         {"code_verifier", pkce.verifier},
                         {"redirect_uri", redirectUri},
                         {"client_id", clientId}})));
  if (!response.ok() || !storeTokens(juce::JSON::parse(response.text()))) {
    result.error = "the sign-in didn't complete: " + httpFailureMessage(response);
    return result;
  }
  result.connected = true;
  if (!callback.canceled)
    result.toneId = callback.toneId;
  return result;
}

bool Tone3000::storeTokens(const juce::var &tokens) {
  const auto access = tokens["access_token"].toString();
  if (access.isEmpty())
    return false;
  settings.set(AgentSettings::tone3000Access, access);
  // A response without a new refresh token leaves the old one in force.
  if (tokens["refresh_token"].toString().isNotEmpty())
    settings.set(AgentSettings::tone3000Refresh, tokens["refresh_token"].toString());
  const auto expiresInSeconds = (juce::int64)tokens["expires_in"];
  settings.set(AgentSettings::tone3000Expiry,
               juce::String(juce::Time::currentTimeMillis() +
                            expiresInSeconds * 1000));
  return true;
}

bool Tone3000::accessTokenExpired() const {
  const auto expiry =
      settings.get(AgentSettings::tone3000Expiry).getLargeIntValue();
  return juce::Time::currentTimeMillis() > expiry - 60 * 1000;
}

bool Tone3000::refreshTokens(const juce::String &rejected) {
  // Refresh tokens are single use, so one at a time in this process.
  static std::mutex oneAtATime;
  std::lock_guard lock(oneAtATime);

  settings.reloadIfChanged();
  const auto access = settings.get(AgentSettings::tone3000Access);
  if (access.isNotEmpty() && access != rejected && !accessTokenExpired())
    return true; // another instance already did it

  const auto refresh = settings.get(AgentSettings::tone3000Refresh);
  if (refresh.isEmpty())
    return false;
  const auto response = http.send(
      formPost(juce::String(origin) + "/api/v1/oauth/token",
               formBody({{"grant_type", "refresh_token"},
                         {"refresh_token", refresh},
                         {"client_id", settings.get(AgentSettings::tone3000Key)}})));
  if (response.ok())
    return storeTokens(juce::JSON::parse(response.text()));

  // Only this means the session is over. Being offline, a server error, or a
  // problem with the key all pass; they're no reason to make anyone sign in again.
  if (juce::JSON::parse(response.text())["error"].toString() != "invalid_grant")
    return false;
  settings.reloadIfChanged();
  if (settings.get(AgentSettings::tone3000Refresh) != refresh)
    return true; // another process used it first, and saved the new one
  disconnect();
  return false;
}

juce::String Tone3000::accessToken() {
  settings.reloadIfChanged();
  if (accessTokenExpired())
    refreshTokens(settings.get(AgentSettings::tone3000Access));
  return settings.get(AgentSettings::tone3000Access);
}

HttpResponse Tone3000::authenticated(HttpRequest request) {
  HttpResponse refused;
  if (!isTrustedUrl(request.url)) {
    refused.error = "refused to send the sign-in to " + request.url;
    return refused;
  }
  const auto token = accessToken();
  if (token.isEmpty()) {
    refused.status = 401;
    refused.error = "not signed in";
    return refused;
  }
  request.headers.set("Authorization", "Bearer " + token);
  auto response = http.send(request);
  if (response.status == 401 && refreshTokens(token)) { // expired in between
    request.headers.set("Authorization",
                        "Bearer " + settings.get(AgentSettings::tone3000Access));
    response = http.send(request);
  }
  return response;
}

juce::var Tone3000::getJson(const juce::String &pathAndQuery, juce::String &error) {
  HttpRequest request;
  request.url = juce::String(origin) + pathAndQuery;
  const auto response = authenticated(request);
  if (!response.ok()) {
    error = httpFailureMessage(response);
    return {};
  }
  return juce::JSON::parse(response.text());
}

juce::var Tone3000::getTone(juce::int64 toneId, juce::String &error) {
  return getJson("/api/v1/tones/" + juce::String(toneId), error);
}

void Tone3000::collectModels(const juce::String &path, FileList &list,
                             juce::Array<juce::int64> &seen, juce::String &error) {
  // At the most the API sends a page, this is 3000 files, and most packs take one
  // request. It's a limit on a server that never stops, not on any real pack.
  constexpr int maxPages = 10;
  for (int page = 1; page <= maxPages; ++page) {
    juce::String failed;
    const auto reply =
        getJson(path + (page > 1 ? "&page=" + juce::String(page) : juce::String()), failed);
    if (failed.isNotEmpty()) {
      error = failed;
      list.complete = false;
      return;
    }
    for (const auto &model : json::items(reply["data"]))
      if (const auto id = (juce::int64)model["id"]; !seen.contains(id)) {
        seen.add(id);
        list.files.add(model);
      }
    if (page >= (int)reply["total_pages"]) // including a reply that doesn't say
      return;
  }
  list.complete = false;
}

Tone3000::FileList Tone3000::listModels(juce::int64 toneId, const juce::String &format,
                                        juce::String &error) {
  const auto path =
      "/api/v1/models?tone_id=" + juce::String(toneId) + "&page_size=300";
  FileList list;
  juce::Array<juce::int64> seen;
  if (format == "ir") {
    collectModels(path, list, seen, error);
    return list;
  }

  // Without an architecture the API leaves out A2; with one it has only A2.
  for (const juce::String architecture : {"&architecture=2", ""})
    collectModels(path + architecture, list, seen, error);
  if (!list.files.isEmpty())
    error = {};
  return list;
}

juce::var Tone3000::listTones(const juce::String &list, const juce::String &gear,
                              const juce::String &query, juce::String &error, int *total) {
  auto path = "/api/v1/tones/" + list;
  juce::StringArray parameters;
  if (gear.isNotEmpty())
    parameters.add("gear=" + juce::URL::addEscapeChars(gear, true));
  // Trending and latest are fixed feeds; the player's own lists can be filtered.
  if (list != "trending" && list != "latest") {
    parameters.add("page_size=25");
    if (query.isNotEmpty())
      parameters.add("query=" + juce::URL::addEscapeChars(query, true));
  }
  if (!parameters.isEmpty())
    path << "?" << parameters.joinIntoString("&");
  const auto reply = getJson(path, error);
  if (total != nullptr)
    *total = (int)reply["total"];
  return reply["data"];
}

juce::File Tone3000::download(const juce::var &tone, const juce::var &model,
                              juce::String &error) {
  const auto toneId = (juce::int64)tone["id"];
  const auto modelId = (juce::int64)model["id"];
  const auto url = model["model_url"].toString();
  if (toneId <= 0 || modelId <= 0 || !isTrustedUrl(url)) {
    error = "TONE3000 gave no usable download for this file";
    return {};
  }

  // Names come from the IDs and a cleaned-up title, never a server's path.
  const auto dir =
      getLibraryDirectory().getChildFile(juce::String(toneId));
  const auto remember = [&] {
    dir.getChildFile(creditFileName)
        .replaceWithText(json::dump(json::object({{"credit", credit(tone)},
                                                  {"url", pageUrl(tone)},
                                                  {"gear", tone["gear"]}})));
  };
  const auto stem = juce::String(modelId) + "-";
  for (const auto &existing :
       dir.findChildFiles(juce::File::findFiles, false, stem + "*"))
    if (existing.getSize() > 0) {
      remember(); // files from before the gear was kept pick it up here
      return existing;
    }

  HttpRequest request;
  request.url = url;
  request.timeoutMs = 2 * 60 * 1000;
  request.maxResponseBytes = maxFileBytes;
  const auto response = authenticated(request);
  if (!response.ok()) {
    error = response.error.isNotEmpty() && response.status != 0
                ? "the download failed: " + response.error
                : "the download failed: " + httpFailureMessage(response);
    return {};
  }
  const auto extension = sniffExtension(response.body);
  if (extension.isEmpty()) {
    error = "the download isn't a NAM capture or an audio file";
    return {};
  }

  const auto name = juce::File::createLegalFileName(model["name"].toString())
                        .substring(0, 60)
                        .trim();
  const auto file = dir.getChildFile(stem + name + extension);
  if (!dir.createDirectory() ||
      !file.replaceWithData(response.body.getData(), response.body.getSize())) {
    error = "couldn't save " + file.getFullPathName();
    return {};
  }
  remember();
  return file;
}

juce::String Tone3000::pageUrl(const juce::var &tone) {
  auto url = tone["url"].toString();
  if (url.startsWith("/"))
    url = juce::String(origin) + url;
  return isTrustedUrl(url)
             ? url
             : juce::String(origin) + "/tones/" + tone["id"].toString();
}

juce::String Tone3000::credit(const juce::var &tone) {
  const auto user = tone["user"];
  const auto displayName = user["display_name"].toString();
  auto creator = displayName.isNotEmpty() ? displayName : user["username"].toString();
  auto line = tone["title"].toString();
  if (creator.isNotEmpty())
    line << " by " << creator;
  const auto license = tone["license"].toString();
  if (license.isNotEmpty())
    line << " (" << (license == "t3k"   ? juce::String("T3K licence")
                     : license == "cco" ? juce::String("CC0")
                                        : license.toUpperCase().replace("-", " "))
         << ")";
  return line;
}

juce::File Tone3000::getLibraryDirectory() {
  if (libraryOverride != juce::File())
    return libraryOverride;
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("Tone3000");
}

void Tone3000::setLibraryDirectoryForTesting(const juce::File &dir) {
  libraryOverride = dir;
}

/** What was kept about a file: beside it in the library, or beside where a preset
    that carried it put it back. */
static juce::var attributionFor(const juce::String &source) {
  if (!juce::File::isAbsolutePath(source))
    return {};
  const juce::File file(source);
  if (!file.isAChildOf(Tone3000::getLibraryDirectory()) &&
      !file.isAChildOf(PresetManager::getSavedFilesDirectory()))
    return {};
  return juce::JSON::parse(file.getSiblingFile(creditFileName).loadFileAsString());
}

juce::String Tone3000::creditForSource(const juce::String &source) {
  const auto line = attributionFor(source)["credit"].toString();
  return line.isNotEmpty() ? "TONE3000: " + line : juce::String();
}

juce::String Tone3000::gearForSource(const juce::String &source) {
  return attributionFor(source)["gear"].toString();
}

Tone3000::Pkce Tone3000::makePkce() {
  Pkce pkce;
  pkce.verifier = randomToken(32); // 43 characters
  pkce.challenge = challengeFor(pkce.verifier);
  pkce.state = randomToken(16);
  return pkce;
}

juce::String Tone3000::challengeFor(const juce::String &verifier) {
  const juce::SHA256 hash(verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
  const auto raw = hash.getRawData();
  return base64Url(raw.getData(), raw.getSize());
}

Tone3000::Callback Tone3000::parseCallback(const juce::String &query) {
  Callback callback;
  for (const auto &pair : juce::StringArray::fromTokens(query, "&", {})) {
    const auto key = juce::URL::removeEscapeChars(
        pair.upToFirstOccurrenceOf("=", false, false));
    const auto value = juce::URL::removeEscapeChars(
        pair.fromFirstOccurrenceOf("=", false, false).replaceCharacter('+', ' '));
    if (key == "code")
      callback.code = value;
    else if (key == "state")
      callback.state = value;
    else if (key == "error")
      callback.error = value;
    else if (key == "tone_id")
      callback.toneId = value;
    else if (key == "canceled")
      callback.canceled = value == "true";
  }
  return callback;
}

juce::int64 Tone3000::parseToneId(const juce::var &idOrUrl) {
  auto text = idOrUrl.toString().trim();
  if (text.containsChar('/')) {
    text = text.upToFirstOccurrenceOf("?", false, false)
               .upToFirstOccurrenceOf("#", false, false);
    while (text.endsWithChar('/'))
      text = text.dropLastCharacters(1);
    text = text.fromLastOccurrenceOf("/", false, false);
    if (text.containsChar('-'))
      text = text.fromLastOccurrenceOf("-", false, false);
  }
  if (text.isEmpty() || !text.containsOnly("0123456789") || text.length() > 15)
    return 0;
  return text.getLargeIntValue();
}

bool Tone3000::looksLikeSecretKey(const juce::String &key) {
  return key.trim().startsWith("t3k_cs_");
}

bool Tone3000::isTrustedUrl(const juce::String &url) {
  if (!url.startsWithIgnoreCase("https://"))
    return false;
  const auto host = juce::URL(url).getDomain().toLowerCase();
  return host == "tone3000.com" || host.endsWith(".tone3000.com");
}

juce::String Tone3000::sniffExtension(const juce::MemoryBlock &data) {
  const auto *bytes = static_cast<const char *>(data.getData());
  const auto size = data.getSize();
  const auto startsWith = [&](const char *magic) {
    const auto length = strlen(magic);
    return size >= length && memcmp(bytes, magic, length) == 0;
  };
  if (startsWith("RIFF"))
    return ".wav";
  if (startsWith("FORM"))
    return ".aif";
  if (startsWith("fLaC"))
    return ".flac";
  for (size_t i = 0; i < size; ++i) {
    if (juce::CharacterFunctions::isWhitespace((juce::juce_wchar)(juce::uint8)bytes[i]))
      continue;
    return bytes[i] == '{' ? ".nam" : "";
  }
  return {};
}
} // namespace evil::agent
