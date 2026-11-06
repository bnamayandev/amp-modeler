#include "StandaloneApp.h"

#include <juce_core/juce_core.h>

#if defined(JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
#include "gui/EvilLookAndFeel.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace evil::standalone {
namespace {
/** JUCE's standalone window without its title bar: the editor's header does
    that job. JUCE keeps a thin border around the content for resizing, painted
    here in the editor's background. */
class Window final : public juce::StandaloneFilterWindow {
public:
  Window(const juce::String &title,
         std::unique_ptr<juce::StandalonePluginHolder> holder)
      : StandaloneFilterWindow(title, gui::colours::background,
                               std::move(holder)) {
    // The Options button is private to JUCE's window, and the only text button
    // directly on it. Hidden, not just shrunk, so it can't take keyboard focus.
    for (auto *child : getChildren())
      if (auto *options = dynamic_cast<juce::TextButton *>(child))
        options->setVisible(false);

    const auto content = getContentComponent()->getBounds();
    setTitleBarButtonsRequired(0, false);
    setTitleBarHeight(0);
    setContentComponentSize(content.getWidth(), content.getHeight());
  }
};

/** Stands in for JUCE's StandaloneFilterApp, which is final and creates its
    own window. Uses the same settings file, so saved state carries over. */
class App final : public juce::JUCEApplication {
public:
  App() {
    juce::PropertiesFile::Options options;
    options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#endif
    properties.setStorageParameters(options);
  }

  const juce::String getApplicationName() override {
    return juce::CharPointer_UTF8(JucePlugin_Name);
  }
  const juce::String getApplicationVersion() override {
    return JucePlugin_VersionString;
  }
  bool moreThanOneInstanceAllowed() override { return true; }
  void anotherInstanceStarted(const juce::String &) override {}

  void initialise(const juce::String &) override {
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {
      quit();
      return;
    }
    window = std::make_unique<Window>(
        getApplicationName(), std::make_unique<juce::StandalonePluginHolder>(
                                  properties.getUserSettings(), false));
    window->setVisible(true);
  }

  void shutdown() override {
    window = nullptr;
    properties.saveIfNeeded();
  }

  void systemRequestedQuit() override {
    if (window != nullptr)
      window->pluginHolder->savePluginState();

    if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
      juce::Timer::callAfterDelay(100, [] {
        if (auto *app = getInstance())
          app->systemRequestedQuit();
      });
    else
      quit();
  }

  Window *getWindow() const { return window.get(); }

private:
  juce::ApplicationProperties properties;
  std::unique_ptr<Window> window;
};

Window *findWindow() {
  auto *app = dynamic_cast<App *>(juce::JUCEApplicationBase::getInstance());
  return app != nullptr ? app->getWindow() : nullptr;
}
} // namespace

void showAudioSettings() {
  if (auto *holder = juce::StandalonePluginHolder::getInstance())
    holder->showAudioSettingsDialog();
}

void saveState() {
  if (auto *holder = juce::StandalonePluginHolder::getInstance())
    holder->askUserToSaveState();
}

void loadState() {
  if (auto *holder = juce::StandalonePluginHolder::getInstance())
    holder->askUserToLoadState();
}

void resetToDefaultState() {
  if (auto *window = findWindow())
    window->resetToDefaultState();
}

void minimiseWindow() {
  if (auto *window = findWindow())
    window->setMinimised(true);
}

void closeWindow() {
  if (auto *window = findWindow())
    window->closeButtonPressed();
}
} // namespace evil::standalone

// With JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP set, JUCE's wrapper calls this.
JUCE_CREATE_APPLICATION_DEFINE(evil::standalone::App)

#else

namespace evil::standalone {
void showAudioSettings() {}
void saveState() {}
void loadState() {}
void resetToDefaultState() {}
void minimiseWindow() {}
void closeWindow() {}
} // namespace evil::standalone

#endif
