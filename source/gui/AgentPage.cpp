#include "AgentPage.h"

#include "EvilBinaryData.h"
#include "EvilLookAndFeel.h"
#include "Widgets.h"

namespace evil::gui {
namespace {
using Entry = agent::Session::Entry;
using agent::AgentSettings;
using agent::ProviderId;

constexpr std::array<ProviderId, 3> providers{
    ProviderId::anthropic, ProviderId::openai, ProviderId::gemini};

const std::array<const char *, 3> examples{
    "A tone like [song or artist]. I have a [guitar].",
    "Make it creamier, and add a bit of fuzz.",
    "Set up a boost I can stomp on for the solo."};

/** Selectable text that sizes to fit: a read-only editor with its chrome off. */
void styleAsText(juce::TextEditor &editor, const juce::Font &font, juce::Colour colour) {
  editor.setMultiLine(true, true);
  editor.setReadOnly(true);
  editor.setCaretVisible(false);
  editor.setScrollbarsShown(false);
  editor.setBorder(juce::BorderSize<int>(0));
  editor.setIndents(0, 0);
  for (const auto id : {juce::TextEditor::backgroundColourId,
                        juce::TextEditor::outlineColourId,
                        juce::TextEditor::focusedOutlineColourId,
                        juce::TextEditor::shadowColourId})
    editor.setColour(id, juce::Colours::transparentBlack);
  editor.setColour(juce::TextEditor::textColourId, colour);
  editor.setFont(font);
}

void styleLink(juce::HyperlinkButton &link, float size = 11.0f) {
  link.setFont(mono(size), false, juce::Justification::centredLeft);
  link.setColour(juce::HyperlinkButton::textColourId, colours::accent());
}

/** Field for pasting a key: shows that one's there without showing it. */
void styleSecret(juce::TextEditor &editor) {
  editor.setPasswordCharacter((juce::juce_wchar)0x2022);
  editor.setFont(mono(12.5f));
  editor.setJustification(juce::Justification::centredLeft);
  editor.setIndents(8, 0);
  editor.setSelectAllWhenFocused(true);
}

juce::Image brandImage(const void *data, int size) {
  return juce::ImageCache::getFromMemory(data, size);
}
} // namespace

// --- the conversation --------------------------------------------------------
class AgentPage::Transcript final : public juce::Component {
public:
  explicit Transcript(AgentPage &p) : page(p) {
    for (const auto *example : examples) {
      auto *button = exampleButtons.add(new juce::TextButton(example));
      button->onClick = [this, example] { page.setComposer(example); };
      addChildComponent(button);
    }
    addChildComponent(hint);
    styleAsText(hint, sans(14.0f), colours::textSoft);
    hint.setJustification(juce::Justification::centred);
    hint.setText("Describe a sound: a song, an artist, or a change to what you "
                 "have now. It looks up the gear, finds captures on TONE3000 and "
                 "sets the amp.");
  }

  void update(const std::vector<Entry> &entries) {
    if (entries.size() < rows.size())
      rows.clear();
    for (size_t i = rows.size(); i < entries.size(); ++i)
      rows.push_back(makeRow(entries[i], i));
    for (size_t i = 0; i < entries.size(); ++i)
      if (rows[i]->revert != nullptr && entries[i].reverted) {
        rows[i]->revert->setEnabled(false);
        rows[i]->revert->setButtonText("reverted");
      }
    const bool empty = entries.empty();
    hint.setVisible(empty);
    for (auto *button : exampleButtons)
      button->setVisible(empty);
    layoutForWidth(getWidth());
  }

