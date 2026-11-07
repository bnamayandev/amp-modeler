#include "PedalBoard.h"

#include "EvilLookAndFeel.h"
#include "state/Parameters.h"

namespace evil::gui {
namespace {
constexpr int captionHeight = 24, gap = 10, rowGap = 8, ampBlockWidth = 54;

const std::array<Pedal::Spec, 12> &pedalSpecs() {
  namespace p = params;
  static const std::array<Pedal::Spec, 12> specs{{
      {"wah", p::wahOn,
       {{{p::wahPosition, "Pedal"}, {p::wahAuto, "Auto"},
         {p::wahResonance, "Peak"}}},
       "After a Dunlop Cry Baby. Pedal is the rocker, heel to toe; Peak at noon "
       "is the stock pedal. Auto isn't on a real one: it opens the filter as you "
       "pick harder, for an envelope filter.",
       0xff2c2d33, Pedal::Art::wah},
      {"compressor", p::compOn,
       {{{p::compSustain, "Sens"}, {p::compBlend, "Blend"},
         {p::compLevel, "Output"}}},
       "After an MXR Dyna Comp: its Sensitivity and Output. Blend isn't on the "
       "original; all the way up is the original.",
       0xff0f4a57, Pedal::Art::compressor},
      {"fuzz", p::fuzzOn,
       {{{p::fuzzAmount, "Sustain"}, {p::fuzzTone, "Tone"},
         {p::fuzzLevel, "Volume"}}},
       "An EHX Big Muff Pi circuit, its knobs where a real one's are. Tone scoops "
       "the mids at noon, dark and wooly to the left, cutting to the right.",
       0xff4a1f4d, Pedal::Art::fuzz},
      {"overdrive", p::odOn,
       {{{p::odDrive, "Drive"}, {p::odTone, "Tone"}, {p::odLevel, "Level"}}},
       "An Ibanez Tube Screamer circuit, its knobs where a real one's are. Thins "
       "the lows and pushes the mids. Low drive and high level tightens a dirty "
       "amp; on a clean amp it's a crunch tone of its own.",
       0xff46531f, Pedal::Art::overdrive},
      {"distortion", p::distOn,
       {{{p::distAmount, "Dist"}, {p::distTone, "Filter"},
         {p::distLevel, "Volume"}}},
       "A ProCo RAT circuit, its knobs where a real one's are: nearly clean with "
       "Dist down, heavy from noon up. Filter darkens clockwise, as on a real one.",
       0xff6a1414, Pedal::Art::distortion},
      {"boost", p::boostOn,
       {{{p::boostGain, "Gain"}, {p::boostBass, "Bass"},
         {p::boostTreble, "Treble"}}},
       "After an MXR Micro Amp: up to 26 dB of clean gain, to hit the amp harder "
       "or lift a solo. Bass and Treble aren't on the original; at noon they're "
       "flat.",
       0xffc08c23, Pedal::Art::boost},
      {"phaser", p::phaserOn,
       {{{p::phaserRate, "Speed"}, {p::phaserDepth, "Depth"},
         {p::phaserFeedback, "Feedback"}}},
       "After an MXR Phase 90: Speed is its one knob. Depth all the way up is the "
       "original's sweep. Feedback sharpens it into a vocal resonance.",
       0xffa2441b, Pedal::Art::phaser},
      {"flanger", p::flangerOn,
       {{{p::flangerRate, "Speed"}, {p::flangerDepth, "Width"},
         {p::flangerFeedback, "Regen"}}},
       "After an MXR M117. A slow speed and high regen for the classic whoosh.",
       0xff2b2d66, Pedal::Art::flanger},
      {"chorus", p::chorusOn,
       {{{p::chorusRate, "Rate"}, {p::chorusDepth, "Depth"},
         {p::chorusMix, "Mix"}}},
       "After a Boss CE-2. Mix isn't on the original: noon is the original, and "
       "all the way up its vibrato.",
       0xff17716b, Pedal::Art::chorus},
      {"tremolo", p::tremOn,
       {{{p::tremRate, "Rate"}, {p::tremDepth, "Depth"},
         {p::tremShape, "Wave"}}},
       "After a Boss TR-2. Wave goes from a triangle, a smooth throb, to a square, "
       "a hard chop.",
       0xff1f4f35, Pedal::Art::tremolo},
      {"delay", p::delayOn,
       {{{p::delayTime, "Delay"}, {p::delayFeedback, "Regen"},
         {p::delayMix, "Mix"}}},
       "After an MXR Carbon Copy: repeats get darker the longer the delay. It goes "
       "well past the original's 600 ms.",
       0xffd4ccb8, Pedal::Art::delay},
      {"reverb", p::reverbOn,
       {{{p::reverbDecay, "Decay"}, {p::reverbTone, "Tone"},
         {p::reverbMix, "Mix"}}},
       "Plate reverb. Not modelled on a particular pedal.",
       0xff142a55, Pedal::Art::reverb},
}};
  return specs;
}

/** A silkscreened drawing of what the pedal does: `shape` takes how far along it
    is, 0..1, and gives the height, -1..1. */
juce::Path waveform(juce::Rectangle<float> area, const std::function<float(float)> &shape,
                    int points = 140) {
  juce::Path path;
  for (int i = 0; i <= points; ++i) {
    const float t = (float)i / (float)points;
    const juce::Point<float> p(area.getX() + t * area.getWidth(),
                               area.getCentreY() - juce::jlimit(-1.0f, 1.0f, shape(t)) *
                                                       area.getHeight() * 0.5f);
    if (i == 0)
      path.startNewSubPath(p);
    else
      path.lineTo(p);
  }
  return path;
}

void drawArt(juce::Graphics &g, juce::Rectangle<float> area, Pedal::Art art, juce::Colour ink) {
  using Art = Pedal::Art;
  constexpr float tau = juce::MathConstants<float>::twoPi;
  const juce::PathStrokeType stroke(1.35f, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);
  const auto draw = [&](const std::function<float(float)> &shape, float alpha = 0.85f) {
    g.setColour(ink.withAlpha(alpha));
    g.strokePath(waveform(area, shape), stroke);
  };
  switch (art) {
  case Art::wah: // a resonant peak, swept
    for (const auto &[peak, alpha] : {std::pair{0.25f, 0.35f}, std::pair{0.5f, 0.55f}, std::pair{0.75f, 0.95f}})
      draw([peak = peak](float t) {
             const float d = (t - peak) / 0.07f;
             return -0.82f + 1.75f * peak * std::exp(-d * d) + 0.45f * peak * std::exp(-d * d * 0.08f);
           }, alpha);
    break;
  case Art::compressor: // peaks held down to one height
    draw([=](float t) {
      const float env = 1.0f - 0.8f * t;
      return juce::jlimit(-0.42f, 0.42f, env * 1.5f * std::sin(tau * 9.0f * t)) +
             (t < 0.12f ? (0.12f - t) * 3.0f * std::sin(tau * 9.0f * t) : 0.0f);
    });
    break;
  case Art::fuzz: // a square wave, sputtering at the top
    draw([=](float t) {
      const float wave = std::sin(tau * 5.0f * t);
      return (wave > 0.0f ? 0.72f : -0.72f) + 0.16f * std::sin(tau * 83.0f * t);
    });
    break;
  case Art::overdrive: // a sine's peaks gently rounded off
    draw([=](float t) { return 0.82f * std::tanh(2.6f * std::sin(tau * 3.5f * t)); });
    break;
  case Art::distortion: // a sine's peaks cut flat
    draw([=](float t) { return juce::jlimit(-0.62f, 0.62f, 2.8f * std::sin(tau * 3.5f * t)); });
    break;
  case Art::boost: // the same wave, only bigger
    draw([=](float t) { return (0.16f + 0.82f * t * t) * std::sin(tau * 7.0f * t); });
    break;
  case Art::phaser: // a wave and its shifted copy
    draw([=](float t) { return 0.7f * std::sin(tau * 3.0f * t); }, 0.45f);
    draw([=](float t) { return 0.7f * std::sin(tau * 3.0f * t + 1.2f + 2.0f * t); });
    break;
  case Art::flanger: // a sweep, up and back
    draw([=](float t) {
      const float sweep = std::sin(juce::MathConstants<float>::pi * t);
      return 0.75f * std::sin(tau * (2.0f + 9.0f * sweep * sweep) * t);
    });
    break;
  case Art::chorus: // voices slightly apart
    for (const auto &[cycles, alpha] : {std::pair{3.0f, 0.35f}, std::pair{3.35f, 0.55f}, std::pair{3.7f, 0.9f}})
      draw([=, cycles = cycles](float t) { return 0.72f * std::sin(tau * cycles * t); }, alpha);
    break;
  case Art::tremolo: // the level pulsing
    draw([=](float t) {
      const float env = 0.5f + 0.5f * std::sin(tau * 2.5f * t - 1.0f);
      return (0.08f + 0.85f * env) * std::sin(tau * 22.0f * t);
    });
    break;
  case Art::delay: // the same note coming back quieter
    draw([=](float t) {
      const float position = t * 4.4f, phase = position - std::floor(position);
      const float height = std::pow(0.62f, std::floor(position));
      return height * std::exp(-phase * 5.0f) * std::sin(tau * 3.2f * phase);
    });
    break;
  case Art::reverb: // a dense tail dying away
    draw([=](float t) {
      return std::exp(-3.6f * t) *
             (0.62f * std::sin(tau * 21.0f * t) + 0.38f * std::sin(tau * 34.0f * t + 1.0f));
    });
    break;
  }
}
} // namespace

Pedal::Pedal(juce::AudioProcessorValueTreeState &state, const Spec &s)
    : spec(s) {
  const juce::Colour finish(spec.paint);
  ink = finish.getPerceivedBrightness() > 0.55f ? juce::Colour(0xff17120c)
                                                : juce::Colour(0xfff3efe5);
  for (size_t i = 0; i < knobs.size(); ++i) {
    knobs[i] = std::make_unique<Knob>(state, spec.knobs[i].first, spec.knobs[i].second,
                                      Knob::Size::pedal);
    knobs[i]->setInk(ink);
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
    k->setAlpha(on ? 1.0f : 0.86f);
  repaint();
}

void Pedal::paintEnclosure(juce::Graphics &g) const {
  const bool on = isOn();
  constexpr float corner = 9.0f;
  const auto body = getLocalBounds().toFloat();
  auto finish = juce::Colour(spec.paint);
  if (!on) // bypassed: the paint goes dull, the printing stays readable
    finish = finish.interpolatedWith(juce::Colour(0xff17171a), 0.34f);

  juce::Path shape;
  shape.addRoundedRectangle(body, corner);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(shape);
    g.setGradientFill(juce::ColourGradient(finish.brighter(0.22f), body.getX(), body.getY(),
                                           finish.darker(0.42f), body.getRight(),
                                           body.getBottom(), false));
    g.fillRect(body);
    // Powder coat: fine speckle, then a sheen where the light pools at the top.
    fillTexture(g, body, Texture::grain, 0.05f);
    fillTexture(g, body, Texture::tolex, 0.1f);
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(on ? 0.13f : 0.05f), body.getCentreX(), body.getY(),
        juce::Colours::white.withAlpha(0.0f), body.getCentreX(), body.getY() + body.getHeight() * 0.5f,
        false));
    g.fillRect(body);
  }
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.drawRoundedRectangle(body.reduced(0.5f), corner, 1.0f);
  g.setColour(juce::Colours::white.withAlpha(on ? 0.3f : 0.14f));
  g.fillRect(body.reduced(corner, 0.0f).withY(body.getY() + 1.0f).withHeight(1.0f));

  constexpr float inset = 7.5f;
  int seed = spec.name[0] + spec.name[1];
  for (const auto at : {body.getTopLeft().translated(inset, inset),
                        body.getTopRight().translated(-inset, inset),
                        body.getBottomLeft().translated(inset, -inset),
                        body.getBottomRight().translated(-inset, -inset)})
    drawScrew(g, at, 2.6f, ++seed);

  // The name, printed large.
  const auto printed = ink.withAlpha(on ? 1.0f : 0.78f);
  g.setColour(printed);
  g.setFont(display(26.0f, 0.01f));
  g.drawFittedText(juce::String(spec.name).toUpperCase(), nameArea,
                   juce::Justification::centred, 1, 0.75f);
  drawArt(g, artArea.toFloat(), spec.art, printed);
}

