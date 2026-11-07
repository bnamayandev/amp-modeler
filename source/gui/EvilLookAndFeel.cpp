#include "EvilLookAndFeel.h"

#include <EvilBinaryData.h>

namespace evil::gui {
namespace colours {
namespace {
float accentHue = defaultAccentHue;

/** Blues read much darker than reds at the same HSL lightness, and would sink
    into the black panel: lift them to match. */
float lightnessAt(float hue) {
  const float degrees = hue * 360.0f;
  const float fromBlue = juce::jmin(std::abs(degrees - 245.0f), 360.0f - std::abs(degrees - 245.0f));
  const float lift = juce::jmax(0.0f, 1.0f - fromBlue / 65.0f);
  return 0.56f + 0.13f * lift * lift * (3.0f - 2.0f * lift);
}
} // namespace

juce::Colour accentAt(float hue) {
  return juce::Colour::fromHSL(hue, 0.92f, lightnessAt(hue), 1.0f);
}
juce::Colour accent() { return accentAt(accentHue); }
juce::Colour accentHover() {
  return juce::Colour::fromHSL(accentHue, 0.95f, lightnessAt(accentHue) + 0.1f, 1.0f);
}
float getAccentHue() { return accentHue; }
void setAccentHue(float hue) { accentHue = hue - std::floor(hue); }
} // namespace colours

namespace {
const juce::Colour black = juce::Colours::black, white = juce::Colours::white;
constexpr float keyCorner = 3.0f;
constexpr int capWidth = 32, capHeight = 17, capMargin = 4;

juce::Typeface::Ptr load(const char *data, int size) {
  return juce::Typeface::createSystemTypefaceFor(data, (size_t)size);
}

juce::Font makeFont(const juce::Typeface::Ptr &face, float size,
                    float tracking) {
  return juce::Font(juce::FontOptions(face).withPointHeight(size).withKerningFactor(
      tracking));
}

/** Accent at a given strength over transparent, like CSS color-mix(). */
juce::Colour tint(float amount) { return colours::accent().withAlpha(amount); }

void drawGlow(juce::Graphics &g, juce::Point<float> centre, float radius,
              float strength = 0.55f) {
  juce::ColourGradient glow(tint(strength), centre, tint(0.0f),
                            centre.translated(radius, 0.0f), true);
  g.setGradientFill(glow);
  g.fillEllipse(circle(centre, radius));
}

bool isPrimary(const juce::Button &button) {
  return button.getProperties()["primary"];
}

bool isTab(const juce::Button &button) {
  return button.getProperties()["tab"];
}

bool isProse(const juce::Button &button) {
  return button.getProperties()["prose"];
}

bool isBipolar(const juce::Slider &slider) {
  return slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 &&
         std::abs(slider.getMinimum() + slider.getMaximum()) < 1.0e-6;
}

juce::TextLayout layoutTooltip(const juce::String &text, juce::Colour colour) {
  juce::AttributedString s;
  s.setJustification(juce::Justification::centredLeft);
  s.append(text, sans(13.5f), colour);
  juce::TextLayout layout;
  layout.createLayoutWithBalancedLineLengths(s, 340.0f);
  return layout;
}

/** A fader's cap: a ridged metal block with its level line across the middle. */
void paintFaderCap(juce::Graphics &g, juce::Rectangle<float> cap) {
  g.setColour(black.withAlpha(0.55f));
  g.fillRoundedRectangle(cap.translated(0.0f, 2.0f).expanded(1.0f, 1.0f), 3.0f);

  juce::ColourGradient metal(juce::Colour(0xff77777d), 0.0f, cap.getY(),
                             juce::Colour(0xff222225), 0.0f, cap.getBottom(), false);
  metal.addColour(0.46, juce::Colour(0xff3c3c41));
  metal.addColour(0.54, juce::Colour(0xff1d1d20));
  g.setGradientFill(metal);
  g.fillRoundedRectangle(cap, 2.0f);
  g.setColour(black.withAlpha(0.8f));
  g.drawRoundedRectangle(cap, 2.0f, 0.8f);
  g.setColour(white.withAlpha(0.24f));
  g.fillRect(cap.reduced(1.5f, 0.0f).withY(cap.getY() + 0.8f).withHeight(0.8f));

  g.setColour(black.withAlpha(0.32f));
  for (const float offset : {-4.0f, -2.5f, 2.5f, 4.0f})
    g.fillRect(cap.reduced(2.0f, 0.0f).withHeight(0.7f).withY(cap.getCentreY() + offset));
}

/** A vertical fader: a slot cut in the panel, the value lit in the accent from
    its centre (or bottom), and a metal cap riding in it. */
void drawFader(juce::Graphics &g, juce::Rectangle<float> bounds, float sliderPos,
               juce::Slider &slider, PaintCache &capCache) {
  using namespace colours;
  const bool enabled = slider.isEnabled();
  const bool hot = enabled && slider.isMouseOverOrDragging();
  const auto x = bounds.getCentreX();
  const auto top = (float)slider.getPositionOfValue(slider.getMaximum());
  const auto bottom = (float)slider.getPositionOfValue(slider.getMinimum());

  const auto slot = juce::Rectangle<float>::leftTopRightBottom(x - 1.5f, top - 3.0f,
                                                               x + 1.5f, bottom + 3.0f);
  g.setColour(white.withAlpha(0.09f));
  g.fillRoundedRectangle(slot, 1.5f);

  const auto from = isBipolar(slider) ? (top + bottom) * 0.5f : bottom;
  const auto lit = juce::Rectangle<float>::leftTopRightBottom(
      x - 1.0f, juce::jmin(from, sliderPos), x + 1.0f, juce::jmax(from, sliderPos));
  if (!lit.isEmpty()) {
    g.setColour(tint(0.3f));
    g.fillRoundedRectangle(lit.expanded(1.5f, 0.5f), 2.0f);
    g.setColour(!enabled ? mutedSoft : hot ? accentHover() : accent());
    g.fillRoundedRectangle(lit, 1.0f);
  }

  capCache.draw(g, {x - capWidth * 0.5f - capMargin, sliderPos - capHeight * 0.5f - capMargin},
                capWidth + 2 * capMargin, capHeight + 2 * capMargin, 0, [](juce::Graphics &c) {
                  paintFaderCap(c, juce::Rectangle<float>((float)capMargin, (float)capMargin,
                                                          (float)capWidth, (float)capHeight));
                });
  // The level line, lit like a knob's pointer.
  g.setColour(!enabled ? muted : hot ? white : text);
  g.fillRect(juce::Rectangle<float>((float)capWidth - 6.0f, 1.8f)
                 .withCentre({x, sliderPos}));
}

/** A key: raised, lit from above, pressed in while held. */
void drawKey(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour top,
             juce::Colour bottom, bool down, juce::Colour edge) {
  g.setColour(black.withAlpha(0.6f)); // its shadow on the panel
  g.fillRoundedRectangle(b.translated(0.0f, down ? 0.5f : 1.2f), keyCorner);
  g.setGradientFill(juce::ColourGradient(down ? bottom : top, 0.0f, b.getY(),
                                         down ? top : bottom, 0.0f, b.getBottom(), false));
  g.fillRoundedRectangle(b, keyCorner);
  g.setColour(edge);
  g.drawRoundedRectangle(b.reduced(0.5f), keyCorner, 1.0f);
  if (!down) {
    g.setColour(white.withAlpha(0.13f));
    g.fillRect(b.reduced(keyCorner, 0.0f).withY(b.getY() + 1.0f).withHeight(1.0f));
  }
}

/** The symbols on small keys, as strokes rather than glyphs so they stay crisp
    and centred. False if this text isn't one of them. */
bool drawSymbol(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> b) {
  const auto c = b.getCentre();
  const float s = 4.0f;
  juce::Path path;
  if (text == "<") {
    path.startNewSubPath(c.x + s * 0.5f, c.y - s);
    path.lineTo(c.x - s * 0.5f, c.y);
    path.lineTo(c.x + s * 0.5f, c.y + s);
  } else if (text == ">") {
    path.startNewSubPath(c.x - s * 0.5f, c.y - s);
    path.lineTo(c.x + s * 0.5f, c.y);
    path.lineTo(c.x - s * 0.5f, c.y + s);
  } else if (text == "-") {
    path.startNewSubPath(c.x - s, c.y);
    path.lineTo(c.x + s, c.y);
  } else if (text == "+") {
    path.startNewSubPath(c.x - s, c.y);
    path.lineTo(c.x + s, c.y);
    path.startNewSubPath(c.x, c.y - s);
    path.lineTo(c.x, c.y + s);
  } else if (text == juce::String::charToString(0x00d7)) {
    path.startNewSubPath(c.x - s * 0.8f, c.y - s * 0.8f);
    path.lineTo(c.x + s * 0.8f, c.y + s * 0.8f);
    path.startNewSubPath(c.x + s * 0.8f, c.y - s * 0.8f);
    path.lineTo(c.x - s * 0.8f, c.y + s * 0.8f);
  } else {
    return false;
  }
  g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
  return true;
}
} // namespace

Typefaces::Typefaces()
    : sans(load(EvilBinaryData::BarlowRegular_ttf,
                EvilBinaryData::BarlowRegular_ttfSize)),
      sansMedium(load(EvilBinaryData::BarlowMedium_ttf,
                      EvilBinaryData::BarlowMedium_ttfSize)),
      silk(load(EvilBinaryData::BarlowSemiCondensedSemiBold_ttf,
                EvilBinaryData::BarlowSemiCondensedSemiBold_ttfSize)),
      display(load(EvilBinaryData::BarlowCondensedExtraBoldItalic_ttf,
                   EvilBinaryData::BarlowCondensedExtraBoldItalic_ttfSize)),
      readout(load(EvilBinaryData::ShareTechMonoRegular_ttf,
                   EvilBinaryData::ShareTechMonoRegular_ttfSize)),
      logo(load(EvilBinaryData::NewRockerRegular_ttf,
                EvilBinaryData::NewRockerRegular_ttfSize)) {}

juce::Font sans(float size, bool medium, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(medium ? faces->sansMedium : faces->sans, size, tracking);
}

juce::Font silk(float size, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(faces->silk, size, tracking);
}

juce::Font display(float size, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(faces->display, size, tracking);
}

juce::Font readout(float size, float tracking) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(faces->readout, size, tracking);
}

