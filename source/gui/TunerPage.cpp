#include "TunerPage.h"

#include "EvilLookAndFeel.h"
#include "Widgets.h"

namespace evil::gui {
namespace {
constexpr float gaugeCents = 50.0f;
constexpr int rowInset = 14;
const auto customName = "custom (not saved)";

juce::String signedCents(double cents) {
  const int rounded = juce::roundToInt(cents);
  return (rounded > 0 ? "+" : "") + juce::String(rounded) + " ct";
}
} // namespace

TunerPage::TunerPage() : strings(library->getCurrent()) {
  addAndMakeVisible(tuningBox);
  tuningBox.setTooltip("Start from a built-in tuning or one you saved");
  tuningBox.onChange = [this] {
    const int index = tuningBox.getSelectedItemIndex();
    const auto all = library->getAll();
    if (juce::isPositiveAndBelow(index, (int)all.size())) {
      library->setCurrent(all[(std::size_t)index].strings);
      showStrings(library->getCurrent());
    }
  };

  addAndMakeVisible(saveButton);
  gui::setPrimary(saveButton);
  saveButton.setTooltip("Save these notes as a tuning of your own");
  saveButton.onClick = [this] { saveDialog(); };

  addAndMakeVisible(deleteButton);
  deleteButton.setTooltip("Delete this saved tuning; its notes stay set until "
                          "you change them");
  deleteButton.onClick = [this] {
    const int index = library->indexOf(strings);
    if (library->isUserTuning(index))
      library->deleteUserTuning(library->getAll()[(std::size_t)index].name);
    refreshTuningList();
  };

  addAndMakeVisible(autoButton);
  autoButton.setTooltip("Tune whichever string's note is closest to what it "
                        "hears");
  autoButton.onClick = [this] { lockString(-1); };

  for (std::size_t s = 0; s < rows.size(); ++s) {
    auto &row = rows[s];
    const auto number = juce::String((int)s + 1);

    row.number.setButtonText(number);
    row.number.setTooltip("Tune only string " + number + ": for a string too "
                          "far off to be told from its neighbours. Click again "
                          "to go back to auto.");
    row.number.onClick = [this, s] {
      lockString(lockedString == (int)s ? -1 : (int)s);
    };
    addAndMakeVisible(row.number);

    row.noteBox.addItemList(tuning::noteNames(), 1);
    row.noteBox.setTooltip("The note string " + number + " should be");
    row.noteBox.onChange = [this, s] {
      auto note = tuning::nearestNote(s, rows[s].noteBox.getText());
      if (!note)
        return;
      // The same note spelled differently stays in the octave it was in.
      if (note->midi % 12 == strings[s].midi % 12)
        note->midi = strings[s].midi;
      setNote(s, *note);
    };
    addAndMakeVisible(row.noteBox);

    for (auto [delta, button] : {std::pair{-12, &row.octaveDown},
                                 std::pair{12, &row.octaveUp}}) {
      button->setTooltip(juce::String(delta < 0 ? "Down" : "Up") +
                         " an octave");
      button->onClick = [this, s, delta = delta] {
        setNote(s, {strings[s].midi + delta, strings[s].flat});
      };
      addAndMakeVisible(button);
    }
  }

  library->addChangeListener(this);
  refreshControls();
}

TunerPage::~TunerPage() { library->removeChangeListener(this); }

void TunerPage::changeListenerCallback(juce::ChangeBroadcaster *) {
  showStrings(library->getCurrent());
}

void TunerPage::showStrings(const tuning::Strings &newStrings) {
  strings = newStrings;
  updateReading(reading.hz);
  refreshControls();
  repaint();
}

void TunerPage::setNote(std::size_t string, tuning::Note note) {
  auto edited = strings;
  edited[string] = note;
  library->setCurrent(edited);
  showStrings(edited);
}

void TunerPage::lockString(int string) {
  lockedString = string;
  updateReading(reading.hz);
  refreshControls();
  repaint();
}

void TunerPage::updateReading(double hz) {
  reading = tuning::read(hz, strings, lockedString);
}

void TunerPage::reset() {
  detector.reset();
  updateReading(0.0);
  repaint();
}

void TunerPage::refresh(double sampleRate, float secondsSinceLast) {
  if (sampleRate > 0.0 &&
      !juce::approximatelyEqual(sampleRate, detector.getSampleRate()))
    detector.prepare(sampleRate);

  // Every other call: 15 readings a second is plenty, and each costs about a
  // millisecond. The needle still eases at the full rate.
  secondsSinceAnalysis += secondsSinceLast;
  analyseNext = !analyseNext;
  const auto before = reading;
  if (analyseNext) {
    updateReading(detector.track(secondsSinceAnalysis));
    secondsSinceAnalysis = 0.0f;
  }

  const bool playing = reading.hz > 0.0;
  const auto target =
      juce::jlimit(-gaugeCents, gaugeCents, (float)reading.cents);
  if (playing && before.hz <= 0.0)
    needle = target;
  else if (playing)
    needle += 0.35f * (target - needle);

  if (playing || before.hz > 0.0)
    repaint(meterPanel);
  if (reading.string != before.string)
    repaint(stringsPanel);
}

void TunerPage::refreshControls() {
  refreshTuningList();
  autoButton.setToggleState(lockedString < 0, juce::dontSendNotification);
  for (std::size_t s = 0; s < rows.size(); ++s) {
    auto &row = rows[s];
    const auto note = strings[s];
    row.number.setToggleState(lockedString == (int)s,
                              juce::dontSendNotification);
    row.noteBox.setSelectedItemIndex(
        tuning::noteNames().indexOf(tuning::noteName(note)),
        juce::dontSendNotification);
    row.octaveDown.setEnabled(note.midi - 12 >= tuning::lowestNote);
    row.octaveUp.setEnabled(note.midi + 12 <= tuning::highestNote);
  }
}

void TunerPage::refreshTuningList() {
  const auto all = library->getAll();
  tuningBox.clear(juce::dontSendNotification);
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (i > 0 && i == tuning::builtinTunings().size())
      tuningBox.addSeparator();
    tuningBox.addItem(all[i].name, (int)i + 1);
  }
  const int index = library->indexOf(strings);
  if (index >= 0)
    tuningBox.setSelectedItemIndex(index, juce::dontSendNotification);
  else
    tuningBox.setText(customName, juce::dontSendNotification);
  deleteButton.setEnabled(library->isUserTuning(index));
}

