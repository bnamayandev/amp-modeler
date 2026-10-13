#include "EvilLookAndFeel.h"

#include <EvilBinaryData.h>

namespace evil::gui {
namespace {
juce::Typeface::Ptr load(const char *data, int size) {
  return juce::Typeface::createSystemTypefaceFor(data, (size_t)size);
}

juce::Font makeFont(const juce::Typeface::Ptr &face, float size,
                    float tracking) {
  return juce::Font(juce::FontOptions(face).withPointHeight(size).withKerningFactor(
      tracking));
}

/** Accent at a given strength over transparent, like CSS color-mix(). */
juce::Colour tint(float amount) { return colours::accent.withAlpha(amount); }

void drawGlow(juce::Graphics &g, juce::Point<float> centre, float radius) {
  juce::ColourGradient glow(tint(0.55f), centre, tint(0.0f),
                            centre.translated(radius, 0.0f), true);
  g.setGradientFill(glow);
  g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                    .withCentre(centre));
}

bool isPrimary(const juce::Button &button) {
  return button.getProperties()["primary"];
}

bool isTab(const juce::Button &button) {
  return button.getProperties()["tab"];
}

juce::TextLayout layoutTooltip(const juce::String &text, juce::Colour colour) {
  juce::AttributedString s;
  s.setJustification(juce::Justification::centredLeft);
  s.append(text, mono(11.5f), colour);
  juce::TextLayout layout;
  layout.createLayoutWithBalancedLineLengths(s, 340.0f);
  return layout;
}
} // namespace

Typefaces::Typefaces()
    : sans(load(EvilBinaryData::InterRegular_ttf,
                EvilBinaryData::InterRegular_ttfSize)),
      sansMedium(load(EvilBinaryData::InterMedium_ttf,
                      EvilBinaryData::InterMedium_ttfSize)),
      mono(load(EvilBinaryData::JetBrainsMonoRegular_ttf,
                EvilBinaryData::JetBrainsMonoRegular_ttfSize)),
      monoMedium(load(EvilBinaryData::JetBrainsMonoMedium_ttf,
                      EvilBinaryData::JetBrainsMonoMedium_ttfSize)) {}

juce::Font sans(float size, bool medium, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(medium ? faces->sansMedium : faces->sans, size, tracking);
}

juce::Font mono(float size, bool medium, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(medium ? faces->monoMedium : faces->mono, size, tracking);
}

void setPrimary(juce::Button &button) {
  button.getProperties().set("primary", true);
}

void setTab(juce::Button &button) {
  button.getProperties().set("tab", true);
}

EvilLookAndFeel::EvilLookAndFeel() {
  using namespace colours;
  setDefaultSansSerifTypeface(typefaces->sans);

  setColour(juce::ResizableWindow::backgroundColourId, background);
  setColour(juce::Label::textColourId, text);
  setColour(juce::Slider::textBoxTextColourId, textSoft);
  setColour(juce::Slider::textBoxOutlineColourId,
            juce::Colours::transparentBlack);
  setColour(juce::Slider::textBoxBackgroundColourId,
            juce::Colours::transparentBlack);
  setColour(juce::Slider::textBoxHighlightColourId, tint(0.32f));
  setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
  setColour(juce::TextButton::buttonOnColourId, tint(0.18f));
  setColour(juce::TextButton::textColourOffId, textSoft);
  setColour(juce::TextButton::textColourOnId, text);
  setColour(juce::ComboBox::backgroundColourId,
            juce::Colours::transparentBlack);
  setColour(juce::ComboBox::outlineColourId, line);
  setColour(juce::ComboBox::textColourId, text);
  setColour(juce::ComboBox::arrowColourId, mutedSoft);
  setColour(juce::PopupMenu::backgroundColourId, panel);
  setColour(juce::PopupMenu::textColourId, textSoft);
  setColour(juce::PopupMenu::headerTextColourId, muted);
  setColour(juce::PopupMenu::highlightedBackgroundColourId, tint(0.22f));
  setColour(juce::PopupMenu::highlightedTextColourId, text);
  setColour(juce::ToggleButton::textColourId, textSoft);
  setColour(juce::TextEditor::backgroundColourId, panel);
  setColour(juce::TextEditor::textColourId, text);
  setColour(juce::TextEditor::highlightColourId, tint(0.32f));
  setColour(juce::TextEditor::highlightedTextColourId, text);
  setColour(juce::TextEditor::outlineColourId, line);
  setColour(juce::TextEditor::focusedOutlineColourId, accent);
  setColour(juce::AlertWindow::backgroundColourId, panel);
  setColour(juce::AlertWindow::textColourId, textSoft);
  setColour(juce::AlertWindow::outlineColourId, line);
  setColour(juce::CaretComponent::caretColourId, accent);
  setColour(juce::TooltipWindow::backgroundColourId, panel);
  setColour(juce::TooltipWindow::textColourId, textSoft);
  setColour(juce::TooltipWindow::outlineColourId, line);
}

void EvilLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                       int width, int height, float sliderPos,
                                       float startAngle, float endAngle,
                                       juce::Slider &slider) {
  using namespace colours;
  const auto bounds =
      juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
  const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
  const auto centre = bounds.getCentre();
  const auto angle = startAngle + sliderPos * (endAngle - startAngle);
  const auto arcRadius = radius - 1.5f;
  const bool enabled = slider.isEnabled();
  const bool hot = enabled && (slider.isMouseOverOrDragging());

  // Hairline track, with the value drawn over it in the accent.
  juce::Path track;
  track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                      startAngle, endAngle, true);
  g.setColour(line);
  g.strokePath(track, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

  // Bipolar parameters (e.g. EQ, +/- dB) draw their arc from the centre.
  const bool bipolar =
      slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 &&
      std::abs(slider.getMinimum() + slider.getMaximum()) < 1.0e-6;
  const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
  const auto valueColour = !enabled ? mutedSoft : hot ? accentHover : accent;
  juce::Path value;
  value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                      juce::jmin(from, angle), juce::jmax(from, angle), true);
  g.setColour(valueColour);
  g.strokePath(value, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

  // Marker at the arc's end, glowing like the web accent.
  const auto tip = centre.getPointOnCircumference(arcRadius, angle);
  if (enabled)
    drawGlow(g, tip, 9.0f);
  g.setColour(valueColour);
  g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(tip));

  // Flat body: paper colour with a soft top light and a hairline edge.
  const auto bodyRadius = radius - 10.0f;
  const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f)
                        .withCentre(centre);
  g.setGradientFill(juce::ColourGradient(background.brighter(0.12f), centre.x,
                                         body.getY(), background.darker(0.15f),
                                         centre.x, body.getBottom(), false));
  g.fillEllipse(body);
  g.setColour(hot ? muted.withAlpha(0.6f) : line);
  g.drawEllipse(body.reduced(0.5f), 1.0f);

  // Pointer: a short hairline from inside the rim towards the centre.
  const auto outer = centre.getPointOnCircumference(bodyRadius - 5.0f, angle);
  const auto inner = centre.getPointOnCircumference(bodyRadius * 0.45f, angle);
  g.setColour(enabled ? text : muted);
  g.drawLine({inner, outer}, 2.0f);
}

void EvilLookAndFeel::drawLinearSlider(juce::Graphics &g, int x, int y,
                                       int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style,
                                       juce::Slider &slider) {
  if (style != juce::Slider::LinearBar) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);
    return;
  }
  // A compact drag field: outlined like a combo box, the value as a faint
  // accent fill from the left.
  using namespace colours;
  const auto bounds =
      juce::Rectangle<int>(x, y, width, height).toFloat().reduced(0.5f);
  const bool hot = slider.isEnabled() && slider.isMouseOverOrDragging();
  juce::Path outline;
  outline.addRoundedRectangle(bounds, 6.0f);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(outline);
    g.setColour(hot ? text.withAlpha(0.04f) : juce::Colours::transparentBlack);
    g.fillRect(bounds);
    g.setColour(tint(hot ? 0.22f : 0.14f));
    g.fillRect(bounds.withRight(juce::jlimit(bounds.getX(), bounds.getRight(),
                                             sliderPos)));
  }
  g.setColour(hot ? muted.withAlpha(0.7f) : line);
  g.strokePath(outline, juce::PathStrokeType(1.0f));
}

