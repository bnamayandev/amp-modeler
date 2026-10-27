#include "PedalBoard.h"

#include "EvilLookAndFeel.h"
#include "state/Parameters.h"

namespace evil::gui {
namespace {
constexpr int captionHeight = 26, gap = 10, rowGap = 12, ampBlockWidth = 50;
constexpr int headerHeight = 44, labelHeight = 16;

const std::array<Pedal::Spec, 12> &pedalSpecs() {
  namespace p = params;
  static const std::array<Pedal::Spec, 12> specs{{
      {"wah", p::wahOn,
       {{{p::wahPosition, "Pedal"}, {p::wahAuto, "Auto"},
         {p::wahResonance, "Peak"}}},
       "Sweep Pedal from heel to toe, or map it to an expression pedal in "
       "your host. Auto opens the filter as you pick harder, for an envelope "
       "filter."},
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
      {"distortion", p::distOn,
       {{{p::distAmount, "Dist"}, {p::distTone, "Tone"},
         {p::distLevel, "Level"}}},
       "Op-amp distortion in the style of the RAT: tighter and more "
       "saturated than the overdrive, with a filter from muffled to raw."},
      {"boost", p::boostOn,
       {{{p::boostGain, "Boost"}, {p::boostBass, "Bass"},
         {p::boostTreble, "Treble"}}},
       "Clean boost with bass and treble shelves: hits the amp harder or "
       "lifts a solo."},
      {"phaser", p::phaserOn,
       {{{p::phaserRate, "Rate"}, {p::phaserDepth, "Depth"},
         {p::phaserFeedback, "Feedback"}}},
       "Four-stage phaser. Feedback sharpens the sweep into a vocal "
       "resonance."},
      {"flanger", p::flangerOn,
       {{{p::flangerRate, "Rate"}, {p::flangerDepth, "Depth"},
         {p::flangerFeedback, "Feedback"}}},
       "Jet-plane flanger. A slow rate and high feedback for the classic "
       "whoosh."},
      {"chorus", p::chorusOn,
       {{{p::chorusRate, "Rate"}, {p::chorusDepth, "Depth"},
         {p::chorusMix, "Mix"}}},
       "Classic single-voice chorus."},
      {"tremolo", p::tremOn,
       {{{p::tremRate, "Rate"}, {p::tremDepth, "Depth"},
         {p::tremShape, "Shape"}}},
       "Volume pulse. Shape goes from a smooth throb to a hard chop."},
      {"delay", p::delayOn,
       {{{p::delayTime, "Time"}, {p::delayFeedback, "Feedback"},
         {p::delayMix, "Mix"}}},
       "Echo with darkening repeats. Moving Time glides like a tape echo."},
      {"reverb", p::reverbOn,
       {{{p::reverbDecay, "Decay"}, {p::reverbTone, "Tone"},
         {p::reverbMix, "Mix"}}},
       "Plate reverb."},
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
  drawLed(g, led, isOn(), colours::accent());

  g.setFont(caption());
  g.setColour(isOn() ? colours::textSoft : colours::mutedSoft);
  g.drawText(isOn() ? "ON" : "BYPASS",
             footswitch.getBounds().withY(footswitch.getBottom() + 2).withHeight(16),
             juce::Justification::centred);
}

void Pedal::resized() {
  auto b = getLocalBounds().reduced(8, 0);
  b.removeFromTop(headerHeight);
  b.removeFromBottom(4);
  // Two by two: the first two knobs, then the third beside the footswitch.
  constexpr int knobRowGap = 8;
  const int rowHeight = (b.getHeight() - knobRowGap) / 2, columnWidth = b.getWidth() / 2;
  auto top = b.removeFromTop(rowHeight);
  b.removeFromTop(knobRowGap);
  knobs[0]->setBounds(top.removeFromLeft(columnWidth));
  knobs[1]->setBounds(top);
  knobs[2]->setBounds(b.removeFromLeft(columnWidth));
  footswitch.setBounds(b.withSizeKeepingCentre(48, 48).translated(0, -labelHeight / 2));
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
  const int rowHeight = (b.getHeight() - rowGap) / 2 - captionHeight;
  const int width = (b.getWidth() - (int)(perRow - 1) * gap) / (int)perRow;
  for (size_t row = 0; row < 2; ++row) {
    if (row == 1)
      b.removeFromTop(rowGap);
    auto caption = b.removeFromTop(captionHeight);
    if (row == 1)
      ampSlot = caption.removeFromLeft(ampBlockWidth).withTrimmedBottom(8);
    auto pedalRow = b.removeFromTop(rowHeight);
    for (size_t i = 0; i < perRow; ++i) {
      pedals[row * perRow + i]->setBounds(pedalRow.removeFromLeft(width));
      pedalRow.removeFromLeft(gap);
    }
    signalY[row] = getLocalArea(pedals[row * perRow].get(),
                                pedals[row * perRow]->getFootswitchBounds())
                       .getCentreY();
  }
}

void PedalBoard::paint(juce::Graphics &g) {
  using namespace colours;
  // Row captions; the amp and cab sit between the two rows.
  g.setFont(caption());
  g.setColour(muted);
  g.drawText("INTO THE AMP", 0, 0, getWidth(), captionHeight - 8,
             juce::Justification::centredLeft);
  g.drawText("AFTER THE CAB",
             ampSlot.withX(ampSlot.getRight() + 10).withRight(getWidth()),
             juce::Justification::centredLeft);

  // Signal path through the gaps (the pedals cover the rest of it).
  g.setColour(line);
  for (int y : signalY)
    g.fillRect(0, y, getWidth(), 1);

  // The amp, as a small block heading the second row.
  const auto amp = ampSlot.toFloat().withSizeKeepingCentre(
      (float)ampBlockWidth, (float)juce::jmin(20, ampSlot.getHeight()));
  g.setColour(background);
  g.fillRoundedRectangle(amp, 6.0f);
  g.setColour(accent().withAlpha(0.4f));
  g.drawRoundedRectangle(amp.reduced(0.5f), 6.0f, 1.0f);
  g.setColour(text);
  g.setFont(mono(11.5f, true));
  g.drawText("amp", amp, juce::Justification::centred);
}
} // namespace evil::gui