void TunerPage::saveDialog() {
  const int index = library->indexOf(strings);
  auto *window = new juce::AlertWindow(
      "Save tuning",
      "Name this tuning. Using the name of one you saved replaces it.",
      juce::MessageBoxIconType::NoIcon, this);
  window->addTextEditor("name", library->isUserTuning(index)
                                    ? library->getAll()[(std::size_t)index].name
                                    : juce::String());
  window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  window->setLookAndFeel(&getLookAndFeel());

  juce::Component::SafePointer<TunerPage> safeThis(this);
  window->enterModalState(
      true, juce::ModalCallbackFunction::create([safeThis, window](int result) {
        // Runs before the window is deleted (deleteWhenDismissed below).
        if (safeThis == nullptr || result != 1)
          return;
        juce::String error;
        if (!safeThis->library->saveUserTuning(
                window->getTextEditorContents("name"), safeThis->strings,
                error)) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Tuning not saved", error);
          return;
        }
        safeThis->refreshTuningList();
      }),
      true);
}

void TunerPage::resized() {
  auto b = getLocalBounds();
  stringsPanel = b.removeFromRight(360);
  b.removeFromRight(12);
  meterPanel = b;

  // Meter: a display with which string, the note and what to do; then the gauge
  // and the numbers.
  {
    auto m = meterPanel.reduced(22, 0).withTrimmedTop(38).withTrimmedBottom(22);
    statsArea = m.removeFromBottom(52);
    m.removeFromBottom(10);
    gaugeWell = m.removeFromBottom(92);
    gaugeArea = gaugeWell.reduced(14, 8);
    m.removeFromBottom(10);
    displayArea = m;
    auto d = displayArea.reduced(0, 12);
    captionArea = d.removeFromTop(18);
    hintArea = d.removeFromBottom(30);
    noteArea = d;
  }

  // Strings: the tuning, a row per string, then auto and how locking works.
  auto s = stringsPanel.reduced(rowInset + 4, 0).withTrimmedTop(38);
  s.removeFromBottom(22);
  {
    auto top = s.removeFromTop(30);
    tuningBox.setBounds(top.removeFromLeft(182));
    top.removeFromLeft(6);
    saveButton.setBounds(top.removeFromLeft(60));
    top.removeFromLeft(6);
    deleteButton.setBounds(top);
  }
  s.removeFromTop(12);
  headerArea = s.removeFromTop(18);
  auto foot = s.removeFromBottom(32);
  autoButton.setBounds(foot.removeFromLeft(56));
  foot.removeFromLeft(10);
  footHintArea = foot;
  s.removeFromBottom(10);

  const int rowHeight = s.getHeight() / tuning::numStrings;
  for (auto &row : rows) {
    row.bounds = s.removeFromTop(rowHeight);
    auto r = row.bounds;
    const auto next = [&r](int width, int gapAfter) {
      const auto cell = r.removeFromLeft(width).withSizeKeepingCentre(width, 30);
      r.removeFromLeft(gapAfter);
      return cell;
    };
    row.number.setBounds(next(34, 22));
    row.noteBox.setBounds(next(80, 14));
    row.octaveDown.setBounds(next(26, 0));
    row.octave = next(24, 0);
    row.octaveUp.setBounds(next(26, 6));
    row.hz = r;
  }
}