  /** Sets this component's height to fit its contents at this width. */
  void layoutForWidth(int width) {
    constexpr int inset = 2;
    const int textWidth = width - 2 * inset;
    if (textWidth <= 0)
      return;
    int y = 2;

    if (rows.empty()) {
      y = 70;
      hint.setBounds(juce::Rectangle<int>(0, y, textWidth, 48)
                         .withSizeKeepingCentre(juce::jmin(textWidth, 560), 48));
      y = hint.getBottom() + 18;
      for (auto *button : exampleButtons) {
        button->setBounds(juce::Rectangle<int>(0, y, width, 30)
                              .withSizeKeepingCentre(juce::jmin(textWidth, 520), 30));
        y += 36;
      }
    }

    for (const auto &rowPointer : rows) {
      auto &row = *rowPointer;
      if (row.kind == Entry::Kind::activity) {
        y += placeText(row.body, inset, y, textWidth) + 2;
      } else {
        y += 10;
        row.captionArea = {inset, y, textWidth, 15};
        y += 18;
        y += placeText(row.body, inset, y, textWidth) + 6;
      }
      y = layoutLinks(row.linkCaptionArea, row.linksCaption, row.links, y, inset, textWidth);
      y = layoutLinks(row.suggestionCaptionArea, row.suggestionsCaption, row.suggestions, y,
                      inset, textWidth);
      if (row.revert != nullptr) {
        row.revert->setBounds(inset, y + 2, 170, 24);
        y += 32;
      }
    }
    setSize(width, y + 8);
    repaint();
  }

  void paint(juce::Graphics &g) override {
    g.setFont(caption());
    for (const auto &row : rows) {
      if (!row->captionArea.isEmpty()) {
        g.setColour(row->kind == Entry::Kind::error ? colours::accent()
                                                    : colours::muted);
        g.drawText(row->kind == Entry::Kind::user        ? "YOU"
                   : row->kind == Entry::Kind::assistant ? "AGENT"
                                                         : "PROBLEM",
                   row->captionArea, juce::Justification::centredLeft, false);
      }
      g.setColour(colours::mutedSoft);
      if (!row->linkCaptionArea.isEmpty())
        g.drawText(row->linksCaption, row->linkCaptionArea,
                   juce::Justification::centredLeft, false);
      if (!row->suggestionCaptionArea.isEmpty())
        g.drawText(row->suggestionsCaption, row->suggestionCaptionArea,
                   juce::Justification::centredLeft, false);
    }
  }

private:
  struct Row {
    Entry::Kind kind = Entry::Kind::activity;
    juce::TextEditor body;
    juce::OwnedArray<juce::HyperlinkButton> links, suggestions;
    juce::String linksCaption, suggestionsCaption;
    std::unique_ptr<juce::TextButton> revert;
    juce::Rectangle<int> captionArea, linkCaptionArea, suggestionCaptionArea;
  };

  std::unique_ptr<Row> makeRow(const Entry &entry, size_t index) {
    auto row = std::make_unique<Row>();
    row->kind = entry.kind;
    const bool activity = entry.kind == Entry::Kind::activity;
    styleAsText(row->body, activity ? mono(11.5f) : sans(14.0f),
                activity ? colours::muted : colours::text);
    row->body.setText((activity ? juce::String::charToString(0x00b7) + " " : juce::String()) +
                      entry.text);
    addAndMakeVisible(row->body);

    const auto addLinks = [this](juce::OwnedArray<juce::HyperlinkButton> &buttons,
                                 const std::vector<agent::Source> &sources) {
      for (const auto &source : sources) {
        if (!agent::isSafeLink(source.url))
          continue;
        auto title = source.title;
        if (title.length() > 48)
          title = title.substring(0, 47).trimEnd() + juce::String::charToString(0x2026);
        auto *link = buttons.add(new juce::HyperlinkButton(title, juce::URL(source.url)));
        styleLink(*link);
        link->setTooltip(source.url);
        addAndMakeVisible(link);
      }
    };
    addLinks(row->links, entry.sources);
    addLinks(row->suggestions, entry.suggestions);
    if (!activity)
      row->linksCaption = "SOURCES";
    row->suggestionsCaption = "GOOGLE SEARCH SUGGESTIONS";

    if (entry.before != nullptr) {
      row->revert = std::make_unique<juce::TextButton>("revert to before this");
      row->revert->setTooltip("Put the amp back how it was before this answer");
      row->revert->onClick = [this, index] { page.session.revert(index); };
      addAndMakeVisible(*row->revert);
    }
    return row;
  }

  /** Sizes a text box to the height of its wrapped text, and returns that. It's
      measured at a height of one line, as an editor reports at least its own
      height. */
  static int placeText(juce::TextEditor &body, int x, int y, int width) {
    body.setBounds(x, y, width, 1);
    const int height = juce::jmax(1, body.getTextHeight());
    body.setSize(width, height);
    return height;
  }