void EvilLookAndFeel::drawToggleButton(juce::Graphics &g,
                                       juce::ToggleButton &button,
                                       bool highlighted, bool) {
  using namespace colours;
  auto bounds = button.getLocalBounds().toFloat();
  const bool on = button.getToggleState();
  const auto track = bounds.removeFromLeft(26.0f).withSizeKeepingCentre(26.0f,
                                                                        14.0f);
  bounds.removeFromLeft(9.0f);

  // Pill switch: hairline when off, accent-tinted with a glowing thumb when on.
  g.setColour(on ? tint(0.28f) : juce::Colours::transparentBlack);
  g.fillRoundedRectangle(track, 7.0f);
  g.setColour(on ? accent : highlighted ? muted : line);
  g.drawRoundedRectangle(track.reduced(0.5f), 6.5f, 1.0f);

  const auto thumb = juce::Point<float>(
      on ? track.getRight() - 7.0f : track.getX() + 7.0f, track.getCentreY());
  if (on)
    drawGlow(g, thumb, 9.0f);
  g.setColour(on ? (highlighted ? accentHover : accent)
                 : (highlighted ? muted : mutedSoft));
  g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(thumb));

  g.setColour(!button.isEnabled() ? mutedSoft
              : on || highlighted ? text
                                  : textSoft);
  g.setFont(mono(12.0f));
  g.drawFittedText(button.getButtonText(), bounds.toNearestInt(),
                   juce::Justification::centredLeft, 1);
}

void EvilLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                           juce::Button &button,
                                           const juce::Colour &, bool highlighted,
                                           bool down) {
  using namespace colours;
  if (isTab(button)) {
    // Underline sitting on the header rule below.
    const auto under =
        button.getLocalBounds().toFloat().removeFromBottom(2.0f).reduced(4.0f, 0.0f);
    if (button.getToggleState()) {
      g.setColour(accent);
      g.fillRect(under);
    } else if (highlighted) {
      g.setColour(muted.withAlpha(0.5f));
      g.fillRect(under.withTrimmedTop(1.0f));
    }
    return;
  }
  const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
  juce::Colour fill, edge;
  if (button.getToggleState()) { // selected segment
    fill = tint(down ? 0.34f : highlighted ? 0.26f : 0.18f);
    edge = tint(0.7f);
  } else if (isPrimary(button)) {
    fill = tint(down ? 0.34f : highlighted ? 0.26f : 0.15f);
    edge = highlighted ? accent : tint(0.4f);
  } else {
    fill = text.withAlpha(down ? 0.08f : highlighted ? 0.04f : 0.0f);
    edge = highlighted ? muted.withAlpha(0.7f) : line;
  }
  g.setColour(fill);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(edge);
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void EvilLookAndFeel::drawButtonText(juce::Graphics &g,
                                     juce::TextButton &button, bool highlighted,
                                     bool) {
  using namespace colours;
  if (isTab(button)) {
    g.setColour(button.getToggleState() ? text
                : highlighted           ? textSoft
                                        : muted);
    g.setFont(mono(13.0f, button.getToggleState()));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedBottom(4),
                     juce::Justification::centred, 1);
    return;
  }
  const bool primary = isPrimary(button) && !button.getToggleState();
  g.setColour(!button.isEnabled()           ? mutedSoft
              : primary && highlighted      ? accentHover
              : primary                     ? text
              : button.getToggleState()     ? text
              : highlighted                 ? text
                                            : textSoft);
  g.setFont(getTextButtonFont(button, button.getHeight()));
  g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 2),
                   juce::Justification::centred, 1);
}