void TunerPage::paint(juce::Graphics &g) {
  frame.draw(g, getLocalBounds(), 0, [this](juce::Graphics &c) {
    drawPanel(c, meterPanel, "tuner");
    drawPanel(c, stringsPanel, "tuning");
    drawRecess(c, displayArea.toFloat(), 4.0f);
    drawRecess(c, gaugeWell.toFloat(), 3.0f);
    drawRecess(c, statsArea.toFloat(), 3.0f);
    paintStringsFixed(c);
  });
  paintMeter(g);
  paintStrings(g);
}

void TunerPage::paintMeter(juce::Graphics &g) {
  using namespace colours;
  const bool playing = reading.hz > 0.0;
  const bool inTune = reading.isInTune();
  const bool hasTarget = reading.string >= 0;

  // An LED in the display's corner: the go-ahead.
  const auto led = juce::Rectangle<float>(10.0f, 10.0f).withCentre(
      {(float)displayArea.getRight() - 18.0f, (float)displayArea.getY() + 18.0f});
  drawLed(g, led, inTune, accent());

  g.setFont(silk(12.5f, 0.14f));
  g.setColour(textSoft);
  g.drawText(!hasTarget ? juce::String("AUTO")
                        : "STRING " + juce::String(reading.string + 1) +
                              (lockedString >= 0 ? " " +
                                                       juce::String::charToString(0x00b7) +
                                                       " LOCKED"
                                                 : juce::String()),
             captionArea, juce::Justification::centred);

  // The note, with its octave small beside it, glowing once it's in tune.
  const auto colour = !playing ? mutedSoft : inTune ? accent() : text;
  const auto name = hasTarget ? tuning::noteName(reading.target) : juce::String("--");
  const auto octave = hasTarget ? juce::String(tuning::octaveOf(reading.target.midi))
                                : juce::String();
  const auto key = (name + "/" + octave + (inTune ? "/tuned" : "") +
                    (playing ? "/playing" : ""))
                       .hashCode64();
  noteCache.draw(g, noteArea, key, [=, this](juce::Graphics &c) {
    const auto area = noteArea.withZeroOrigin().toFloat();
    if (!hasTarget) { // nothing to show yet: just a pair of dashes
      c.setFont(readout(72.0f));
      c.setColour(mutedSoft);
      c.drawText(name, area, juce::Justification::centred, false);
      return;
    }
    const auto noteFont = display(164.0f);
    const auto octaveFont = readout(30.0f);
    const float nameWidth = juce::GlyphArrangement::getStringWidth(noteFont, name);
    const float octaveWidth = juce::GlyphArrangement::getStringWidth(octaveFont, octave);
    auto row = area.withSizeKeepingCentre(nameWidth + 8.0f + octaveWidth, area.getHeight());

    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(noteFont, name, row.getX(), row.getY(), nameWidth + 8.0f,
                         row.getHeight(), juce::Justification::centredLeft, 1, 1.0f);
    juce::Path letters;
    glyphs.createPath(letters);
    if (inTune)
      juce::DropShadow(accent().withAlpha(0.85f), 26, {}).drawForPath(c, letters);
    c.setColour(colour);
    c.fillPath(letters);

    c.setFont(octaveFont);
    c.drawText(octave, row.withTrimmedLeft(nameWidth + 8.0f).withTrimmedTop(row.getHeight() * 0.5f),
               juce::Justification::centredLeft, false);
  });

  // What to do about it.
  juce::String hint;
  if (!playing)
    hint = hasTarget ? "play string " + juce::String(reading.string + 1)
                     : juce::String("play a string");
  else if (inTune)
    hint = "in tune";
  else {
    hint = reading.cents < 0.0 ? "tune up" : "tune down";
    if (std::abs(reading.cents) >= 100.0)
      hint << " " << juce::String(std::abs(reading.cents) / 100.0, 1)
           << " semitones";
  }
  g.setFont(silk(17.0f, 0.12f));
  g.setColour(inTune ? accent() : playing ? text : muted);
  g.drawText(hint.toUpperCase(), hintArea, juce::Justification::centred, false);

  paintGauge(g);
  paintStats(g);
}

