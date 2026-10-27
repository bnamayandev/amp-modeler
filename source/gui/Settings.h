#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

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

  static juce::File getFile();

private:
  juce::PropertiesFile file;
};

/** The settings popover: accent swatches, and a hue strip for anything in
    between. */
class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener {
public:
  explicit SettingsPanel(Settings &);
  ~SettingsPanel() override;

  static constexpr int panelWidth = 300, panelHeight = 118;
  /** The swatch hues, in degrees; the first is the default. */
  static constexpr std::array<float, 9> swatchHues{350.0f, 18.0f,  42.0f,
                                                   95.0f,  150.0f, 185.0f,
                                                   215.0f, 268.0f, 315.0f};

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseMove(const juce::MouseEvent &) override;

private:
  void changeListenerCallback(juce::ChangeBroadcaster *) override { repaint(); }
  juce::Rectangle<float> swatch(size_t index) const;

  Settings &settings;
  juce::Rectangle<int> caption, swatchRow, hueStrip;
  bool draggingHue = false;
};
} // namespace evil::gui
