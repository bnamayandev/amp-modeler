#include "Widgets.h"

#include "EvilLookAndFeel.h"

namespace evil::gui
{
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& titleText)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (state, parameterId, slider)
{
    title.setText (titleText.toUpperCase(), juce::dontSendNotification);
    title.setFont (uiFont (12.0f, true));
    title.setColour (juce::Label::textColourId, colours::textDim);
    title.setJustificationType (juce::Justification::centred);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    if (auto* p = state.getParameter (parameterId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    slider.setTooltip (titleText + " (double-click to reset)");
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto b = getLocalBounds();
    title.setBounds (b.removeFromTop (16));
    // Keep knobs a consistent size however big their panel is.
    constexpr int textHeight = 20, maxDiameter = 104;
    const int diameter = juce::jmin (b.getWidth(), b.getHeight() - textHeight, maxDiameter);
    slider.setBounds (b.withSizeKeepingCentre (juce::jmax (diameter, 84), diameter + textHeight));
}

void LevelMeter::pushPeak (float peakGain, float secondsSinceLast)
{
    const float db = juce::Decibels::gainToDecibels (peakGain, -100.0f);
    const float fallDb = 30.0f * secondsSinceLast; // 30 dB/s release
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - fallDb);
    if (peakGain >= 1.0f)
        clipped = true;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    constexpr float minDb = -60.0f, maxDb = 6.0f;
    auto bounds = getLocalBounds().toFloat();
    const auto clipLed = bounds.removeFromTop (6.0f);
    bounds.removeFromTop (3.0f);

    g.setColour (clipped ? colours::accent : colours::control);
    g.fillRoundedRectangle (clipLed, 2.0f);

    g.setColour (colours::control);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto toY = [&] (float db) {
        return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, bounds.getBottom(), bounds.getY());
    };
    const auto top = toY (levelDb);
    if (top < bounds.getBottom())
    {
        juce::ColourGradient fill (colours::accent, 0.0f, toY (0.0f), colours::good, 0.0f, toY (-30.0f), false);
        fill.addColour (0.5, colours::warn);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (bounds.withTop (top), 3.0f);
    }

    g.setColour (colours::background.withAlpha (0.8f));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        g.fillRect (bounds.getX(), toY (db), bounds.getWidth(), 1.0f);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    const auto b = bounds.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (colours::accent);
    g.fillRect (b.getX() + 14.0f, b.getY() + 15.0f, 3.0f, 12.0f);
    g.setColour (colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (title.toUpperCase(), bounds.getX() + 23, bounds.getY() + 10, bounds.getWidth() - 40, 22,
                juce::Justification::centredLeft);
}
} // namespace evil::gui
