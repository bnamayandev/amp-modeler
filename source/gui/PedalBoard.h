#pragma once

#include "Widgets.h"

#include <array>
#include <memory>

namespace evil::gui {
/** One stomp box in its own paint: three knobs across the top, its name and a
   drawing of what it does to a signal, then its light and footswitch. While
   bypassed its paint goes dull and its light goes out. */
class Pedal final : public juce::Component {
public:
  /** What's silkscreened on it, as a waveform or a response. */
  enum class Art {
    wah,
    compressor,
    fuzz,
    overdrive,
    distortion,
    boost,
    phaser,
    flanger,
    chorus,
    tremolo,
    delay,
    reverb
  };

  struct Spec {
    const char *name;
    const char *onId;
    std::array<std::pair<const char *, const char *>, 3> knobs; // id, title
    const char *tooltip;
    juce::uint32 paint;
    Art art;
  };

  Pedal(juce::AudioProcessorValueTreeState &state, const Spec &spec);

  void paint(juce::Graphics &) override;
  void resized() override;
  bool isOn() const noexcept { return footswitch.getToggleState(); }

private:
  void setOn(bool on);
  void paintEnclosure(juce::Graphics &) const;

  Spec spec;
  juce::Colour ink;
  std::array<std::unique_ptr<Knob>, 3> knobs;
  Footswitch footswitch;
  std::unique_ptr<juce::ParameterAttachment> onAttachment;
  juce::Rectangle<int> nameArea, artArea;
  juce::Rectangle<float> led;
  PaintCache enclosure;
};

/** The pedal page: pedals in signal order, left to right, on a board in two
   rows: the ones into the amp above, the ones after the cab below. */
class PedalBoard final : public juce::Component {
public:
  explicit PedalBoard(juce::AudioProcessorValueTreeState &state);

  void paint(juce::Graphics &) override;
  void resized() override;

  int getNumActive() const noexcept;

private:
  static constexpr size_t perRow = 6;

  std::array<std::unique_ptr<Pedal>, 2 * perRow> pedals;
  juce::Rectangle<int> ampSlot;
  PaintCache board;
};
} // namespace evil::gui