  /** Lays links out left to right, wrapping, under a caption if they have one. */
  static int layoutLinks(juce::Rectangle<int> &captionArea, const juce::String &captionText,
                         juce::OwnedArray<juce::HyperlinkButton> &links, int y, int x,
                         int width) {
    captionArea = {};
    if (links.isEmpty())
      return y;
    if (captionText.isNotEmpty()) {
      captionArea = {x, y, width, 14};
      y += 15;
    }
    int cursor = x;
    constexpr int linkHeight = 18;
    const auto font = mono(11.0f);
    for (auto *link : links) {
      const int linkWidth =
          juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, link->getButtonText())) + 14;
      if (cursor > x && cursor + linkWidth > x + width) {
        cursor = x;
        y += linkHeight;
      }
      link->setBounds(cursor, y, juce::jmin(linkWidth, width), linkHeight);
      cursor += linkWidth + 8;
    }
    return y + linkHeight + 4;
  }

  AgentPage &page;
  std::vector<std::unique_ptr<Row>> rows;
  juce::TextEditor hint;
  juce::OwnedArray<juce::TextButton> exampleButtons;
};

// --- the keys ----------------------------------------------------------------
class AgentPage::KeysView final : public juce::Component {
public:
  KeysView(AgentPage &p) : page(p) {
    const std::array<std::pair<const char *, const char *>, 3> where{
        {{"Claude", "https://console.anthropic.com/settings/keys"},
         {"ChatGPT", "https://platform.openai.com/api-keys"},
         {"Gemini", "https://aistudio.google.com/apikey"}}};
    for (size_t i = 0; i < providers.size(); ++i) {
      auto &row = rows[i];
      const auto id = providers[i];
      row.name = where[i].first;
      styleSecret(row.field);
      row.field.setTextToShowWhenEmpty("paste your " + row.name + " API key",
                                       colours::mutedSoft);
      row.field.onTextChange = [this, id, &row] {
        page.settings.setApiKey(id, row.field.getText());
        repaint();
      };
      row.link.setButtonText("get a key");
      row.link.setURL(juce::URL(where[i].second));
      styleLink(row.link);
      addAndMakeVisible(row.field);
      addAndMakeVisible(row.link);
    }

    auto &t3k = rows[3];
    t3k.name = "TONE3000";
    styleSecret(t3k.field);
    t3k.field.setTextToShowWhenEmpty("publishable key: t3k_pub_...", colours::mutedSoft);
    t3k.field.onTextChange = [this, &t3k] {
      const auto typed = t3k.field.getText().trim();
      secretKeyRefused = agent::Tone3000::looksLikeSecretKey(typed);
      if (!secretKeyRefused)
        page.settings.set(AgentSettings::tone3000Key, typed);
      refreshTone3000();
    };
    t3k.link.setButtonText("get a key");
    t3k.link.setURL(juce::URL("https://www.tone3000.com/settings"));
    styleLink(t3k.link);
    addAndMakeVisible(t3k.field);
    addAndMakeVisible(t3k.link);

    addAndMakeVisible(connectButton);
    gui::setPrimary(connectButton);
    connectButton.onClick = [this] {
      if (waitingForCode())
        page.session.stop();
      else if (connected())
        page.session.disconnectTone3000();
      else
        page.session.connectTone3000();
      refreshTone3000();
    };
    connectButton.setTooltip("Sign in once with a code, on any device. No browser "
                             "opens, and it stays signed in.");

    codeLink.setButtonText("open it here");
    styleLink(codeLink);
    codeLink.setTooltip("Only if you want to: this opens the sign-in page with the "
                        "code filled in");
    addChildComponent(codeLink);

    styleAsText(notes, sans(13.0f), colours::textSoft);
    notes.setText(
        "Requests go straight from this computer to the company you choose, on "
        "your account and at your cost. Keys are saved only in " +
        AgentSettings::getDefaultFile().getFullPathName() +
        ", which only your user can read. They're never put in presets or "
        "sessions.\n\n"
        "TONE3000 is the community library of NAM captures and IRs. You sign in "
        "once with your own account there, by typing a code on any device, and "
        "the amp keeps itself signed in. Each capture stays credited to whoever "
        "made it.");
    addAndMakeVisible(notes);

    fillFromSettings();
  }

