// evil-snapshot: renders the plugin editor offscreen to PNG files, without
// opening a window.
//
//   evil-snapshot <output-directory>
//
// Writes editor.png (default size), editor@1.5x.png, stretched.png, which
// simulates a window manager forcing the window to an odd size, to check the
// layout scales uniformly, pedals.png: the pedal page with a few pedals on,
// editor-typing.png and pedals-typing.png: those pages with every value box open
// for typing, tuner-idle.png, tuner.png, tuner-in-tune.png and tuner-locked.png: the tuner
// page with a custom tuning, hearing notes played into the processor, and
// settings.png: the settings popover open, with a blue accent, app.png and
// app-settings.png: the header and the popover as the standalone app has them,
// and agent-keys.png, agent-sign-in.png, agent-empty.png and agent-chat.png: the
// agent tab, with made-up keys and a made-up conversation. The saved accent,
// tuning and keys are ignored, so every shot starts from the default and none
// can show a real key.

#include "PluginEditor.h"

#include <iostream>

namespace {
bool save(juce::Component &c, const juce::File &file, float scale) {
  const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
  file.deleteFile();
  juce::FileOutputStream out(file);
  juce::PNGImageFormat png;
  const bool ok = out.openedOk() && png.writeImageToStream(image, out);
  std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " ("
            << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
  return ok;
}

juce::Component *findButton(juce::Component &root, const juce::String &text) {
  for (auto *child : root.getChildren()) {
    if (auto *b = dynamic_cast<juce::Button *>(child);
        b != nullptr && b->getButtonText() == text)
      return b;
    if (auto *found = findButton(*child, text))
      return found;
  }
  return nullptr;
}

template <typename Type> Type *findChild(juce::Component &root) {
  for (auto *child : root.getChildren()) {
    if (auto *c = dynamic_cast<Type *>(child))
      return c;
    if (auto *found = findChild<Type>(*child))
      return found;
  }
  return nullptr;
}

template <typename Type>
void findAll(juce::Component &root, std::vector<Type *> &found) {
  for (auto *child : root.getChildren()) {
    if (auto *c = dynamic_cast<Type *>(child))
      found.push_back(c);
    findAll(*child, found);
  }
}

/** Saves a shot with every value box on the page open for typing. */
bool saveTyping(juce::Component &page, const juce::File &file) {
  std::vector<juce::Slider *> sliders;
  findAll(page, sliders);
  for (auto *slider : sliders)
    if (slider->getTextBoxPosition() != juce::Slider::NoTextBox)
      slider->showTextBox();
  const bool ok = save(page, file, 1.5f);
  for (auto *slider : sliders)
    if (slider->getTextBoxPosition() != juce::Slider::NoTextBox)
      slider->hideTextBox(true);
  return ok;
}

/** Plays a plucked note through the processor and hands what comes out of its
    tuner tap to the page, the way the editor's timer does. */
void playNote(evil::EvilAmplifierProcessor &processor,
              evil::gui::TunerPage &tuner, double hz) {
  constexpr double rate = 48000.0;
  constexpr int block = 128, blocksPerTick = 13; // about 30 Hz
  juce::AudioBuffer<float> buffer(2, block);
  juce::MidiBuffer midi;
  std::array<float, 4096> samples;
  int position = 0;
  for (int tick = 0; tick < 30; ++tick) {
    for (int b = 0; b < blocksPerTick; ++b) {
      buffer.clear();
      auto *in = buffer.getWritePointer(0);
      for (int i = 0; i < block; ++i, ++position) {
        const double t = position / rate;
        for (int k = 1; k <= 6; ++k)
          in[i] += (float)(0.2 / k * std::exp(-k * t) *
                           std::sin(juce::MathConstants<double>::twoPi * hz * k * t));
      }
      processor.processBlock(buffer, midi);
    }
    auto &tap = processor.getTunerTap();
    for (int n; (n = tap.pull(samples.data(), (int)samples.size())) > 0;)
      tuner.push(samples.data(), n);
    tuner.refresh(rate, 1.0f / 30.0f);
  }
}
} // namespace

