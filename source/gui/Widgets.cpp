#include "Widgets.h"

#include "EvilLookAndFeel.h"

namespace evil::gui {
void ignoreTextWithoutNumber(juce::Slider &slider) {
  slider.valueFromTextFunction = [&slider, parse = slider.valueFromTextFunction](
                                     const juce::String &text) {
    return text.containsAnyOf("0123456789") ? parse(text) : slider.getValue();
  };
}

Knob::Knob(juce::AudioProcessorValueTreeState &state,
           const juce::String &parameterId, const juce::String &titleText,
           Size sizeToUse)
    : slider(juce::Slider::RotaryHorizontalVerticalDrag,
             juce::Slider::TextBoxBelow),
      attachment(state, parameterId, slider), size(sizeToUse) {
  title.setText(titleText.toUpperCase(), juce::dontSendNotification);
  title.setFont(size == Size::pedal ? silk(10.0f, 0.06f) : silk(12.0f, 0.11f));
  title.setColour(juce::Label::textColourId, colours::text);
  title.setJustificationType(juce::Justification::centred);
  title.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(title);

  if (size == Size::pedal)
    setCompact(slider);
  slider.setRotaryParameters(juce::degreesToRadians(225.0f),
                             juce::degreesToRadians(495.0f), true);
  slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80,
                         size == Size::pedal ? 14 : 18);
  slider.setColour(juce::Slider::textBoxOutlineColourId,
                   juce::Colours::transparentBlack);
  slider.setPopupDisplayEnabled(false, false, nullptr);
  if (auto *p = state.getParameter(parameterId))
    slider.setDoubleClickReturnValue(true,
                                     p->convertFrom0to1(p->getDefaultValue()));
  slider.setTooltip(titleText + " (double-click to reset)");
  ignoreTextWithoutNumber(slider);
  addAndMakeVisible(slider);
}

void Knob::setInk(juce::Colour ink) {
  title.setColour(juce::Label::textColourId, ink);
  slider.setColour(juce::Slider::textBoxTextColourId, ink);
  slider.setColour(juce::Slider::rotarySliderOutlineColourId, ink.withAlpha(0.32f));
}

