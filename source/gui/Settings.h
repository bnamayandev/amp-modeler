#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>

namespace evil::gui {
/** App-wide preferences (not part of presets or the host's session), shared by
    every open editor through a SharedResourcePointer and kept in
    EvilAmplifier/settings.xml beside the user presets. Broadcasts a change
    when any of them is set. */
class Settings final : public juce::ChangeBroadcaster {
public:
  Settings();

  /** Applies the accent hue (0..1) to colours::accent(), saves it and tells
      the open editors to repaint. */
  void setAccentHue(float hue);

  /** Whether every capture and IR plays at the same volume, as measured when it
      loads, rather than at the level it was made at. On unless turned off. */
  bool isNormalisingVolume() const;
  void setNormalisingVolume(bool);

  static juce::File getFile();

private:
  juce::PropertiesFile file;
};

/** The settings popover: whether to normalise the volume, accent swatches,
    and a hue strip for anything in between. In the standalone app, also the
    audio device and saved states, which a host otherwise looks after. */
class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener {
public:
  /** Each runs after the popover has closed, so it may delete the editor. */
  struct AppActions {
    std::function<void()> audioSettings, saveState, loadState, resetState;
  };

  explicit SettingsPanel(std::optional<AppActions> = std::nullopt);
  ~SettingsPanel() override;

  static constexpr int panelWidth = 300, panelHeight = 188,
                       appSectionHeight = 138;
  /** The swatch hues, in degrees; the first is the default. */
  static constexpr std::array<float, 9> swatchHues{10.0f,  350.0f, 40.0f,
                                                   95.0f,  150.0f, 185.0f,
                                                   215.0f, 268.0f, 315.0f};

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;

private:
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  juce::Rectangle<float> swatch(size_t index) const;
  void addAction(juce::TextButton &, const juce::String &tooltip,
                 std::function<void()> action);

  // Its own reference: resetting the app deletes the editor, and the editor's
  // reference with it, while this panel is still closing.
  juce::SharedResourcePointer<Settings> settings;
  const bool hasAppSection;
  juce::TextButton audioButton{"audio/midi settings..."},
      saveButton{"save..."}, loadButton{"load..."}, resetButton{"reset"};
  juce::ToggleButton normaliseButton{"normalise volume"};
  juce::Rectangle<int> audioCaption, stateCaption, levelsCaption, caption, swatchRow,
      hueStrip;
  bool draggingHue = false;
};
} // namespace evil::gui