void Pedal::paint(juce::Graphics &g) {
  juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 10, {0, 4})
      .drawForRectangle(g, getLocalBounds());
  enclosure.draw(g, getLocalBounds(), isOn() ? 1 : 0,
                 [this](juce::Graphics &c) { paintEnclosure(c); });
  drawLed(g, led, isOn(), colours::accent());
}

void Pedal::resized() {
  auto b = getLocalBounds().reduced(3, 0);
  b.removeFromTop(8);
  auto knobRow = b.removeFromTop(74);
  const int column = knobRow.getWidth() / (int)knobs.size();
  for (auto &knob : knobs) {
    knob->setMaxDiameter(column);
    knob->setBounds(knobRow.removeFromLeft(column));
  }
  b.removeFromTop(1);
  nameArea = b.removeFromTop(27).reduced(6, 0);
  artArea = b.removeFromTop(24).reduced(16, 2);
  b.removeFromTop(5);
  led = juce::Rectangle<float>(9.0f, 9.0f).withCentre(
      {(float)b.getCentreX(), (float)b.getY() + 4.5f});
  b.removeFromTop(10);
  footswitch.setBounds(b.withSizeKeepingCentre(58, juce::jmin(58, b.getHeight())));
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
      ampSlot = caption.removeFromLeft(ampBlockWidth).withTrimmedBottom(5);
    auto pedalRow = b.removeFromTop(rowHeight);
    for (size_t i = 0; i < perRow; ++i) {
      pedals[row * perRow + i]->setBounds(pedalRow.removeFromLeft(width));
      pedalRow.removeFromLeft(gap);
    }
  }
}

