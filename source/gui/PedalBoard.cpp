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
       "After a Dunlop Cry Baby. Pedal is the rocker, heel to toe; Peak at noon "
       "is the stock pedal. Auto isn't on a real one: it opens the filter as you "
       "pick harder, for an envelope filter."},
      {"compressor", p::compOn,
       {{{p::compSustain, "Sens"}, {p::compBlend, "Blend"},
         {p::compLevel, "Output"}}},
       "After an MXR Dyna Comp: its Sensitivity and Output. Blend isn't on the "
       "original; all the way up is the original."},
      {"fuzz", p::fuzzOn,
       {{{p::fuzzAmount, "Sustain"}, {p::fuzzTone, "Tone"},
         {p::fuzzLevel, "Volume"}}},
       "An EHX Big Muff Pi circuit, its knobs where a real one's are. Tone scoops "
       "the mids at noon, dark and wooly to the left, cutting to the right."},
      {"overdrive", p::odOn,
       {{{p::odDrive, "Drive"}, {p::odTone, "Tone"}, {p::odLevel, "Level"}}},
       "An Ibanez Tube Screamer circuit, its knobs where a real one's are. Thins "
       "the lows and pushes the mids. Low drive and high level tightens a dirty "
       "amp; on a clean amp it's a crunch tone of its own."},
      {"distortion", p::distOn,
       {{{p::distAmount, "Dist"}, {p::distTone, "Filter"},
         {p::distLevel, "Volume"}}},
       "A ProCo RAT circuit, its knobs where a real one's are: nearly clean with "
       "Dist down, heavy from noon up. Filter darkens clockwise, as on a real one."},
      {"boost", p::boostOn,
       {{{p::boostGain, "Gain"}, {p::boostBass, "Bass"},
         {p::boostTreble, "Treble"}}},
       "After an MXR Micro Amp: up to 26 dB of clean gain, to hit the amp harder "
       "or lift a solo. Bass and Treble aren't on the original; at noon they're "
       "flat."},
      {"phaser", p::phaserOn,
       {{{p::phaserRate, "Speed"}, {p::phaserDepth, "Depth"},
         {p::phaserFeedback, "Feedback"}}},
       "After an MXR Phase 90: Speed is its one knob. Depth all the way up is the "
       "original's sweep. Feedback sharpens it into a vocal resonance."},
      {"flanger", p::flangerOn,
       {{{p::flangerRate, "Speed"}, {p::flangerDepth, "Width"},
         {p::flangerFeedback, "Regen"}}},
       "After an MXR M117. A slow speed and high regen for the classic whoosh."},
      {"chorus", p::chorusOn,
       {{{p::chorusRate, "Rate"}, {p::chorusDepth, "Depth"},
         {p::chorusMix, "Mix"}}},
       "After a Boss CE-2. Mix isn't on the original: noon is the original, and "
       "all the way up its vibrato."},
      {"tremolo", p::tremOn,
       {{{p::tremRate, "Rate"}, {p::tremDepth, "Depth"},
         {p::tremShape, "Wave"}}},
       "After a Boss TR-2. Wave goes from a triangle, a smooth throb, to a square, "
       "a hard chop."},
      {"delay", p::delayOn,
       {{{p::delayTime, "Delay"}, {p::delayFeedback, "Regen"},
         {p::delayMix, "Mix"}}},
       "After an MXR Carbon Copy: repeats get darker the longer the delay. It goes "
       "well past the original's 600 ms."},
      {"reverb", p::reverbOn,
       {{{p::reverbDecay, "Decay"}, {p::reverbTone, "Tone"},
         {p::reverbMix, "Mix"}}},
       "Plate reverb. Not modelled on a particular pedal."},
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
