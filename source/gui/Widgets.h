#pragma once

#include "Hardware.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <vector>

namespace evil::gui {
/** Has a slider ignore typed text with no number in it, which its parameter would
    read as 0: clearing the box shouldn't put an output at 0 dB. Call once the
    slider is attached. */
void ignoreTextWithoutNumber(juce::Slider &);

/** Rotary knob attached to a parameter, with its name above and value below. */
class Knob final : public juce::Component {
public:
  enum class Size {
    regular, // on the amp's panels
    pedal    // smaller, and printed smaller, to fit three across a pedal
  };

  Knob(juce::AudioProcessorValueTreeState &state,
       const juce::String &parameterId, const juce::String &title,
       Size size = Size::regular);
  void resized() override;

  /** The colour its markings are printed in, for a pedal's paint. */
  void setInk(juce::Colour);
  void setMaxDiameter(int diameter) { maxDiameter = diameter; }

private:
  juce::Label title;
  juce::Slider slider;
  juce::AudioProcessorValueTreeState::SliderAttachment attachment;
  const Size size;
  int maxDiameter = 104;
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

/** Vertical LED ladder with a dB scale (-60..+6 dBFS) and a clip light. */
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

/** An analyser screen: the live spectrum on a log-frequency axis spanning
    lowHz..highHz, the shape the EQ's bands make drawn over it, and a graticule
    behind. It sits behind the EQ faders. Feed it samples as they arrive, then
    call refresh() at the GUI rate. */
class Spectrum final : public juce::Component {
public:
  Spectrum(double lowHz, double highHz);
  void push(const float *data, int n) noexcept;
  void refresh(double sampleRate, float secondsSinceLast);
  void paint(juce::Graphics &) override;

  /** Each band's gain, 0 (lowest) to 1 (highest), evenly spaced along the screen. */
  template <size_t numBands>
  void setBands(const std::array<float, numBands> &gains, double maxGainDb) {
    if (bands.size() != numBands || !juce::approximatelyEqual(scaleDb, maxGainDb) ||
        !std::equal(gains.begin(), gains.end(), bands.begin())) {
      bands.assign(gains.begin(), gains.end());
      scaleDb = maxGainDb;
      repaint();
    }
  }

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
  std::vector<float> bands;
  double scaleDb = 0.0;
  PaintCache graticule;
};

/** A chrome stomp switch in its hex nut. Its toggle state is the pedal's on/off. */
class Footswitch final : public juce::Button {
public:
  Footswitch() : juce::Button("footswitch") {}
  void paintButton(juce::Graphics &, bool highlighted, bool down) override;

private:
  PaintCache up, pressed;
};
} // namespace evil::gui