void TunerPage::paintGauge(juce::Graphics &g) {
  using namespace colours;
  const bool playing = reading.hz > 0.0;
  const bool inTune = reading.isInTune();

  auto area = gaugeArea.toFloat();
  const auto captions = area.removeFromTop(13.0f);
  const auto labels = area.removeFromBottom(13.0f);
  area.removeFromBottom(3.0f);
  const auto track = area.reduced(8.0f, 2.0f);
  const auto xOf = [&track](float cents) {
    return juce::jmap(cents, -gaugeCents, gaugeCents, track.getX(), track.getRight());
  };

  g.setFont(caption());
  g.setColour(muted);
  g.drawText("FLAT", captions, juce::Justification::centredLeft, false);
  g.drawText("SHARP", captions, juce::Justification::centredRight, false);

  // A ladder of LEDs, 2.5 cents apart: lit from the centre out to the note.
  const float spacing = xOf(2.5f) - xOf(0.0f);
  const float width = juce::jmax(2.0f, spacing * 0.56f);
  const auto zone = (float)tuning::inTuneCents;
  const float at = playing ? juce::jlimit(-gaugeCents, gaugeCents, needle) : 0.0f;
  for (int i = -20; i <= 20; ++i) {
    const float cents = 2.5f * (float)i;
    const bool centre = i == 0, major = i % 10 == 0;
    const float height = centre ? track.getHeight()
                         : major ? track.getHeight() * 0.78f
                                 : track.getHeight() * 0.58f;
    const auto led = juce::Rectangle<float>(centre ? width * 1.5f : width, height)
                         .withCentre({xOf(cents), track.getCentreY()});
    const bool nearest = playing && std::abs(cents - at) <= 1.25f;
    const bool between = playing && ((at >= 0.0f && cents >= 0.0f && cents < at) ||
                                     (at < 0.0f && cents <= 0.0f && cents > at));
    const bool inZone = std::abs(cents) <= zone;
    juce::Colour colour = (inZone ? accent() : text).withAlpha(inZone ? 0.2f : 0.075f);
    if (between)
      colour = text.withAlpha(0.4f);
    if (nearest) {
      const auto lit = inTune ? accent() : text;
      g.setGradientFill(juce::ColourGradient(lit.withAlpha(0.5f), led.getCentre(),
                                             lit.withAlpha(0.0f),
                                             led.getCentre().translated(20.0f, 0.0f), true));
      g.fillEllipse(circle(led.getCentre(), 20.0f));
      colour = lit;
    }
    g.setColour(colour);
    g.fillRoundedRectangle(led, 1.0f);
  }

  g.setFont(readout(11.0f));
  g.setColour(muted);
  for (int cents = -50; cents <= 50; cents += 25)
    g.drawText((cents > 0 ? "+" : "") + juce::String(cents),
               juce::Rectangle<float>(40.0f, labels.getHeight())
                   .withCentre({xOf((float)cents), labels.getCentreY()}),
               juce::Justification::centred, false);
}