  void fillFromSettings() {
    for (size_t i = 0; i < providers.size(); ++i)
      if (!rows[i].field.hasKeyboardFocus(false))
        rows[i].field.setText(page.settings.getApiKey(providers[i]), false);
    if (!rows[3].field.hasKeyboardFocus(false))
      rows[3].field.setText(page.settings.get(AgentSettings::tone3000Key), false);
    refreshTone3000();
  }

  void refreshTone3000() {
    const auto code = page.session.getSignInCode();
    connectButton.setButtonText(waitingForCode() ? "cancel"
                                : connected()    ? "sign out"
                                                 : "sign in");
    connectButton.setEnabled(connected() || waitingForCode() ||
                             page.settings.get(AgentSettings::tone3000Key).isNotEmpty());
    codeLink.setVisible(code.linkWithCode.isNotEmpty() &&
                        agent::isSafeLink(code.linkWithCode));
    if (codeLink.isVisible())
      codeLink.setURL(juce::URL(code.linkWithCode));
    resized();
    repaint();
  }

  void resized() override {
    auto b = getLocalBounds().reduced(16, 4);
    b.removeFromTop(8);
    for (auto &row : rows) {
      auto line = b.removeFromTop(30);
      row.labelArea = line.removeFromLeft(96);
      row.link.setBounds(line.removeFromRight(76));
      line.removeFromRight(8);
      row.field.setBounds(line);
      b.removeFromTop(8);
    }
    auto status = b.removeFromTop(28);
    status.removeFromLeft(96); // under the fields
    connectButton.setBounds(status.removeFromLeft(112));
    status.removeFromLeft(12);
    codeLink.setBounds(status.removeFromRight(110));
    statusArea = status;
    b.removeFromTop(14);
    notes.setBounds(b);
  }

  void paint(juce::Graphics &g) override {
    g.setFont(caption());
    for (const auto &row : rows) {
      g.setColour(colours::muted);
      g.drawText(row.name.toUpperCase(), row.labelArea,
                 juce::Justification::centredLeft, false);
    }
    const auto code = page.session.getSignInCode();
    if (code.userCode.isNotEmpty()) {
      auto area = statusArea;
      const auto font = mono(15.0f, true, 0.06f);
      g.setFont(font);
      g.setColour(colours::accent());
      g.drawText(code.userCode,
                 area.removeFromLeft(juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                         font, code.userCode)) + 14),
                 juce::Justification::centredLeft, false);
      g.setFont(mono(11.5f));
      g.setColour(colours::textSoft);
      g.drawText("enter this at " +
                     code.verificationUri.fromFirstOccurrenceOf("://", false, false) +
                     ", on any device",
                 area, juce::Justification::centredLeft, true);
      return;
    }
    g.setFont(mono(11.5f));
    g.setColour(secretKeyRefused ? colours::accent() : colours::textSoft);
    g.drawText(secretKeyRefused
                   ? "That's a secret key, which must stay on a server. Use your "
                     "publishable one."
               : connected() ? "signed in, and it stays that way"
                             : "not signed in: only the built-in capture is available",
               statusArea, juce::Justification::centredLeft, true);
  }

private:
  struct KeyRow {
    juce::String name;
    juce::TextEditor field;
    juce::HyperlinkButton link;
    juce::Rectangle<int> labelArea;
  };

  bool connected() const {
    return page.settings.get(AgentSettings::tone3000Refresh).isNotEmpty() ||
           page.settings.get(AgentSettings::tone3000Access).isNotEmpty();
  }
  bool waitingForCode() const {
    return page.session.getSignInCode().userCode.isNotEmpty();
  }

  AgentPage &page;
  std::array<KeyRow, 4> rows;
  juce::TextButton connectButton{"sign in"};
  juce::HyperlinkButton codeLink;
  juce::TextEditor notes;
  juce::Rectangle<int> statusArea;
  bool secretKeyRefused = false;
};

