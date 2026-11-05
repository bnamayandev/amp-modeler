#pragma once

#include "AgentSettings.h"
#include "Http.h"

#include <functional>

namespace evil::agent {
/** TONE3000: where the NAM captures and IRs come from.

    TONE3000's free tier, which an open-source amp falls under, allows its OAuth
    flows and bounded lists but not its search endpoint. So the agent finds tones
    through its web search, and this fetches them by ID, as TONE3000's own Load
    Tone example does. See https://www.tone3000.com/api/terms.

    Sign-in is TONE3000's device flow, meant for plugin hosts and headless boxes:
    the player enters a short code once, on any device, and the amp refreshes
    its tokens from then on. No browser opens and none has to stay open.

    The catalog is the one thing that needs a browser, so it only opens when the
    player asks for it, and takes the redirect on a loopback port.
    Blocking; call from the agent's worker thread. */
class Tone3000 {
public:
  struct SignInCode {
    juce::String userCode;        // what to type, as XXXX-XXXX
    juce::String verificationUri; // where to type it
    juce::String linkWithCode;    // the same with the code filled in
  };
  struct CatalogPick {
    bool connected = false;
    juce::String toneId; // empty if they signed in and picked nothing
    juce::String error;
  };
  using OpenBrowser = std::function<void(const juce::URL &)>;
  using ShowCode = std::function<void(const SignInCode &)>;
  using ShouldCancel = std::function<bool()>;

  Tone3000(HttpClient &, AgentSettings &, OpenBrowser, ShowCode = {});

  bool hasKey() const;
  bool isConnected() const;
  void disconnect();

  /** Asks for a code, shows it, and waits for the player to enter it, up to the
      code's lifetime or maxWaitMs. Returns why not, or empty once signed in. */
  juce::String signInWithCode(const ShouldCancel &, int maxWaitMs = -1);

  /** Opens the catalog in the browser, and waits for a pick. */
  CatalogPick browseCatalog(const ShouldCancel &, int timeoutMs = 3 * 60 * 1000);

  juce::var getTone(juce::int64 toneId, juce::String &error);
  /** A tone's files of both NAM generations, or its IRs. */
  juce::var listModels(juce::int64 toneId, const juce::String &format,
                       juce::String &error);
  /** list: favorited, downloaded, created, trending or latest. */
  juce::var listTones(const juce::String &list, const juce::String &gear,
                      const juce::String &query, juce::String &error);
  /** Saves one file to the library, keeping who made it beside it, and returns
      it. A file already there isn't fetched again. */
  juce::File download(const juce::var &tone, const juce::var &model,
                      juce::String &error);

  /** The tone's page, always on TONE3000's site. */
  static juce::String pageUrl(const juce::var &tone);
  /** "Title by creator (licence)", as TONE3000 requires it stays visible. */
  static juce::String credit(const juce::var &tone);

  /** Where downloads go. */
  static juce::File getLibraryDirectory();
  static void setLibraryDirectoryForTesting(const juce::File &);
  /** For a loaded model or IR: its TONE3000 credit, or empty if it isn't one. */
  static juce::String creditForSource(const juce::String &source);

  // --- pure helpers, public for the tests ---
  struct Pkce {
    juce::String verifier, challenge, state;
  };
  static Pkce makePkce();
  static juce::String challengeFor(const juce::String &verifier);

  struct Callback {
    juce::String code, state, error, toneId;
    bool canceled = false;
  };
  static Callback parseCallback(const juce::String &query);

  /** Accepts a bare ID or a tone page's URL, whose slug ends in the ID.
      Returns 0 if there isn't one. */
  static juce::int64 parseToneId(const juce::var &idOrUrl);

  /** A t3k_cs_ key must never reach a user's machine, so it's refused. */
  static bool looksLikeSecretKey(const juce::String &key);

  /** How long to wait before polling again, given what the last poll said. */
  static int nextPollIntervalMs(int intervalMs, const juce::String &pollError);

  /** Only https on TONE3000's own hosts ever gets the access token. */
  static bool isTrustedUrl(const juce::String &url);

  /** ".nam", ".wav", ".aif" or ".flac" from a file's first bytes, whatever the
      server called it; empty if it's none of those. */
  static juce::String sniffExtension(const juce::MemoryBlock &);

private:
  juce::String accessToken();
  /** rejected is the access token found wanting. If the one saved has since
      changed, another instance got there first and there's nothing to do. */
  bool refreshTokens(const juce::String &rejected);
  bool accessTokenExpired() const;
  bool storeTokens(const juce::var &tokenResponse);
  HttpResponse authenticated(HttpRequest);
  juce::var getJson(const juce::String &pathAndQuery, juce::String &error);

  HttpClient &http;
  AgentSettings &settings;
  OpenBrowser openBrowser;
  ShowCode showCode;
};
} // namespace evil::agent
