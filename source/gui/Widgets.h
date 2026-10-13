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

/** Vertical fader attached to a bipolar parameter (a graphic EQ band), with
    its value above and its title below. */
class Fader final : public juce::Component {
public:
  Fader(juce::AudioProcessorValueTreeState &state,
        const juce::String &parameterId, const juce::String &title);
  void resized() override;

private:
  juce::Label value, title;
  juce::Slider slider{juce::Slider::LinearVertical, juce::Slider::NoTextBox};
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

/** Round stomp switch. Its toggle state is the pedal's on/off. */
class Footswitch final : public juce::Button {
public:
  Footswitch() : juce::Button("footswitch") {}
  void paintButton(juce::Graphics &, bool highlighted, bool down) override;
};

/** A small status light with a soft accent glow when lit. */
void drawLed(juce::Graphics &g, juce::Rectangle<float> led, bool lit,
             juce::Colour colour);

/** A rounded panel drawn behind a group of controls, headed by its number in
    the signal chain (omitted if 0), a pill-tag title and a rule. Returns the rule's row, so
    callers can put an indicator at its end. */
juce::Rectangle<float> drawPanel(juce::Graphics &g, juce::Rectangle<int> bounds,
                                 int number, const juce::String &title);
} // namespace evil::gui
