// evil-snapshot: renders the plugin editor offscreen to PNG files, without
// opening a window.
//
//   evil-snapshot <output-directory>
//
// Writes editor.png (default size), editor@1.5x.png, stretched.png, which
// simulates a window manager forcing the window to an odd size, to check the
// layout scales uniformly, pedals.png: the pedal page with a few pedals on,
// tuner-idle.png, tuner.png, tuner-in-tune.png and tuner-locked.png: the tuner
// page with a custom tuning, hearing notes played into the processor, and
// settings.png: the settings popover open, with a blue accent. The saved accent
// and tuning are ignored, so every shot starts from the default.

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
    evil::gui::SettingsPanel panel(*settings);
    juce::CallOutBox box(panel, button->getBounds(),
                         button->getParentComponent());
    ok = save(*editor, dir.getChildFile("settings.png"), 1.5f) && ok;
  } else {
    std::cout << "FAILED: no settings button" << std::endl;
    ok = false;
  }

  editor.reset();
  processor.releaseResources();
  return ok ? 0 : 1;
}