void PedalBoard::paint(juce::Graphics &g) {
  board.draw(g, getLocalBounds(), 0, [this](juce::Graphics &c) {
    using namespace colours;
    // Row captions; the amp and cab sit between the two rows.
    c.setFont(caption());
    c.setColour(textSoft);
    c.drawText("INTO THE AMP", 0, 0, getWidth(), captionHeight - 5,
               juce::Justification::centredLeft);
    c.drawText("AFTER THE CAB",
               ampSlot.withX(ampSlot.getRight() + 10).withRight(getWidth()),
               juce::Justification::centredLeft);

    // Couplers joining each pedal's jacks to the next.
    for (size_t i = 0; i + 1 < pedals.size(); ++i) {
      if ((i + 1) % perRow == 0)
        continue;
      const auto left = pedals[i]->getBounds();
      const auto coupler = juce::Rectangle<float>((float)left.getRight() - 1.0f,
                                                  (float)left.getCentreY() - 4.0f,
                                                  (float)gap + 2.0f, 8.0f);
      c.setGradientFill(juce::ColourGradient(
          juce::Colour(0xffc7c7cc), 0.0f, coupler.getY(), juce::Colour(0xff36363a), 0.0f,
          coupler.getBottom(), false));
      c.fillRoundedRectangle(coupler, 1.0f);
      c.setColour(juce::Colours::black.withAlpha(0.6f));
      c.drawRoundedRectangle(coupler, 1.0f, 0.7f);
    }

    // The amp, as a small plate heading the second row.
    const auto amp = ampSlot.toFloat();
    c.setGradientFill(juce::ColourGradient(juce::Colour(0xff36363b), 0.0f, amp.getY(),
                                           juce::Colour(0xff1c1c1f), 0.0f, amp.getBottom(), false));
    c.fillRoundedRectangle(amp, 2.5f);
    c.setColour(accent());
    c.drawRoundedRectangle(amp.reduced(0.5f), 2.5f, 1.0f);
    c.setColour(text);
    c.setFont(silk(11.5f, 0.12f));
    c.drawText("AMP", amp, juce::Justification::centred);
  });
}
} // namespace evil::gui
