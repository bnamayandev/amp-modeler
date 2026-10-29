#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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

/** Live spectrum on a log-frequency axis spanning lowHz..highHz, drawn as a
    soft filled curve (behind the EQ faders). Feed it samples as they arrive,
    then call refresh() at the GUI rate. */
class Spectrum final : public juce::Component {
public:
  Spectrum(double lowHz, double highHz);
  void push(const float *data, int n) noexcept;
  void refresh(double sampleRate, float secondsSinceLast);
  void paint(juce::Graphics &) override;

private:
  static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
  static constexpr int numPoints = 160;
  static constexpr float floorDb = -90.0f, topDb = -6.0f;

  double lowHz, highHz;
  juce::dsp::FFT fft{fftOrder};
  juce::dsp::WindowingFunction<float> window{
      (size_t)fftSize, juce::dsp::WindowingFunction<float>::hann, false};
  std::array<float, fftSize> history{};
  std::array<float, 2 * fftSize> fftData{};
  std::array<float, numPoints> levelsDb;
  int historyWrite = 0;
  bool fresh = false; // samples pushed since the last refresh
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