// --- the page ----------------------------------------------------------------
AgentPage::AgentPage(agent::Session &s, agent::AgentSettings &a)
    : session(s), settings(a), transcript(std::make_unique<Transcript>(*this)),
      keysView(std::make_unique<KeysView>(*this)) {
  for (const auto id : providers)
    providerBox.addItem(AgentSettings::providerName(id), (int)id + 1);
  providerBox.setTooltip("Whose model answers, on your own API key");
  providerBox.onChange = [this] {
    const auto id = (ProviderId)(providerBox.getSelectedId() - 1);
    if (id != settings.getProvider())
      settings.setProvider(id);
    refreshProviderControls();
    if (settings.getApiKey(id).isEmpty())
      showKeys(true);
  };
  addAndMakeVisible(providerBox);

  modelBox.setEditableText(true);
  modelBox.setTooltip("The model to use. Pick one, or type the name of any other");
  modelBox.onChange = [this] {
    const auto id = settings.getProvider();
    if (modelBox.getText().trim() != settings.getModel(id))
      settings.setModel(id, modelBox.getText());
  };
  addAndMakeVisible(modelBox);

  addAndMakeVisible(keysButton);
  keysButton.setClickingTogglesState(false);
  keysButton.setTooltip("Your API keys and your TONE3000 sign-in");
  keysButton.onClick = [this] { showKeys(!keysView->isVisible()); };

  addAndMakeVisible(newChatButton);
  newChatButton.setTooltip("Start over. The amp stays as it is");
  newChatButton.onClick = [this] { session.newChat(); };

  addAndMakeVisible(browseButton);
  browseButton.setTooltip("Pick a tone yourself from the full TONE3000 catalog, "
                          "and the agent sets the amp up around it. This is the only "
                          "thing here that opens your browser.");
  browseButton.onClick = [this] {
    if (settings.get(AgentSettings::tone3000Key).isEmpty()) {
      showKeys(true);
      return;
    }
    showKeys(false);
    session.browseTone3000();
  };

  tone3000Logo.setImage(brandImage(EvilBinaryData::TONE3000logo_png,
                                   EvilBinaryData::TONE3000logo_pngSize),
                        juce::RectanglePlacement::centred);
  tone3000Logo.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(tone3000Logo);
  t3kMark.setImage(brandImage(EvilBinaryData::T3Kmark_png, EvilBinaryData::T3Kmark_pngSize),
                   juce::RectanglePlacement::centred);
  t3kMark.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(t3kMark);
  poweredBy.setButtonText("powered by");
  poweredBy.setURL(juce::URL("https://www.tone3000.com"));
  styleLink(poweredBy, 10.5f);
  poweredBy.setColour(juce::HyperlinkButton::textColourId, colours::muted);
  poweredBy.setTooltip("https://www.tone3000.com");
  addAndMakeVisible(poweredBy);

  viewport.setViewedComponent(transcript.get(), false);
  viewport.setScrollBarsShown(true, false);
  viewport.setScrollBarThickness(8);
  viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, colours::line);
  addAndMakeVisible(viewport);
  addChildComponent(*keysView);

  composer.setMultiLine(true, true);
  composer.setReturnKeyStartsNewLine(false);
  composer.setFont(sans(14.0f));
  composer.setBorder(juce::BorderSize<int>(0));
  composer.setIndents(10, 10);
  composer.setScrollbarsShown(false);
  composer.setTextToShowWhenEmpty("Describe the sound you're after", colours::mutedSoft);
  composer.onReturnKey = [this] { sendComposed(); };
  addAndMakeVisible(composer);

  gui::setPrimary(sendButton);
  sendButton.onClick = [this] {
    if (session.isBusy() &&
        !(session.isOnlyWaitingOnPlayer() && composer.getText().trim().isNotEmpty()))
      session.stop();
    else
      sendComposed();
  };
  addAndMakeVisible(sendButton);

  session.addChangeListener(this);
  settings.addChangeListener(this);
  refreshProviderControls();
  transcript->update(session.getEntries());
  showKeys(settings.getApiKey(settings.getProvider()).isEmpty());
  refresh();
}

AgentPage::~AgentPage() {
  session.removeChangeListener(this);
  settings.removeChangeListener(this);
}