void Knob::resized() {
  const bool pedal = size == Size::pedal;
  auto b = getLocalBounds();
  title.setBounds(b.removeFromTop(pedal ? 13 : 16));
  // Keep knobs a consistent size however big their panel is.
  const int textHeight = pedal ? 14 : 20;
  const int diameter =
      juce::jmin(b.getWidth(), b.getHeight() - textHeight, maxDiameter);
  // No wider than this component, which would cut off the sides of the value box.
  slider.setBounds(b.withSizeKeepingCentre(
      juce::jmin(b.getWidth(), juce::jmax(diameter, pedal ? 46 : 84)),
      diameter + textHeight));
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
  title.setFont(silk(11.5f, 0.07f));
  title.setColour(juce::Label::textColourId, colours::text);
  value.setFont(readout(12.0f));

  // Compact, to fit a narrow column: "+3.5", "0.0", "-10.0".
  slider.onValueChange = [this] {
    const auto v = slider.getValue();
    const bool flat = std::abs(v) < 0.05;
    value.setText((v >= 0.05 ? "+" : "") + juce::String(flat ? 0.0 : v, 1),
                  juce::dontSendNotification);
    value.setColour(juce::Label::textColourId,
                    flat ? colours::muted : colours::accent());
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
  constexpr float minDb = -60.0f, maxDb = 6.0f, hotDb = -6.0f, warmDb = -18.0f;
  constexpr float pitch = 4.0f, segment = 3.0f; // one LED every 4 px
  auto bounds = getLocalBounds().toFloat();
  auto clipLed = bounds.removeFromTop(6.0f);
  bounds.removeFromTop(4.0f);

  drawLed(g, clipLed.withSizeKeepingCentre(6.0f, 6.0f), clipped, colours::accent());

  drawRecess(g, bounds, 2.0f);
  const auto ladder = bounds.reduced(2.0f);
  const int count = (int)(ladder.getHeight() / pitch);
  for (int i = 0; i < count; ++i) {
    const float top = ladder.getBottom() - pitch * (float)(i + 1) + (pitch - segment);
    const float db = juce::jmap((float)i + 0.5f, 0.0f, (float)count, minDb, maxDb);
    const auto led = juce::Rectangle<float>(ladder.getX(), top, ladder.getWidth(), segment);
    const auto colour = db >= hotDb    ? colours::accent()
                        : db >= warmDb ? colours::text
                                       : colours::textSoft.interpolatedWith(juce::Colours::black, 0.18f);
    g.setColour(db <= levelDb ? colour : colour.withAlpha(0.08f));
    g.fillRect(led);
  }
}

Spectrum::Spectrum(double low, double high) : lowHz(low), highHz(high) {
  levelsDb.fill(floorDb);
  setInterceptsMouseClicks(false, false);
}

void Spectrum::push(const float *data, int n) noexcept {
  for (int i = 0; i < n; ++i) {
    history[(size_t)historyWrite] = std::isfinite(data[i]) ? data[i] : 0.0f;
    historyWrite = (historyWrite + 1) & (fftSize - 1);
  }
  fresh = fresh || n > 0;
}

void Spectrum::refresh(double sampleRate, float secondsSinceLast) {
  const float fall = 48.0f * secondsSinceLast; // dB per refresh
  if (!fresh || sampleRate <= 0.0) {
    // Nothing playing: let the curve sink away rather than freeze.
    bool moving = false;
    for (auto &l : levelsDb) {
      moving = moving || l > floorDb;
      l = juce::jmax(floorDb, l - fall);
    }
    if (moving)
      repaint();
    return;
  }
  fresh = false;

  // Oldest sample first, windowed.
  const auto split = (size_t)historyWrite;
  std::copy(history.begin() + (long)split, history.end(), fftData.begin());
  std::copy(history.begin(), history.begin() + (long)split,
            fftData.begin() + (long)(fftSize - split));
  window.multiplyWithWindowingTable(fftData.data(), (size_t)fftSize);
  fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

  // A full-scale sine reads 0 dB (Hann halves the peak bin), tilted +3 dB per
  // octave about 1 kHz so a guitar's falling top end still reads clearly.
  const double binHz = sampleRate / fftSize;
  const int lastBin = fftSize / 2;
  const auto binDb = [&](double bin) {
    const auto i = juce::jlimit(0, lastBin - 1, (int)bin);
    const auto t = (float)juce::jlimit(0.0, 1.0, bin - i);
    const float mag = fftData[(size_t)i] + t * (fftData[(size_t)i + 1] -
                                                fftData[(size_t)i]);
    return juce::Decibels::gainToDecibels(mag * 4.0f / fftSize, -200.0f);
  };
  const double octaves = std::log2(highHz / lowHz);
  const double halfStep = 0.5 * octaves / (numPoints - 1);
  for (int p = 0; p < numPoints; ++p) {
    const double octave = octaves * p / (numPoints - 1);
    const double hz = lowHz * std::exp2(octave);
    const double from = lowHz * std::exp2(octave - halfStep) / binHz;
    const double to = lowHz * std::exp2(octave + halfStep) / binHz;
    float db = binDb(hz / binHz);
    for (int b = (int)std::ceil(from); b <= (int)to && b <= lastBin; ++b)
      db = juce::jmax(db, binDb(b)); // wide at the top: the loudest bin
    db += 3.0f * (float)std::log2(hz / 1000.0);

    auto &l = levelsDb[(size_t)p];
    l = db > l ? l + 0.6f * (db - l) : juce::jmax(db, l - fall);
    l = juce::jlimit(floorDb, topDb, l);
  }
  repaint();
}

void Spectrum::paint(juce::Graphics &g) {
  using namespace colours;
  const auto bounds = getLocalBounds().toFloat();
  // The EQ's scale runs between where a fader's cap can reach.
  constexpr float trackInset = 12.0f;

  graticule.draw(g, getLocalBounds(), juce::roundToInt(scaleDb * 10.0),
                 [this, bounds](juce::Graphics &c) {
    drawRecess(c, bounds, 3.0f);
    const auto columns = (float)juce::jmax(1, (int)bands.size());
    c.setColour(line.withAlpha(0.05f));
    for (float i = 1.0f; i < columns; i += 1.0f)
      c.fillRect(bounds.getX() + bounds.getWidth() * i / columns, bounds.getY() + 1.0f,
                 1.0f, bounds.getHeight() - 2.0f);
    const auto scale = bounds.reduced(0.0f, trackInset);
    c.setFont(readout(8.5f));
    for (int step = -2; step <= 2; ++step) {
      const float y = scale.getY() + scale.getHeight() * ((float)step + 2.0f) * 0.25f;
      c.setColour(line.withAlpha(step == 0 ? 0.13f : 0.05f));
      c.fillRect(bounds.getX() + 1.0f, y - 0.5f, bounds.getWidth() - 2.0f, 1.0f);
      if (step % 2 == 0) {
        const int db = juce::roundToInt(scaleDb * (float)-step * 0.5f);
        c.setColour(mutedSoft);
        c.drawText((db > 0 ? "+" : "") + juce::String(db),
                   juce::Rectangle<float>(bounds.getX() + 5.0f, y - 12.0f, 24.0f, 11.0f),
                   juce::Justification::centredLeft, false);
      }
    }
  });

  const auto inner = bounds.reduced(1.0f);
  const bool hearing = std::any_of(levelsDb.begin(), levelsDb.end(),
                                   [](float level) { return level > floorDb + 0.5f; });
  juce::Path clip;
  clip.addRoundedRectangle(inner, 2.0f);
  const juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(clip);

  const auto point = [&](int p) {
    return juce::Point<float>(
        juce::jmap((float)p, 0.0f, (float)(numPoints - 1), inner.getX(),
                   inner.getRight()),
        juce::jmap(levelsDb[(size_t)p], floorDb, topDb, inner.getBottom(),
                   inner.getY()));
  };

  if (hearing) {
    // Smooth: quadratic segments through the midpoints between points.
    juce::Path curve;
    curve.startNewSubPath(point(0));
    for (int p = 1; p < numPoints - 1; ++p) {
      const auto here = point(p), next = point(p + 1);
      curve.quadraticTo(here, (here + next) * 0.5f);
    }
    curve.lineTo(point(numPoints - 1));

    auto fill = curve;
    fill.lineTo(bounds.getBottomRight());
    fill.lineTo(bounds.getBottomLeft());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(accent().withAlpha(0.46f), 0.0f, inner.getY(),
                                           accent().withAlpha(0.03f), 0.0f, inner.getBottom(),
                                           false));
    g.fillPath(fill);
    g.setColour(accent().withAlpha(0.22f));
    g.strokePath(curve, juce::PathStrokeType(3.0f));
    g.setColour(accent().interpolatedWith(juce::Colours::white, 0.12f).withAlpha(0.9f));
    g.strokePath(curve, juce::PathStrokeType(1.1f));
  }

  // The EQ's shape, through the fader caps and out flat past the ends.
  if (!bands.empty()) {
    const auto scale = bounds.reduced(0.0f, trackInset);
    const auto bandPoint = [&](size_t i) {
      return juce::Point<float>(
          bounds.getX() + bounds.getWidth() * ((float)i + 0.5f) / (float)bands.size(),
          scale.getBottom() - scale.getHeight() * bands[i]);
    };
    juce::Path shape;
    shape.startNewSubPath(bounds.getX(), bandPoint(0).y);
    shape.lineTo(bandPoint(0));
    for (size_t i = 1; i + 1 < bands.size(); ++i) {
      const auto here = bandPoint(i), next = bandPoint(i + 1);
      shape.quadraticTo(here, (here + next) * 0.5f);
    }
    shape.lineTo(bandPoint(bands.size() - 1));
    shape.lineTo(bounds.getRight(), bandPoint(bands.size() - 1).y);
    g.setColour(text.withAlpha(0.1f));
    g.strokePath(shape, juce::PathStrokeType(3.0f));
    g.setColour(text.withAlpha(0.55f));
    g.strokePath(shape, juce::PathStrokeType(1.1f));
  }
}

