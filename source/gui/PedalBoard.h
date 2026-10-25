#pragma once

#include "Widgets.h"

#include <array>
#include <memory>

namespace evil::gui {
/** One stomp box: a header with its LED, then three knobs and a footswitch in
   a two-by-two grid. Its knobs dim while it is bypassed, like the amp's
   inactive section. */
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
  juce::Rectangle<int> getFootswitchBounds() const noexcept {
    return footswitch.getBounds();
  }

private:
  void setOn(bool on);

  Spec spec;
  std::array<std::unique_ptr<Knob>, 3> knobs;
  Footswitch footswitch;
  std::unique_ptr<juce::ParameterAttachment> onAttachment;
};

/** The pedal page: pedals in signal order, left to right, in two rows: the
   ones into the amp above, the ones after the cab below. */
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
  std::array<int, 2> signalY{};
};
} // namespace evil::gui
