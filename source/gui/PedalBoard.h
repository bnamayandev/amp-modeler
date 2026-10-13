#pragma once

#include "Widgets.h"

#include <array>
#include <memory>

namespace evil::gui {
/** One stomp box: a header with its LED, three knobs, a footswitch. Its knobs
   dim while it is bypassed, like the amp's inactive section. */
class Pedal final : public juce::Component {
public:
  struct Spec {
    const char *name;
    const char *onId;
    std::array<std::pair<const char *, const char *>, 3> knobs; // id, title
    const char *tooltip;
  };

  Pedal(juce::AudioProcessorValueTreeState &state, const Spec &spec);

  void paint(juce::Graphics &) override;
  void resized() override;
  bool isOn() const noexcept { return footswitch.getToggleState(); }

private:
  void setOn(bool on);

  Spec spec;
  std::array<std::unique_ptr<Knob>, 3> knobs;
  Footswitch footswitch;
  std::unique_ptr<juce::ParameterAttachment> onAttachment;
};

/** The pedal page: pedals in signal order, left to right, split where the amp
   sits between the drives and the time-based effects. */
class PedalBoard final : public juce::Component {
public:
  explicit PedalBoard(juce::AudioProcessorValueTreeState &state);

  void paint(juce::Graphics &) override;
  void resized() override;

  int getNumActive() const noexcept;

private:
  std::array<std::unique_ptr<Pedal>, 6> pedals;
  juce::Rectangle<int> ampSlot;
  int signalY = 0;
};
} // namespace evil::gui