void RecordButton::showRecording(double seconds) {
  const int whole = (int)seconds;
  if (recording && whole == shownSeconds)
    return;
  recording = true;
  shownSeconds = whole;
  const int hours = whole / 3600, minutes = whole / 60 % 60, secs = whole % 60;
  setButtonText(hours > 0 ? juce::String::formatted("%d:%02d:%02d", hours, minutes, secs)
                          : juce::String::formatted("%d:%02d", minutes, secs));
  repaint();
}

void RecordButton::showIdle(const char *label) {
  if (!recording && getButtonText() == label)
    return;
  recording = false;
  setButtonText(label);
  repaint();
}

juce::String RecordButton::getTooltip() {
  return recording ? juce::String("Stop, and save the take to your Downloads folder")
                   : juce::TextButton::getTooltip();
}

void RecordButton::paintButton(juce::Graphics &g, bool highlighted, bool down) {
  getLookAndFeel().drawButtonBackground(g, *this, {}, highlighted, down);

  auto area = getLocalBounds().translated(0, down ? 1 : 0);
  const auto lightArea = area.removeFromLeft(21).toFloat();
  drawLed(g, juce::Rectangle<float>(8.0f, 8.0f).withCentre(lightArea.getCentre().translated(2.0f, 0.0f)),
          recording, colours::recordLight);

  g.setColour(recording || highlighted ? colours::text : colours::textSoft);
  g.setFont(recording ? readout(14.0f) : silk(12.5f, 0.08f));
  g.drawFittedText(recording ? getButtonText() : getButtonText().toUpperCase(),
                   area.withTrimmedRight(4), juce::Justification::centred, 1, 0.7f);
}