juce::Font logoFont(float size) {
  const juce::SharedResourcePointer<Typefaces> faces;
  return makeFont(faces->logo, size, 0.0f);
}

void setPrimary(juce::Button &button) {
  button.getProperties().set("primary", true);
}

void setTab(juce::Button &button) {
  button.getProperties().set("tab", true);
}

void setProse(juce::Button &button) {
  button.getProperties().set("prose", true);
}

void setCompact(juce::Slider &slider) {
  slider.getProperties().set("compact", true);
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
  setColour(juce::Slider::rotarySliderOutlineColourId, white.withAlpha(0.2f));
  setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
  setColour(juce::TextButton::textColourOffId, textSoft);
  setColour(juce::TextButton::textColourOnId, text);
  setColour(juce::ComboBox::backgroundColourId,
            juce::Colours::transparentBlack);
  setColour(juce::ComboBox::outlineColourId, line);
  setColour(juce::ComboBox::textColourId, text);
  setColour(juce::ComboBox::arrowColourId, muted);
  setColour(juce::PopupMenu::backgroundColourId, panel);
  setColour(juce::PopupMenu::textColourId, textSoft);
  setColour(juce::PopupMenu::headerTextColourId, muted);
  setColour(juce::PopupMenu::highlightedTextColourId, text);
  setColour(juce::ToggleButton::textColourId, textSoft);
  setColour(juce::TextEditor::backgroundColourId, glass);
  setColour(juce::TextEditor::textColourId, text);
  setColour(juce::TextEditor::highlightedTextColourId, text);
  setColour(juce::TextEditor::outlineColourId, line);
  setColour(juce::AlertWindow::backgroundColourId, panel);
  setColour(juce::AlertWindow::textColourId, textSoft);
  setColour(juce::AlertWindow::outlineColourId, line);
  setColour(juce::TooltipWindow::backgroundColourId, panel);
  setColour(juce::TooltipWindow::textColourId, textSoft);
  setColour(juce::TooltipWindow::outlineColourId, line);
  refreshAccent();
}