int main(int argc, char **argv) {
  juce::ScopedJuceInitialiser_GUI juce;
  if (argc != 2) {
    std::cerr << "usage: evil-snapshot <output-directory>" << std::endl;
    return 1;
  }
  const auto dir =
      juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
  dir.createDirectory();

  // Held for the whole run, so editors don't reload the saved accent.
  juce::SharedResourcePointer<evil::gui::Settings> settings;
  evil::gui::colours::setAccentHue(evil::gui::colours::defaultAccentHue);

  // Before anything reads it, so the player's real keys never get near a shot.
  juce::SharedResourcePointer<evil::agent::AgentSettings> agentSettings;
  const juce::TemporaryFile agentFile(".xml");
  agentSettings->useFile(agentFile.getFile());

  evil::EvilAmplifierProcessor processor;
  processor.prepareToPlay(48000.0, 128);

  // Let the background model load finish; the editor reads the status when it
  // opens.
  for (int i = 0; i < 500 && processor.getModels().getStatus().state !=
                                 evil::ModelManager::State::ready;
       ++i)
    juce::Thread::sleep(10);

  std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());

  bool ok = save(*editor, dir.getChildFile("editor.png"), 1.0f);
  ok = save(*editor, dir.getChildFile("editor@1.5x.png"), 1.5f) && ok;
  ok = saveTyping(*editor, dir.getChildFile("editor-typing.png")) && ok;

  editor->setBoundsConstrained({0, 0, 940, 970}); // what the window manager did
  std::cout << "asked for 940x970, constrained to " << editor->getWidth() << "x"
            << editor->getHeight() << std::endl;
  ok = save(*editor, dir.getChildFile("stretched.png"), 1.0f) && ok;

  editor.reset();

  for (auto *id : {evil::params::wahOn, evil::params::odOn,
                   evil::params::phaserOn, evil::params::delayOn,
                   evil::params::reverbOn})
    processor.getState().getParameter(id)->setValueNotifyingHost(1.0f);
  auto *transpose = processor.getState().getParameter(evil::params::transpose);
  transpose->setValueNotifyingHost(transpose->convertTo0to1(-1.0f));
  processor.editorPage = 1;
  editor.reset(processor.createEditor());
  ok = save(*editor, dir.getChildFile("pedals.png"), 1.5f) && ok;
  ok = saveTyping(*editor, dir.getChildFile("pedals-typing.png")) && ok;

  // The transposer is still on from above: the tuner must not hear it. Not
  // a custom tuning saved to the library, which would overwrite the player's.
  editor.reset();
  processor.editorPage = 2;
  editor.reset(processor.createEditor());
  if (auto *tuner = findChild<evil::gui::TunerPage>(*editor)) {
    const auto custom = evil::tuning::fromText("D4 B3 G3 Gb3 A2 E2");
    tuner->showStrings(*custom);
    ok = save(*editor, dir.getChildFile("tuner-idle.png"), 1.5f) && ok;

    const auto gb3 = evil::tuning::frequencyOf(54);
    playNote(processor, *tuner, gb3 * std::pow(2.0, 18.0 / 1200.0));
    ok = save(*editor, dir.getChildFile("tuner.png"), 1.5f) && ok;
    playNote(processor, *tuner, gb3 * std::pow(2.0, -1.0 / 1200.0));
    ok = save(*editor, dir.getChildFile("tuner-in-tune.png"), 1.5f) && ok;

    // String 1 wants D4 and it's still at E4.
    if (auto *string1 = dynamic_cast<juce::Button *>(findButton(*tuner, "1")))
      string1->onClick();
    playNote(processor, *tuner, evil::tuning::frequencyOf(64));
    ok = save(*editor, dir.getChildFile("tuner-locked.png"), 1.5f) && ok;
  } else {
    std::cout << "FAILED: no tuner page" << std::endl;
    ok = false;
  }

  // Not settings->setAccentHue(), which would save it.
  evil::gui::colours::setAccentHue(215.0f / 360.0f);
  editor.reset();
  processor.editorPage = 0;
  editor.reset(processor.createEditor());
  if (auto *button = findButton(*editor, "settings")) {
    evil::gui::SettingsPanel panel;
    juce::CallOutBox box(panel, button->getBounds(),
                         button->getParentComponent());
    ok = save(*editor, dir.getChildFile("settings.png"), 1.5f) && ok;
  } else {
    std::cout << "FAILED: no settings button" << std::endl;
    ok = false;
  }

  evil::gui::colours::setAccentHue(evil::gui::colours::defaultAccentHue);
  editor.reset();
  {
    // The app is the same editor on a processor that knows it's in the app.
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Standalone);
    evil::EvilAmplifierProcessor app;
    juce::AudioProcessor::setTypeOfNextNewPlugin(
        juce::AudioProcessor::wrapperType_Undefined);
    app.prepareToPlay(48000.0, 128);
    for (int i = 0; i < 500 && app.getModels().getStatus().state !=
                                   evil::ModelManager::State::ready;
         ++i)
      juce::Thread::sleep(10);

    std::unique_ptr<juce::AudioProcessorEditor> appEditor(app.createEditor());
    ok = save(*appEditor, dir.getChildFile("app.png"), 1.5f) && ok;
    if (auto *button = findButton(*appEditor, "settings")) {
      evil::gui::SettingsPanel panel(evil::gui::SettingsPanel::AppActions{});
      juce::CallOutBox box(panel, button->getBounds(),
                           button->getParentComponent());
      ok = save(*appEditor, dir.getChildFile("app-settings.png"), 1.5f) && ok;
    } else {
      std::cout << "FAILED: no settings button in the app" << std::endl;
      ok = false;
    }
    appEditor.reset();
    app.releaseResources();
  }

  processor.editorPage = 3;
  editor.reset(processor.createEditor()); // no key yet: opens on the keys
  ok = save(*editor, dir.getChildFile("agent-keys.png"), 1.5f) && ok;

  agentSettings->set(evil::agent::AgentSettings::tone3000Key, "t3k_pub_not_real");
  processor.getAgent().showForSnapshot(
      {}, {"WXYZ-1234", "https://www.tone3000.com/activate",
           "https://www.tone3000.com/activate?code=WXYZ-1234"});
  editor.reset();
  editor.reset(processor.createEditor());
  ok = save(*editor, dir.getChildFile("agent-sign-in.png"), 1.5f) && ok;
  processor.getAgent().showForSnapshot({});

  using Entry = evil::agent::Session::Entry;
  agentSettings->setApiKey(evil::agent::ProviderId::anthropic, "not-a-real-key");
  editor.reset();
  editor.reset(processor.createEditor());
  ok = save(*editor, dir.getChildFile("agent-empty.png"), 1.5f) && ok;

  const auto entry = [](Entry::Kind kind, const juce::String &text) {
    Entry e;
    e.kind = kind;
    e.text = text;
    return e;
  };
  std::vector<Entry> chat;
  // Made up, and about nothing real: this is only to look at the layout.
  chat.push_back(entry(Entry::Kind::user, "A tone like Example Song. I have an Example Guitar."));
  chat.push_back(entry(Entry::Kind::activity, "searched the web: Example Song guitar gear"));
  chat.push_back(entry(Entry::Kind::activity,
                       "searched the web: tone3000 Example Amp NAM capture"));
  auto loaded = entry(Entry::Kind::activity,
                      "loaded amp: Example Amp by somebody (T3K licence), through the cabinet");
  loaded.sources.push_back({"on TONE3000", "https://www.tone3000.com/tones/example-1"});
  chat.push_back(std::move(loaded));
  chat.push_back(entry(Entry::Kind::activity,
                       "set Input -4.0 dB, EQ 3.2kHz 2.5 dB, OD Drive 3.0, OD Tone 6.0, "
                       "OD Level -1.5 dB, Boost Gain 11.7 dB"));
  auto answer = entry(
      Entry::Kind::assistant,
      "This is where the agent says what the original rig was and how sure it is, what "
      "it set and why, and what's only an approximation.\n\n"
      "Verse: as set.\nChorus: stomp on the overdrive.\nSolo: stomp on the boost.");
  answer.sources.push_back({"Example rig rundown", "https://example.com/rundown"});
  answer.sources.push_back({"Example interview", "https://example.com/interview"});
  answer.before = std::make_shared<juce::XmlElement>("Preset");
  chat.push_back(std::move(answer));
  chat.push_back(entry(Entry::Kind::user, "Make it creamier."));
  chat.push_back(entry(Entry::Kind::error,
                       "Claude says the rate limit or credit has run out."));
  processor.getAgent().showForSnapshot(std::move(chat));
  editor.reset();
  editor.reset(processor.createEditor());
  ok = save(*editor, dir.getChildFile("agent-chat.png"), 1.5f) && ok;

  editor.reset();
  processor.releaseResources();
  return ok ? 0 : 1;
}