void AgentPage::resized() {
  auto b = getLocalBounds();

  auto top = b.removeFromTop(30);
  providerBox.setBounds(top.removeFromLeft(110));
  top.removeFromLeft(6);
  modelBox.setBounds(top.removeFromLeft(190));
  top.removeFromLeft(6);
  keysButton.setBounds(top.removeFromLeft(58));
  top.removeFromLeft(6);
  newChatButton.setBounds(top.removeFromLeft(84));

  auto browse = top.removeFromRight(196); // the label centres clear of the mark
  browseButton.setBounds(browse);
  t3kMark.setBounds(browse.withTrimmedLeft(10).withWidth(30).withSizeKeepingCentre(30, 10));
  top.removeFromRight(10);
  tone3000Logo.setBounds(top.removeFromRight(92).withSizeKeepingCentre(92, 14));
  top.removeFromRight(2);
  poweredBy.setBounds(top.removeFromRight(76));
  b.removeFromTop(8);

  auto bottom = b.removeFromBottom(44);
  sendButton.setBounds(bottom.removeFromRight(72));
  bottom.removeFromRight(8);
  composer.setBounds(bottom);
  b.removeFromBottom(8);

  panelArea = b;
  auto inner = panelArea.reduced(14, 8).withTrimmedTop(34);
  viewport.setBounds(inner);
  keysView->setBounds(panelArea.reduced(1, 1).withTrimmedTop(38));
  transcript->layoutForWidth(viewport.getMaximumVisibleWidth() - 4);
  viewport.setViewPosition(0, transcript->getHeight());
}

void AgentPage::paint(juce::Graphics &g) {
  using namespace colours;
  headerRule = drawPanel(g, panelArea, 0, keysView->isVisible() ? "keys" : "tone agent");
  if (statusText.isEmpty())
    return;
  const auto font = mono(11.5f);
  const auto width = juce::GlyphArrangement::getStringWidth(font, statusText) + 18.0f;
  const auto area = headerRule.withLeft(juce::jmax(headerRule.getX(), headerRule.getRight() - width));
  g.setColour(panel);
  g.fillRect(area);
  g.setFont(font);
  g.setColour(session.isBusy() ? accent() : muted);
  g.drawText(statusText, area, juce::Justification::centredRight, true);
}

void AgentPage::refresh() {
  const bool busy = session.isBusy();
  juce::String text;
  if (busy) {
    const auto detail = session.getStatus();
    text = (detail.isNotEmpty() ? detail : juce::String("working")) + " " +
           juce::String(juce::roundToInt(session.getBusySeconds())) + " s";
  } else {
    text = session.getLastUsage();
  }
  if (text != statusText) {
    statusText = text;
    repaint(panelArea.withHeight(44));
  }
  if (busy != wasBusy) {
    wasBusy = busy;
    sendButton.setButtonText(busy ? "stop" : "send");
    browseButton.setEnabled(!busy);
    newChatButton.setEnabled(!busy);
  }
}

void AgentPage::pageShown() {
  refresh();
  if (isShowing() && !keysView->isVisible())
    composer.grabKeyboardFocus();
}

void AgentPage::showKeys(bool shouldShow) {
  keysView->setVisible(shouldShow);
  viewport.setVisible(!shouldShow);
  keysButton.setToggleState(shouldShow, juce::dontSendNotification);
  if (shouldShow)
    keysView->fillFromSettings();
  repaint();
}

void AgentPage::refreshProviderControls() {
  const auto id = settings.getProvider();
  providerBox.setSelectedId((int)id + 1, juce::dontSendNotification);
  modelBox.clear(juce::dontSendNotification);
  modelBox.addItemList(AgentSettings::suggestedModels(id), 1);
  modelBox.setText(settings.getModel(id), juce::dontSendNotification);
}

void AgentPage::setComposer(const juce::String &text) {
  composer.setText(text, false);
  composer.moveCaretToEnd();
  if (isShowing())
    composer.grabKeyboardFocus();
}

void AgentPage::sendComposed() {
  const auto text = composer.getText().trim();
  if (text.isEmpty())
    return;
  if (session.send(text))
    composer.clear();
}

void AgentPage::changeListenerCallback(juce::ChangeBroadcaster *source) {
  if (keysView->isVisible())
    keysView->refreshTone3000();
  if (source == &settings)
    return;
  const bool atEnd = viewport.getViewPositionY() + viewport.getViewHeight() >=
                     transcript->getHeight() - 24;
  transcript->update(session.getEntries());
  if (atEnd) // don't drag someone away from reading further up
    viewport.setViewPosition(0, transcript->getHeight());
  refresh();
}
} // namespace evil::gui