void EvilLookAndFeel::refreshAccent() {
  setColour(juce::Slider::textBoxHighlightColourId, tint(0.32f));
  setColour(juce::TextButton::buttonOnColourId, tint(0.18f));
  setColour(juce::PopupMenu::highlightedBackgroundColourId, tint(0.28f));
  setColour(juce::TextEditor::highlightColourId, tint(0.32f));
  setColour(juce::TextEditor::focusedOutlineColourId, colours::accent());
  setColour(juce::CaretComponent::caretColourId, colours::accent());
}

void EvilLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                       int width, int height, float sliderPos,
                                       float startAngle, float endAngle,
                                       juce::Slider &slider) {
  using namespace colours;
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
  const auto centre = bounds.getCentre();
  const auto angle = startAngle + sliderPos * (endAngle - startAngle);
  const bool enabled = slider.isEnabled();
  const bool hot = enabled && slider.isMouseOverOrDragging();
  const bool small = radius < 30.0f;

  // A ring of lights: every mark up to the value is lit, from the centre for
  // bipolar parameters (e.g. EQ, +/- dB).
  const float outer = radius - 0.5f, inner = outer - (small ? 3.0f : 5.5f);
  const int marks = small ? 15 : 27;
  const auto from = isBipolar(slider) ? (startAngle + endAngle) * 0.5f : startAngle;
  constexpr float nudge = 0.002f;
  const auto lower = juce::jmin(from, angle) - nudge, upper = juce::jmax(from, angle) + nudge;
  juce::Path unlit, lit;
  for (int i = 0; i < marks; ++i) {
    const float a = startAngle + (endAngle - startAngle) * (float)i / (float)(marks - 1);
    const bool on = a >= lower && a <= upper;
    auto &path = on ? lit : unlit;
    path.startNewSubPath(centre.getPointOnCircumference(inner, a));
    path.lineTo(centre.getPointOnCircumference(outer, a));
  }
  const float markWidth = small ? 1.3f : 1.7f;
  const juce::PathStrokeType stroke(markWidth, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);
  g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
  g.strokePath(unlit, stroke);
  const auto valueColour = !enabled ? mutedSoft : hot ? accentHover() : accent();
  if (enabled) {
    g.setColour(valueColour.withAlpha(0.24f));
    g.strokePath(lit, juce::PathStrokeType(markWidth + (small ? 2.0f : 3.2f),
                                           juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
  }
  g.setColour(valueColour);
  g.strokePath(lit, stroke);

  // The knob itself. Only its pointer moves, so the rest is painted once per size.
  const float body = inner - (small ? 1.5f : 3.0f);
  const int size = (int)std::ceil(body * 2.8f);
  knobBodies[juce::roundToInt(body * 10.0f)].draw(
      g, centre.translated(-(float)size * 0.5f, -(float)size * 0.5f), size, size, 0,
      [size, body](juce::Graphics &c) {
        paintKnobBody(c, {(float)size * 0.5f, (float)size * 0.5f}, body);
      });
  if (hot) {
    g.setColour(white.withAlpha(0.05f));
    g.fillEllipse(circle(centre, body));
  }

  const juce::Line<float> pointer(centre.getPointOnCircumference(body * 0.36f, angle),
                                  centre.getPointOnCircumference(body * 0.97f, angle));
  const float pointerWidth = juce::jmax(1.6f, body * 0.075f);
  g.setColour(black.withAlpha(0.65f));
  g.drawLine(pointer, pointerWidth + 1.4f);
  g.setColour(enabled ? text : muted);
  g.drawLine(pointer, pointerWidth);
}

void EvilLookAndFeel::drawLinearSlider(juce::Graphics &g, int x, int y,
                                       int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style,
                                       juce::Slider &slider) {
  if (style == juce::Slider::LinearVertical) {
    drawFader(g, juce::Rectangle<int>(x, y, width, height).toFloat(), sliderPos,
              slider, faderCap);
    return;
  }
  if (style != juce::Slider::LinearBar) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);
    return;
  }
  // A readout you drag: a recessed window, the value a lit bar from the left.
  using namespace colours;
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const bool hot = slider.isEnabled() && slider.isMouseOverOrDragging();
  drawRecess(g, bounds, keyCorner);
  juce::Path outline;
  outline.addRoundedRectangle(bounds.reduced(1.0f), keyCorner - 1.0f);
  const juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(outline);
  g.setColour(tint(hot ? 0.4f : 0.3f));
  g.fillRect(bounds.withRight(juce::jlimit(bounds.getX(), bounds.getRight(), sliderPos)));
}

