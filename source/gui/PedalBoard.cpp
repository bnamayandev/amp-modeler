#include "PedalBoard.h"

#include "EvilLookAndFeel.h"
#include "state/Parameters.h"

namespace evil::gui {
namespace {
constexpr int captionHeight = 26, gap = 10, ampSlotWidth = 64;
constexpr int headerHeight = 44, footswitchArea = 88;

const std::array<Pedal::Spec, 6> &pedalSpecs() {
  namespace p = params;
  static const std::array<Pedal::Spec, 6> specs{{
      {"compressor", p::compOn,
       {{{p::compSustain, "Sustain"}, {p::compBlend, "Blend"},
         {p::compLevel, "Level"}}},
       "Evens out your picking and lengthens sustain. Blend mixes the "
       "uncompressed signal back in to keep the pick attack."},
      {"fuzz", p::fuzzOn,
       {{{p::fuzzAmount, "Fuzz"}, {p::fuzzTone, "Tone"},
         {p::fuzzLevel, "Level"}}},
       "Hard-clipping fuzz with a scooped tone control: dark and wooly to "
       "the left, cutting to the right."},
      {"overdrive", p::odOn,
       {{{p::odDrive, "Drive"}, {p::odTone, "Tone"}, {p::odLevel, "Level"}}},
       "Mid-hump overdrive. Low drive and high level tightens a high-gain "
       "amp; more drive is a lead tone on its own."},
      {"chorus", p::chorusOn,
       {{{p::chorusRate, "Rate"}, {p::chorusDepth, "Depth"},
         {p::chorusMix, "Mix"}}},
       "Classic single-voice chorus, after the cab."},
      {"delay", p::delayOn,
       {{{p::delayTime, "Time"}, {p::delayFeedback, "Feedback"},
         {p::delayMix, "Mix"}}},
       "Echo with darkening repeats. Moving Time glides like a tape echo."},
      {"reverb", p::reverbOn,
       {{{p::reverbDecay, "Decay"}, {p::reverbTone, "Tone"},
         {p::reverbMix, "Mix"}}},
       "Plate reverb, after the cab."},
  }};
  return specs;
}
} // namespace

Pedal::Pedal(juce::AudioProcessorValueTreeState &state, const Spec &s)
    : spec(s) {
  for (size_t i = 0; i < knobs.size(); ++i) {
    knobs[i] = std::make_unique<Knob>(state, spec.knobs[i].first,
                                      spec.knobs[i].second);
    addAndMakeVisible(*knobs[i]);
  }
  footswitch.setTooltip(juce::String(spec.tooltip) + " (click to switch)");
  footswitch.onClick = [this] {
    onAttachment->setValueAsCompleteGesture(isOn() ? 0.0f : 1.0f);
  };
  addAndMakeVisible(footswitch);
  onAttachment = std::make_unique<juce::ParameterAttachment>(
      *state.getParameter(spec.onId), [this](float v) { setOn(v >= 0.5f); });
  onAttachment->sendInitialUpdate();
}

void Pedal::setOn(bool on) {
  footswitch.setToggleState(on, juce::dontSendNotification);
  for (auto &k : knobs)
    k->setAlpha(on ? 1.0f : 0.45f);
  repaint();
}

void Pedal::paint(juce::Graphics &g) {
  // No chain number: the board's left-to-right order already shows it, and
  // names like "compressor" need the width.
  const auto rule = drawPanel(g, getLocalBounds(), 0, spec.name);
  // LED at the end of the header rule, as on the amp's input panel.
  const auto led = juce::Rectangle<float>(8.0f, 8.0f).withCentre(
      {rule.getRight() - 4.0f, rule.getCentreY()});
  g.setColour(colours::panel);
  g.fillRect(led.expanded(8.0f, 0.0f).withHeight(2.0f).withCentre(
      led.getCentre())); // gap in the rule
  drawLed(g, led, isOn(), colours::accent);

  g.setFont(caption());
  g.setColour(isOn() ? colours::textSoft : colours::mutedSoft);
  g.drawText(isOn() ? "ON" : "BYPASS",
             footswitch.getBounds().withY(footswitch.getBottom() + 2).withHeight(16),
             juce::Justification::centred);
}

void Pedal::resized() {
  auto b = getLocalBounds().reduced(12, 0);
  b.removeFromTop(headerHeight);
  auto foot = b.removeFromBottom(footswitchArea);
  footswitch.setBounds(foot.withSizeKeepingCentre(52, 52).translated(0, -10));
  const int knobHeight = b.getHeight() / 3;
  for (auto &k : knobs)
    k->setBounds(b.removeFromTop(knobHeight));
}

PedalBoard::PedalBoard(juce::AudioProcessorValueTreeState &state) {
  for (size_t i = 0; i < pedals.size(); ++i) {
    pedals[i] = std::make_unique<Pedal>(state, pedalSpecs()[i]);
    addAndMakeVisible(*pedals[i]);
  }
}

int PedalBoard::getNumActive() const noexcept {
  int n = 0;
  for (auto &p : pedals)
    n += p->isOn() ? 1 : 0;
  return n;
}

void PedalBoard::resized() {
  auto b = getLocalBounds();
  b.removeFromTop(captionHeight);
  const int width = (b.getWidth() - ampSlotWidth - 4 * gap) / 6;
  for (size_t i = 0; i < pedals.size(); ++i) {
    pedals[i]->setBounds(b.removeFromLeft(width));
    if (i == 2)
      ampSlot = b.removeFromLeft(ampSlotWidth);
    else if (i < 5)
      b.removeFromLeft(gap);
  }
  signalY = pedals[0]->getBottom() - footswitchArea / 2 - 10;
}

void PedalBoard::paint(juce::Graphics &g) {
  using namespace colours;
  // Group captions above the pedals.
  g.setFont(caption());
  g.setColour(muted);
  const auto preArea = pedals[0]->getBounds().getUnion(pedals[2]->getBounds());
  const auto postArea = pedals[3]->getBounds().getUnion(pedals[5]->getBounds());
  g.drawText("INTO THE AMP", preArea.withY(0).withHeight(captionHeight - 8),
             juce::Justification::centredLeft);
  g.drawText("AFTER THE CAB", postArea.withY(0).withHeight(captionHeight - 8),
             juce::Justification::centredLeft);

  // Signal path through the gaps (the pedals cover the rest of it).
  g.setColour(line);
  g.fillRect(0, signalY, getWidth(), 1);

  // The amp, as a small block on the path.
  const auto amp = ampSlot.toFloat()
                       .withSizeKeepingCentre((float)ampSlotWidth - 14.0f, 26.0f)
                       .withCentre({(float)ampSlot.getCentreX(), (float)signalY});
  g.setColour(background);
  g.fillRoundedRectangle(amp, 6.0f);
  g.setColour(accent.withAlpha(0.4f));
  g.drawRoundedRectangle(amp.reduced(0.5f), 6.0f, 1.0f);
  g.setColour(text);
  g.setFont(mono(11.5f, true));
  g.drawText("amp", amp, juce::Justification::centred);
}
} // namespace evil::gui
