// evil-snapshot: renders the plugin editor offscreen to PNG files, without
// opening a window.
//
//   evil-snapshot <output-directory>
//
// Writes editor.png (default size), editor@1.5x.png, stretched.png, which
// simulates a window manager forcing the window to an odd size, to check the
// layout scales uniformly, pedals.png: the pedal page with a few pedals on,
// and settings.png: the settings popover open, with a blue accent. The saved
// accent is ignored, so every shot starts from the default.

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
