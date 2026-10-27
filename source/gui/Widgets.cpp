#include "Widgets.h"

#include "EvilLookAndFeel.h"

namespace evil::gui {
Knob::Knob(juce::AudioProcessorValueTreeState &state,
           const juce::String &parameterId, const juce::String &titleText)
    : slider(juce::Slider::RotaryHorizontalVerticalDrag,
             juce::Slider::TextBoxBelow),
      attachment(state, parameterId, slider) {
  title.setText(titleText.toUpperCase(), juce::dontSendNotification);
  title.setFont(caption());
  title.setColour(juce::Label::textColourId, colours::muted);
  title.setJustificationType(juce::Justification::centred);
  title.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(title);

  slider.setRotaryParameters(juce::degreesToRadians(225.0f),
                             juce::degreesToRadians(495.0f), true);
  slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
  slider.setColour(juce::Slider::textBoxOutlineColourId,
                   juce::Colours::transparentBlack);
  slider.setPopupDisplayEnabled(false, false, nullptr);
  if (auto *p = state.getParameter(parameterId))
    slider.setDoubleClickReturnValue(true,
                                     p->convertFrom0to1(p->getDefaultValue()));
  slider.setTooltip(titleText + " (double-click to reset)");
  addAndMakeVisible(slider);
}

void Knob::resized() {
  auto b = getLocalBounds();
  title.setBounds(b.removeFromTop(16));
  // Keep knobs a consistent size however big their panel is.
  constexpr int textHeight = 20, maxDiameter = 104;
  const int diameter =
      juce::jmin(b.getWidth(), b.getHeight() - textHeight, maxDiameter);
  slider.setBounds(
      b.withSizeKeepingCentre(juce::jmax(diameter, 84), diameter + textHeight));
}

Fader::Fader(juce::AudioProcessorValueTreeState &state,
             const juce::String &parameterId, const juce::String &titleText)
    : attachment(state, parameterId, slider) {
  for (auto *l : {&value, &title}) {
    l->setJustificationType(juce::Justification::centred);
    l->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(l);
  }
  title.setText(titleText.toUpperCase(), juce::dontSendNotification);
  title.setFont(caption());
  title.setColour(juce::Label::textColourId, colours::muted);
  value.setFont(mono(10.5f));

  // Compact, to fit a narrow column: "+3.5", "0.0", "-12.0".
  slider.onValueChange = [this] {
    const auto v = slider.getValue();
    const bool flat = std::abs(v) < 0.05;
    value.setText((v >= 0.05 ? "+" : "") + juce::String(flat ? 0.0 : v, 1),
                  juce::dontSendNotification);
    value.setColour(juce::Label::textColourId,
                    flat ? colours::mutedSoft : colours::textSoft);
  };
  slider.onValueChange();
  slider.setPopupDisplayEnabled(false, false, nullptr);
  if (auto *p = state.getParameter(parameterId)) {
    slider.setDoubleClickReturnValue(true,
                                     p->convertFrom0to1(p->getDefaultValue()));
    slider.setTooltip(p->getName(64) + " (double-click to reset)");
  }
  addAndMakeVisible(slider);
}

void Fader::resized() {
  auto b = getLocalBounds();
  value.setBounds(b.removeFromTop(16));
  title.setBounds(b.removeFromBottom(16));
  slider.setBounds(b.reduced(0, 2));
}

void LevelMeter::pushPeak(float peakGain, float secondsSinceLast) {
  const float db = juce::Decibels::gainToDecibels(peakGain, -100.0f);
  const float fallDb = 30.0f * secondsSinceLast; // 30 dB/s release
  levelDb = db > levelDb ? db : juce::jmax(db, levelDb - fallDb);
  if (peakGain >= 1.0f)
    clipped = true;
  repaint();
}

void LevelMeter::paint(juce::Graphics &g) {
  constexpr float minDb = -60.0f, maxDb = 6.0f;
  auto bounds = getLocalBounds().toFloat();
  const auto clipLed = bounds.removeFromTop(4.0f);
  bounds.removeFromTop(5.0f);

  if (clipped) {
    g.setColour(colours::accent().withAlpha(0.35f));
    g.fillRoundedRectangle(clipLed.expanded(2.0f), 3.0f);
  }
  g.setColour(clipped ? colours::accent() : colours::lineSoft);
  g.fillRoundedRectangle(clipLed, 2.0f);

  g.setColour(colours::lineSoft);
  g.fillRoundedRectangle(bounds, 2.0f);

  const auto toY = [&](float db) {
    return juce::jmap(juce::jlimit(minDb, maxDb, db), minDb, maxDb,
                      bounds.getBottom(), bounds.getY());
  };
  const auto top = toY(levelDb);
  if (top < bounds.getBottom()) {
    // Neutral up to -6 dBFS; only the hot zone uses the accent.
    juce::ColourGradient fill(colours::mutedSoft, 0.0f, bounds.getBottom(),
                              colours::text, 0.0f, toY(-6.0f), false);
    g.setGradientFill(fill);
    const auto bar = bounds.withTop(top);
    g.fillRoundedRectangle(bar, 2.0f);
    const auto hot = bar.withBottom(juce::jmax(bar.getY(), toY(-6.0f)));
    if (!hot.isEmpty()) {
      g.setColour(colours::accent());
      g.fillRect(hot);
    }
  }

  g.setColour(colours::panel);
  for (float db : {0.0f, -12.0f, -24.0f, -36.0f, -48.0f})
    g.fillRect(bounds.getX(), toY(db), bounds.getWidth(), 1.0f);
}

void Footswitch::paintButton(juce::Graphics &g, bool highlighted, bool down) {
  using namespace colours;
  const bool on = getToggleState();
  const auto size =
      (float)juce::jmin(getWidth(), getHeight()) - 2.0f;
  const auto outer =
      juce::Rectangle<float>(size, size).withCentre(getLocalBounds().toFloat().getCentre());

  // Hairline bezel, accent when engaged.
  g.setColour(on ? colours::accent().withAlpha(highlighted ? 0.9f : 0.7f)
              : highlighted ? muted.withAlpha(0.7f)
                            : line);
  g.drawEllipse(outer.reduced(0.5f), 1.0f);

  // The cap: paper colour with a soft top light, pressed in while held.
  const auto cap = outer.reduced(size * (down ? 0.2f : 0.17f));
  g.setGradientFill(juce::ColourGradient(
      background.brighter(down ? 0.04f : 0.12f), cap.getCentreX(), cap.getY(),
      background.darker(0.2f), cap.getCentreX(), cap.getBottom(), false));
  g.fillEllipse(cap);
  g.setColour(highlighted ? muted.withAlpha(0.6f) : line);
  g.drawEllipse(cap.reduced(0.5f), 1.0f);
}

void drawLed(juce::Graphics &g, juce::Rectangle<float> led, bool lit,
             juce::Colour colour) {
  if (lit) {
    g.setGradientFill(juce::ColourGradient(
        colour.withAlpha(0.55f), led.getCentre(), colour.withAlpha(0.0f),
        led.getCentre().translated(led.getWidth() * 1.25f, 0.0f), true));
    g.fillEllipse(led.expanded(led.getWidth() * 0.75f));
  }
  g.setColour(lit ? colour : colours::lineSoft);
  g.fillEllipse(led);
  if (!lit) {
    g.setColour(colours::line);
    g.drawEllipse(led.reduced(0.5f), 1.0f);
  }
}

juce::Rectangle<float> drawPanel(juce::Graphics &g, juce::Rectangle<int> bounds,
                                 int number, const juce::String &title) {
  using namespace colours;
  const auto b = bounds.toFloat();
  g.setColour(panel);
  g.fillRoundedRectangle(b, 10.0f);
  g.setColour(line);
  g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);