void EvilLookAndFeel::drawToggleButton(juce::Graphics &g,
                                       juce::ToggleButton &button,
                                       bool highlighted, bool) {
  using namespace colours;
  auto bounds = button.getLocalBounds().toFloat();
  const bool on = button.getToggleState();
  const auto rocker = bounds.removeFromLeft(32.0f).withSizeKeepingCentre(32.0f, 18.0f);
  bounds.removeFromLeft(9.0f);

  // An illuminated rocker switch, in the hole it's mounted in.
  if (on)
    drawGlow(g, rocker.getCentre(), 26.0f, 0.3f);
  g.setColour(white.withAlpha(0.1f));
  g.fillRoundedRectangle(rocker.translated(0.0f, 0.8f), 2.5f);
  g.setColour(black);
  g.fillRoundedRectangle(rocker, 2.5f);

  const auto face = rocker.reduced(1.6f);
  const auto pressed = face.withWidth(face.getWidth() * 0.5f);
  const auto proud = face.withTrimmedLeft(face.getWidth() * 0.5f);
  if (on) { // lit from behind, brightest at the proud end
    const auto hotColour = highlighted ? accentHover() : accent();
    g.setGradientFill(juce::ColourGradient(hotColour.darker(0.55f), face.getX(), 0.0f,
                                           hotColour.interpolatedWith(white, 0.18f),
                                           face.getRight(), 0.0f, false));
    g.fillRoundedRectangle(face, 1.5f);
    g.setColour(white.withAlpha(0.32f));
    g.fillRect(proud.reduced(1.0f, 0.0f).withHeight(1.0f).withY(proud.getY() + 0.8f));
  } else { // dark plastic, tipped the other way
    const auto base = juce::Colour(highlighted ? 0xff3e3e43 : 0xff2e2e32);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.2f), face.getX(), 0.0f,
                                           base.darker(0.8f), face.getRight(), 0.0f, false));
    g.fillRoundedRectangle(face, 1.5f);
    g.setColour(white.withAlpha(0.14f));
    g.fillRect(pressed.reduced(1.0f, 0.0f).withHeight(1.0f).withY(pressed.getY() + 0.8f));
  }
  g.setColour(black.withAlpha(0.42f));
  g.fillRect(face.withWidth(1.0f).withX(face.getCentreX() - 0.5f));

  g.setColour(!button.isEnabled() ? mutedSoft
              : on || highlighted ? text
                                  : textSoft);
  g.setFont(silk(12.0f, 0.08f));
  g.drawFittedText(button.getButtonText().toUpperCase(), bounds.toNearestInt(),
                   juce::Justification::centredLeft, 1);
}

void EvilLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                           juce::Button &button,
                                           const juce::Colour &, bool highlighted,
                                           bool down) {
  using namespace colours;
  const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 1.0f);
  const auto edge = black.withAlpha(0.9f);
  if (isTab(button)) {
    // A key in the tray; the selected one is lit, with a bar of light along its
    // bottom edge.
    if (button.getToggleState()) {
      drawKey(g, bounds, juce::Colour(0xff414146), juce::Colour(0xff26262a), false, edge);
      const auto bar = bounds.reduced(6.0f, 0.0f)
                           .withHeight(2.0f)
                           .withY(bounds.getBottom() - 3.5f);
      g.setColour(tint(0.42f));
      g.fillRoundedRectangle(bar.expanded(1.5f, 1.5f), 2.0f);
      g.setColour(accent());
      g.fillRoundedRectangle(bar, 1.0f);
    } else if (highlighted) {
      g.setColour(white.withAlpha(0.06f));
      g.fillRoundedRectangle(bounds, keyCorner);
    }
    return;
  }
  if (button.getToggleState()) { // selected: the key lit from inside
    const auto top = accent().interpolatedWith(black, down ? 0.55f : highlighted ? 0.35f : 0.45f);
    drawKey(g, bounds, top, top.interpolatedWith(black, 0.5f), down, edge);
  } else if (isPrimary(button) && button.isEnabled()) {
    const auto top = juce::Colour(0xff37373c).interpolatedWith(accent(), highlighted ? 0.42f : 0.28f);
    drawKey(g, bounds, top, top.interpolatedWith(black, 0.45f), down, edge);
  } else {
    const auto top = juce::Colour(highlighted ? 0xff47474d : 0xff37373c);
    const auto bottom = juce::Colour(highlighted ? 0xff29292d : 0xff1f1f23);
    drawKey(g, bounds, top, bottom, down, edge);
  }
}