void Footswitch::paintButton(juce::Graphics &g, bool highlighted, bool down) {
  const auto size = juce::jmin(getWidth(), getHeight());
  const auto at = getLocalBounds().toFloat().getCentre().translated(
      -(float)size * 0.5f, -(float)size * 0.5f);
  (down ? pressed : up).draw(g, at, size, size, 0, [size, down](juce::Graphics &c) {
    const auto centre = juce::Point<float>((float)size, (float)size) * 0.5f;
    const float nut = (float)size * 0.43f;

    // The shadow it throws on the enclosure.
    juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.65f),
                                centre.translated(0.0f, 2.0f),
                                juce::Colours::black.withAlpha(0.0f),
                                centre.translated(nut + 2.0f, 2.0f), true);
    shadow.addColour(0.8, juce::Colours::black.withAlpha(0.5f));
    c.setGradientFill(shadow);
    c.fillEllipse(circle(centre.translated(0.0f, 2.0f), nut + 2.0f));

    // The hex nut.
    juce::Path hex;
    hex.addPolygon(centre, 6, nut, juce::MathConstants<float>::pi / 6.0f);
    juce::ColourGradient nutFill(juce::Colour(0xff9c9ca2), centre.x - nut, centre.y - nut,
                                 juce::Colour(0xff222225), centre.x + nut, centre.y + nut,
                                 false);
    nutFill.addColour(0.5, juce::Colour(0xff515156));
    c.setGradientFill(nutFill);
    c.fillPath(hex);
    c.setColour(juce::Colours::black.withAlpha(0.65f));
    c.strokePath(hex, juce::PathStrokeType(0.9f));

    // The plunger: a chrome dome, sitting lower while it's held.
    const float dome = nut * (down ? 0.68f : 0.74f);
    c.setColour(juce::Colours::black.withAlpha(0.75f));
    c.fillEllipse(circle(centre.translated(0.0f, 0.8f), dome + 1.2f));
    juce::ColourGradient chrome(juce::Colours::white, centre.translated(-dome * 0.3f, -dome * 0.45f),
                                juce::Colour(0xff17171a), centre.translated(dome * 0.7f, dome * 0.95f),
                                true);
    chrome.addColour(0.35, juce::Colour(0xffb6b6bc));
    chrome.addColour(0.72, juce::Colour(0xff44444a));
    chrome.addColour(0.9, juce::Colour(0xff8c8c92));
    c.setGradientFill(chrome);
    c.fillEllipse(circle(centre, dome));
  });
  if (highlighted) {
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillEllipse(circle(getLocalBounds().toFloat().getCentre(), (float)size * 0.32f));
  }
}
} // namespace evil::gui