  // Section header:  01  [ title ]  ------------------------------
  auto row = b.reduced(16.0f, 0.0f).withTop(b.getY() + 14.0f).withHeight(20.0f);
  if (number > 0) {
    const auto numberFont = mono(11.0f);
    const auto numberText = juce::String(number).paddedLeft('0', 2);
    g.setFont(numberFont);
    g.setColour(accent());
    g.drawText(numberText,
               row.removeFromLeft(juce::GlyphArrangement::getStringWidth(
                                      numberFont, numberText) +
                                  1.0f),
               juce::Justification::centredLeft, false);
    row.removeFromLeft(10.0f);
  }

  const auto titleFont = mono(10.5f, true, 0.1f);
  const auto titleText = title.toLowerCase();
  const auto pill = row.removeFromLeft(
      juce::GlyphArrangement::getStringWidth(titleFont, titleText) + 18.0f);
  g.setColour(accent().withAlpha(0.28f));
  g.fillRect(pill);
  g.setColour(text);
  g.setFont(titleFont);
  g.drawText(titleText, pill.withTrimmedLeft(9.0f),
             juce::Justification::centredLeft, false);
  row.removeFromLeft(12.0f);

  g.setColour(line);
  g.fillRect(row.withSizeKeepingCentre(row.getWidth(), 1.0f));
  return row;
}
} // namespace evil::gui
