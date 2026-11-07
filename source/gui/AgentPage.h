#pragma once

#include "Hardware.h"
#include "agent/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace evil::gui {
/** The agent tab: a chat with a model that researches tones and sets the amp up,
    using the player's own API keys, which are entered on the same page. */
class AgentPage final : public juce::Component, private juce::ChangeListener {
public:
  AgentPage(agent::Session &, agent::AgentSettings &);
  ~AgentPage() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  /** At the editor's text rate, while this page shows. */
  void refresh();
  /** The tab came into view. */
  void pageShown();
  /** Between the chat and the keys, which is what opens with no key yet. */
  void showKeys(bool);

private:
  class Transcript;
  class KeysView;

  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  void refreshProviderControls();
  void sendComposed();
  void setComposer(const juce::String &text);

  agent::Session &session;
  agent::AgentSettings &settings;

  juce::ComboBox providerBox, modelBox;
  juce::TextButton keysButton{"keys"}, newChatButton{"new chat"},
      browseButton{"browse tone3000"};
  juce::ImageComponent tone3000Logo, t3kMark;
  juce::HyperlinkButton poweredBy;

  std::unique_ptr<Transcript> transcript;
  juce::Viewport viewport;
  std::unique_ptr<KeysView> keysView;

  juce::TextEditor composer;
  juce::TextButton sendButton{"send"};

  juce::Rectangle<int> panelArea, screenArea;
  juce::Rectangle<float> headerRule;
  PaintCache frame;
  juce::String statusText;
  bool wasBusy = false;
};
} // namespace evil::gui
