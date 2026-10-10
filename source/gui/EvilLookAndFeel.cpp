#include "EvilLookAndFeel.h"

namespace evil::gui
{
juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

EvilLookAndFeel::EvilLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
    setColour (juce::TextButton::buttonColourId, control);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, control);
    setColour (juce::ComboBox::outlineColourId, controlEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, control);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, controlEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::CaretComponent::caretColourId, accent);
}

void EvilLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace colours;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto trackWidth = juce::jmax (2.5f, radius * 0.1f);
    const auto arcRadius = radius - trackWidth * 0.5f;
    const bool enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (control.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters (e.g. EQ, +/- dB) draw their arc from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                         && std::abs (slider.getMinimum() + slider.getMaximum()) < 1.0e-6;
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? accent : textDim);
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto bodyRadius = radius - trackWidth * 2.2f;
    juce::ColourGradient body (control.brighter (0.25f), centre.x, centre.y - bodyRadius,
                               control.darker (0.4f), centre.x, centre.y + bodyRadius, false);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    g.setColour (controlEdge);
    g.drawEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), 1.0f);

    juce::Path pointer;
    const auto pointerLength = bodyRadius * 0.62f;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 4.0f, 3.0f, pointerLength, 1.5f);
    g.setColour (enabled ? text : textDim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void EvilLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    using namespace colours;
    auto bounds = button.getLocalBounds().toFloat();
    const auto ledSize = juce::jmin (12.0f, bounds.getHeight() - 4.0f);
    const auto led = bounds.removeFromLeft (ledSize + 8.0f).withSizeKeepingCentre (ledSize, ledSize);
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.0f));
    }
    g.setColour (on ? accent : control.brighter (0.2f));
    g.fillEllipse (led);
    g.setColour (highlighted ? text.withAlpha (0.6f) : controlEdge);
    g.drawEllipse (led, 1.0f);

    g.setColour (button.isEnabled() ? text : textDim);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centredLeft, 1);
}

void EvilLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                            bool highlighted, bool down)
{
    using namespace colours;
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = button.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : background;
    if (down)
        fill = fill.darker (0.2f);
    else if (highlighted)
        fill = fill.brighter (0.12f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (button.getToggleState() ? fill.brighter (0.2f) : controlEdge);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void EvilLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                    juce::ComboBox& box)
{
    using namespace colours;
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? accent : controlEdge);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const auto arrowZone = juce::Rectangle<float> ((float) width - 20.0f, 0.0f, 14.0f, (float) height);
    juce::Path arrow;
    arrow.startNewSubPath (arrowZone.getCentreX() - 4.0f, arrowZone.getCentreY() - 2.0f);
    arrow.lineTo (arrowZone.getCentreX(), arrowZone.getCentreY() + 2.0f);
    arrow.lineTo (arrowZone.getCentreX() + 4.0f, arrowZone.getCentreY() - 2.0f);
    g.setColour (textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

juce::Font EvilLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (14.0f, (float) buttonHeight * 0.55f));
}

juce::Font EvilLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (14.0f);
}

juce::Font EvilLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

juce::Font EvilLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Label* EvilLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (uiFont (12.0f));
    label->setColour (juce::Label::textColourId, colours::textDim);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, colours::accent);
    return label;
}
} // namespace evil::gui
