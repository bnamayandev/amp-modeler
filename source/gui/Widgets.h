#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace evil::gui {
/** Rotary knob attached to a parameter, with its name above and value below. */
class Knob final : public juce::Component {
public:
  Knob(juce::AudioProcessorValueTreeState &state,
       const juce::String &parameterId, const juce::String &title);
  void resized() override;

private:
  juce::Label title;
  juce::Slider slider;
  juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** Vertical peak meter with a dB scale (-60..+6 dBFS) and a clip indicator. */
class LevelMeter final : public juce::Component {
public:
  /** Call at the GUI refresh rate with the peak since the last call. */
  void pushPeak(float peakGain, float secondsSinceLast);
  void paint(juce::Graphics &) override;
  void mouseDown(const juce::MouseEvent &) override {
    clipped = false;
    repaint();
  }

private:
  float levelDb = -100.0f;
  bool clipped = false;
};

/** A titled, rounded panel drawn behind a group of controls. */
void drawPanel(juce::Graphics &g, juce::Rectangle<int> bounds,
               const juce::String &title);
} // namespace evil::gui
