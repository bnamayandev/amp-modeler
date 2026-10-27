#include "Settings.h"

#include "EvilLookAndFeel.h"

namespace evil::gui {
namespace {
constexpr auto accentHueKey = "accentHue";

juce::PropertiesFile::Options fileOptions() {
  juce::PropertiesFile::Options options;
  options.storageFormat = juce::PropertiesFile::storeAsXML;
  options.millisecondsBeforeSaving = 500; // a hue drag saves once, at the end
  return options;
}
} // namespace

Settings::Settings() : file(getFile(), fileOptions()) {
  colours::setAccentHue((float)file.getDoubleValue(
      accentHueKey, (double)colours::defaultAccentHue));
}

juce::File Settings::getFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("settings.xml");
}

void Settings::setAccentHue(float hue) {
  colours::setAccentHue(hue);
  getFile().getParentDirectory().createDirectory();
  file.setValue(accentHueKey, (double)colours::getAccentHue());
  sendChangeMessage();
}

SettingsPanel::SettingsPanel(Settings &s) : settings(s) {
  settings.addChangeListener(this);
  setSize(panelWidth, panelHeight);
}

SettingsPanel::~SettingsPanel() { settings.removeChangeListener(this); }

void SettingsPanel::resized() {
  auto b = getLocalBounds().reduced(14, 12);
  caption = b.removeFromTop(16);
  b.removeFromTop(14);
  swatchRow = b.removeFromTop(24);
  b.removeFromTop(18);
  hueStrip = b.removeFromTop(10);
}

juce::Rectangle<float> SettingsPanel::swatch(size_t index) const {
  const float step = (float)swatchRow.getWidth() / (float)swatchHues.size();
  return juce::Rectangle<float>(20.0f, 20.0f).withCentre(
      {(float)swatchRow.getX() + step * ((float)index + 0.5f),
       (float)swatchRow.getCentreY()});
}

void SettingsPanel::paint(juce::Graphics &g) {
  using namespace colours;
  const float hue = getAccentHue();

  g.setFont(gui::caption());
  g.setColour(muted);
  g.drawText("ACCENT", caption, juce::Justification::centredLeft);
  g.setFont(mono(11.5f));
  g.setColour(textSoft);
  g.drawText(juce::String(juce::roundToInt(hue * 360.0f) % 360) +
                 juce::String::charToString(0x00b0),
             caption, juce::Justification::centredRight);

  for (size_t i = 0; i < swatchHues.size(); ++i) {
    const auto r = swatch(i);
    const float distance = std::abs(swatchHues[i] - hue * 360.0f);
    if (juce::jmin(distance, 360.0f - distance) < 0.5f) {
      g.setColour(text);
      g.drawEllipse(r.expanded(3.5f), 1.25f);
    }
    g.setColour(accentAt(swatchHues[i] / 360.0f));
    g.fillEllipse(r);
  }

  // The whole wheel at the accent's saturation and lightness.
  const auto strip = hueStrip.toFloat();
  juce::ColourGradient wheel(accentAt(0.0f), strip.getX(), 0.0f,
                             accentAt(0.0f), strip.getRight(), 0.0f, false);
  for (int i = 1; i < 12; ++i)
    wheel.addColour(i / 12.0, accentAt((float)i / 12.0f));
  g.setGradientFill(wheel);
  g.fillRoundedRectangle(strip, strip.getHeight() * 0.5f);

  const auto marker =
      juce::Rectangle<float>(6.0f, strip.getHeight() + 8.0f)
          .withCentre({strip.getX() + hue * strip.getWidth(), strip.getCentreY()});
  g.setColour(accent());
  g.fillRoundedRectangle(marker, 3.0f);
  g.setColour(text);
  g.drawRoundedRectangle(marker, 3.0f, 1.25f);
}

void SettingsPanel::mouseDown(const juce::MouseEvent &e) {
  draggingHue = hueStrip.expanded(4, 8).contains(e.getPosition());
  if (draggingHue) {
    mouseDrag(e);
    return;
  }
  for (size_t i = 0; i < swatchHues.size(); ++i)
    if (swatch(i).expanded(3.0f).contains(e.position)) {
      settings.setAccentHue(swatchHues[i] / 360.0f);
      return;
    }
}

void SettingsPanel::mouseDrag(const juce::MouseEvent &e) {
  if (!draggingHue)
    return;
  const auto strip = hueStrip.toFloat();
  // Stop just short of 1, which would wrap round to the strip's left end.
  settings.setAccentHue(juce::jlimit(
      0.0f, 0.999f, (e.position.x - strip.getX()) / strip.getWidth()));
}

void SettingsPanel::mouseMove(const juce::MouseEvent &e) {
  bool hot = hueStrip.expanded(4, 8).contains(e.getPosition());
  for (size_t i = 0; i < swatchHues.size() && !hot; ++i)
    hot = swatch(i).expanded(3.0f).contains(e.position);
  setMouseCursor(hot ? juce::MouseCursor::PointingHandCursor
                     : juce::MouseCursor::NormalCursor);
}
} // namespace evil::gui