void EvilLookAndFeel::drawButtonText(juce::Graphics &g,
                                     juce::TextButton &button, bool highlighted,
                                     bool down) {
  using namespace colours;
  const auto offset = down ? 1 : 0;
  if (isTab(button)) {
    const bool selected = button.getToggleState();
    g.setColour(selected ? text : highlighted ? textSoft : muted);
    g.setFont(silk(14.0f, 0.11f));
    g.drawFittedText(button.getButtonText().toUpperCase(),
                     button.getLocalBounds().withTrimmedBottom(selected ? 3 : 0),
                     juce::Justification::centred, 1);
    return;
  }
  const bool primary = isPrimary(button) && !button.getToggleState();
  const bool enabled = button.isEnabled();
  g.setColour(!enabled ? mutedSoft
              : primary || button.getToggleState() || highlighted ? text
                                                                  : textSoft);
  const auto area = button.getLocalBounds().toFloat().translated(0.0f, (float)offset);
  if (drawSymbol(g, button.getButtonText(), area))
    return;
  if (isProse(button)) {
    g.setFont(sans(14.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(10, 2),
                     juce::Justification::centred, 1);
    return;
  }
  g.setFont(getTextButtonFont(button, button.getHeight()));
  g.drawFittedText(button.getButtonText().toUpperCase(),
                   button.getLocalBounds().reduced(5, 2).translated(0, offset),
                   juce::Justification::centred, 1);
}

void EvilLookAndFeel::drawComboBox(juce::Graphics &g, int width, int height,
                                   bool, int, int, int, int,
                                   juce::ComboBox &box) {
  using namespace colours;
  const auto bounds = juce::Rectangle<int>(width, height).toFloat().reduced(0.0f, 0.5f);
  const bool hot = box.isEnabled() && box.isMouseOver(true);
  drawRecess(g, bounds, keyCorner);
  if (hot) {
    g.setColour(white.withAlpha(0.03f));
    g.fillRoundedRectangle(bounds.reduced(1.0f), keyCorner);
  }
  if (box.hasKeyboardFocus(true)) {
    g.setColour(accent());
    g.drawRoundedRectangle(bounds.reduced(0.5f), keyCorner, 1.0f);
  }

  const auto arrowZone =
      juce::Rectangle<float>((float)width - 21.0f, 0.0f, 14.0f, (float)height);
  juce::Path arrow;
  arrow.startNewSubPath(arrowZone.getCentreX() - 3.5f,
                        arrowZone.getCentreY() - 1.75f);
  arrow.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 1.75f);
  arrow.lineTo(arrowZone.getCentreX() + 3.5f, arrowZone.getCentreY() - 1.75f);
  g.setColour(hot ? accent() : muted);
  g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void EvilLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width,
                                              int height) {
  g.fillAll(colours::panel.darker(0.25f));
  g.setColour(black);
  g.drawRect(0, 0, width, height, 1);
}

void EvilLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox &,
                                               juce::Graphics &g,
                                               const juce::Path &path,
                                               juce::Image &) {
  juce::DropShadow(black.withAlpha(0.7f), 18, {0, 5}).drawForPath(g, path);
  g.setColour(colours::panel);
  g.fillPath(path);
  g.setColour(black);
  g.strokePath(path, juce::PathStrokeType(1.0f));
}

void EvilLookAndFeel::drawCornerResizer(juce::Graphics &g, int w, int h,
                                        bool isMouseOver,
                                        bool isMouseDragging) {
  g.setColour(isMouseDragging ? colours::accent()
              : isMouseOver   ? colours::muted
                              : colours::mutedSoft);
  for (float i = 0.3f; i < 1.0f; i += 0.3f)
    g.drawLine((float)w * i, (float)h, (float)w, (float)h * i, 1.0f);
}

void EvilLookAndFeel::drawTooltip(juce::Graphics &g, const juce::String &text,
                                  int width, int height) {
  const auto bounds = juce::Rectangle<int>(width, height).toFloat();
  g.fillAll(colours::panel.darker(0.25f));
  g.setColour(black);
  g.drawRect(bounds, 1.0f);
  layoutTooltip(text, colours::text).draw(g, bounds.reduced(10.0f, 6.0f));
}

juce::Rectangle<int>
EvilLookAndFeel::getTooltipBounds(const juce::String &text,
                                  juce::Point<int> screenPos,
                                  juce::Rectangle<int> parentArea) {
  const auto layout = layoutTooltip(text, colours::text);
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
  return silk(12.5f, 0.08f);
}

juce::Font EvilLookAndFeel::getComboBoxFont(juce::ComboBox &) {
  return readout(14.0f);
}

juce::Font EvilLookAndFeel::getPopupMenuFont() { return sans(14.5f, true); }

juce::Font EvilLookAndFeel::getLabelFont(juce::Label &label) {
  return label.getFont();
}

juce::Font EvilLookAndFeel::getAlertWindowTitleFont() {
  return display(22.0f);
}

juce::Font EvilLookAndFeel::getAlertWindowMessageFont() { return sans(14.5f); }

juce::Font EvilLookAndFeel::getAlertWindowFont() { return silk(12.5f, 0.08f); }

juce::Label *EvilLookAndFeel::createSliderTextBox(juce::Slider &slider) {
  auto *label = LookAndFeel_V4::createSliderTextBox(slider);
  const bool compact = slider.getProperties()["compact"];
  label->setFont(readout(compact ? 11.0f : 14.0f));
  label->setColour(juce::Label::textColourId,
                   slider.findColour(juce::Slider::textBoxTextColourId));
  label->setColour(juce::Label::outlineColourId,
                   juce::Colours::transparentBlack);
  label->setColour(juce::Label::backgroundColourId,
                   juce::Colours::transparentBlack);
  // Typing happens in place: the same text in the same spot, on a tint, or on
  // the bar a bar slider already draws. JUCE's default fill is the transparent
  // colour made opaque.
  label->setColour(juce::Label::textWhenEditingColourId, colours::text);
  label->setColour(juce::Label::backgroundWhenEditingColourId,
                   slider.isBar() ? juce::Colours::transparentBlack
                                  : colours::glass.withAlpha(0.92f));
  label->setColour(juce::Label::outlineWhenEditingColourId,
                   juce::Colours::transparentBlack);
  label->onEditorShow = [label, compact] {
    auto *editor = label->getCurrentTextEditor();
    if (editor == nullptr)
      return;
    // JUCE fills the editor while it's 10 pixels wide, which leaves the text
    // scrolled to the left. Undo that first: once centred, the caret at the start
    // is already in view and wouldn't scroll back. No top indent, as it would push
    // the text below the middle of a box this short.
    // A pedal's box is barely wider than its text, so no side indent there either.
    editor->setCaretPosition(0);
    editor->setIndents(compact ? 0 : editor->getLeftIndent(), 0);
    editor->setJustification(label->getJustificationType());
    editor->selectAll();
  };
  return label;
}
} // namespace evil::gui