void TunerPage::paintStats(juce::Graphics &g) {
  using namespace colours;
  const bool playing = reading.hz > 0.0;
  const bool hasTarget = reading.string >= 0;

  const juce::String none = "--";
  const std::array<std::pair<const char *, juce::String>, 4> stats{{
      {"OFF BY", playing ? signedCents(reading.cents) : none},
      {"TARGET", hasTarget ? juce::String(tuning::frequencyOf(reading.target.midi), 1) +
                                 " Hz"
                           : none},
      {"HEARING", playing ? juce::String(reading.hz, 1) + " Hz" : none},
      {"NEAREST NOTE", playing ? tuning::noteName(reading.heard) +
                                     juce::String(tuning::octaveOf(reading.heard.midi)) +
                                     " " + signedCents(reading.heardCents)
                               : none},
  }};
  auto b = statsArea.reduced(14, 8);
  const int width = b.getWidth() / (int)stats.size();
  for (const auto &[title, value] : stats) {
    auto column = b.removeFromLeft(width);
    g.setFont(caption());
    g.setColour(muted);
    g.drawText(title, column.removeFromTop(16), juce::Justification::centredLeft,
               false);
    g.setFont(readout(15.0f));
    g.setColour(playing || value != none ? text : mutedSoft);
    g.drawText(value, column, juce::Justification::centredLeft, false);
  }
}

void TunerPage::paintStringsFixed(juce::Graphics &g) {
  using namespace colours;
  // Column headings over their columns.
  g.setFont(caption());
  g.setColour(textSoft);
  const auto heading = [&](const char *title, juce::Rectangle<int> column,
                           juce::Justification justification) {
    g.drawText(title, column.withY(headerArea.getY()).withHeight(headerArea.getHeight()),
               justification, false);
  };
  const auto &first = rows.front();
  heading("STRING", first.number.getBounds().withWidth(46),
          juce::Justification::centredLeft);
  heading("NOTE", first.noteBox.getBounds(), juce::Justification::centredLeft);
  heading("OCTAVE",
          first.octaveDown.getBounds().getUnion(first.octaveUp.getBounds()),
          juce::Justification::centred);
  heading("PITCH", first.hz, juce::Justification::centredRight);

  g.setFont(sans(13.0f));
  g.setColour(muted);
  g.drawFittedText("Click a string's number to lock onto it.\n"
                   "String 1 is the thinnest.",
                   footHintArea, juce::Justification::centredLeft, 2, 1.0f);
}

void TunerPage::paintStrings(juce::Graphics &g) {
  using namespace colours;
  // A ledger of the strings, the one being tuned picked out in the accent.
  for (std::size_t s = 0; s < rows.size(); ++s) {
    const auto &row = rows[s];
    const auto note = strings[s];
    const bool active = reading.string == (int)s;
    if (active) {
      g.setColour(accent().withAlpha(0.14f));
      g.fillRoundedRectangle(row.bounds.toFloat().expanded(6.0f, 0.0f).reduced(0.0f, 1.0f),
                             3.0f);
      g.setColour(accent());
      g.fillRect(row.bounds.toFloat().withWidth(2.0f).translated(-6.0f, 0.0f).reduced(0.0f, 3.0f));
    }
    g.setColour(lineSoft);
    g.fillRect(row.bounds.getX(), row.bounds.getY(), row.bounds.getWidth(), 1);

    g.setFont(readout(15.0f));
    g.setColour(text);
    g.drawText(juce::String(tuning::octaveOf(note.midi)), row.octave,
               juce::Justification::centred, false);
    g.setFont(readout(13.0f));
    g.setColour(active ? text : textSoft);
    g.drawText(juce::String(tuning::frequencyOf(note.midi), 1) + " Hz", row.hz,
               juce::Justification::centredRight, false);
  }
  g.setColour(lineSoft);
  g.fillRect(headerArea.getX(), rows.back().bounds.getBottom(),
             headerArea.getWidth(), 1);
}
} // namespace evil::gui