void EvilLookAndFeel::drawComboBox(juce::Graphics &g, int width, int height,
                                   bool, int, int, int, int,
                                   juce::ComboBox &box) {
  using namespace colours;
  const auto bounds =
      juce::Rectangle<int>(width, height).toFloat().reduced(0.5f);
  const bool hot = box.isEnabled() && box.isMouseOver(true);
  g.setColour(hot ? text.withAlpha(0.04f) : juce::Colours::transparentBlack);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(box.hasKeyboardFocus(true) ? accent
              : hot                      ? muted.withAlpha(0.7f)
                                         : line);
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  const auto arrowZone =
      juce::Rectangle<float>((float)width - 22.0f, 0.0f, 14.0f, (float)height);
  juce::Path arrow;
  arrow.startNewSubPath(arrowZone.getCentreX() - 3.5f,
                        arrowZone.getCentreY() - 1.75f);
  arrow.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 1.75f);
  arrow.lineTo(arrowZone.getCentreX() + 3.5f, arrowZone.getCentreY() - 1.75f);
  g.setColour(hot ? accent : mutedSoft);
  g.strokePath(arrow, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void EvilLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width,
                                              int height) {
  g.fillAll(colours::panel);
  g.setColour(colours::line);
  g.drawRect(0, 0, width, height, 1);
}

void EvilLookAndFeel::drawCornerResizer(juce::Graphics &g, int w, int h,
                                        bool isMouseOver,
                                        bool isMouseDragging) {
  g.setColour(isMouseDragging ? colours::accent
              : isMouseOver   ? colours::muted
                              : colours::mutedSoft);
  for (float i = 0.3f; i < 1.0f; i += 0.3f)
    g.drawLine((float)w * i, (float)h, (float)w, (float)h * i, 1.0f);
}

void EvilLookAndFeel::drawTooltip(juce::Graphics &g, const juce::String &text,
                                  int width, int height) {
  const auto bounds = juce::Rectangle<int>(width, height).toFloat();
  g.fillAll(colours::panel);
  g.setColour(colours::line);
  g.drawRect(bounds, 1.0f);
  layoutTooltip(text, colours::textSoft).draw(g, bounds.reduced(10.0f, 6.0f));
}

juce::Rectangle<int>
EvilLookAndFeel::getTooltipBounds(const juce::String &text,
                                  juce::Point<int> screenPos,
                                  juce::Rectangle<int> parentArea) {
  const auto layout = layoutTooltip(text, colours::textSoft);
  const int w = (int)std::ceil(layout.getWidth()) + 20;
  const int h = (int)std::ceil(layout.getHeight()) + 12;
  return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX()
                                  ? screenPos.x - (w + 12)
                                  : screenPos.x + 24,
                              screenPos.y > parentArea.getCentreY()
                                  ? screenPos.y - (h + 6)
                                  : screenPos.y + 6,
                              w, h)
      .constrainedWithin(parentArea);
}

juce::Font EvilLookAndFeel::getTextButtonFont(juce::TextButton &, int) {
  return mono(12.0f);
}

juce::Font EvilLookAndFeel::getComboBoxFont(juce::ComboBox &) {
  return mono(12.5f);
}

juce::Font EvilLookAndFeel::getPopupMenuFont() { return mono(12.5f); }

juce::Font EvilLookAndFeel::getLabelFont(juce::Label &label) {
  return label.getFont();
}

juce::Font EvilLookAndFeel::getAlertWindowTitleFont() {
  return sans(17.0f, true, -0.01f);
}

juce::Font EvilLookAndFeel::getAlertWindowMessageFont() { return sans(13.5f); }

juce::Font EvilLookAndFeel::getAlertWindowFont() { return mono(12.0f); }

juce::Label *EvilLookAndFeel::createSliderTextBox(juce::Slider &slider) {
  auto *label = LookAndFeel_V4::createSliderTextBox(slider);
  label->setFont(mono(12.5f));
  label->setColour(juce::Label::textColourId, colours::textSoft);
  label->setColour(juce::Label::outlineColourId,
                   juce::Colours::transparentBlack);
  label->setColour(juce::Label::backgroundColourId,
                   juce::Colours::transparentBlack);
  label->setColour(juce::Label::outlineWhenEditingColourId, colours::accent);
  return label;
}
} // namespace evil::gui
