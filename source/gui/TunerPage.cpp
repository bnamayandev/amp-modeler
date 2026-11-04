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

  // Meter: which string, the note, what to do; then the gauge and the numbers.
  {
    auto m = meterPanel.reduced(28, 0).withTrimmedTop(46).withTrimmedBottom(16);
    statsArea = m.removeFromBottom(50);
    m.removeFromBottom(22);
    gaugeArea = m.removeFromBottom(84);
    m.removeFromBottom(14);
    captionArea = m.removeFromTop(18);
    hintArea = m.removeFromBottom(30);
    noteArea = m;
  }

  // Strings: the tuning, a row per string, then auto and how locking works.
  auto s = stringsPanel.reduced(rowInset, 0).withTrimmedTop(44);
  s.removeFromBottom(14);
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
    row.number.setBounds(next(34, 24));
    row.noteBox.setBounds(next(84, 16));
    row.octaveDown.setBounds(next(26, 0));
    row.octave = next(24, 0);
    row.octaveUp.setBounds(next(26, 8));
    row.hz = r;
  }
}

void TunerPage::paint(juce::Graphics &g) {
  paintMeter(g);
  paintStrings(g);
}

void TunerPage::paintMeter(juce::Graphics &g) {
  using namespace colours;
  const auto rule = drawPanel(g, meterPanel, 0, "tuner");
  const bool playing = reading.hz > 0.0;
  const bool inTune = reading.isInTune();
  const bool hasTarget = reading.string >= 0;

  // An LED at the end of the header rule, as on the other panels.
  const auto led = juce::Rectangle<float>(8.0f, 8.0f).withCentre(
      {rule.getRight() - 4.0f, rule.getCentreY()});
  g.setColour(panel);
  g.fillRect(led.expanded(8.0f, 0.0f).withHeight(2.0f).withCentre(
      led.getCentre())); // gap in the rule
  drawLed(g, led, inTune, accent());

  g.setFont(caption());
  g.setColour(muted);
  g.drawText(!hasTarget ? juce::String("AUTO")
                        : "STRING " + juce::String(reading.string + 1) +
                              (lockedString >= 0 ? " " +
                                                       juce::String::charToString(0x00b7) +
                                                       " LOCKED"
                                                 : juce::String()),
             captionArea, juce::Justification::centred);

  // The note, with its octave small beside it.
  const auto noteFont = sans(132.0f, true, -0.03f);
  g.setColour(!playing ? mutedSoft : inTune ? accent() : text);
  if (hasTarget) {
    const auto name = tuning::noteName(reading.target);
    const auto octave = juce::String(tuning::octaveOf(reading.target.midi));
    const auto octaveFont = mono(30.0f);
    const float nameWidth = juce::GlyphArrangement::getStringWidth(noteFont, name);
    const float octaveWidth =
        juce::GlyphArrangement::getStringWidth(octaveFont, octave);
    auto row = noteArea.toFloat().withSizeKeepingCentre(
        nameWidth + 6.0f + octaveWidth, (float)noteArea.getHeight());
    g.setFont(noteFont);
    g.drawText(name, row.removeFromLeft(nameWidth + 6.0f),
               juce::Justification::centredLeft, false);
    g.setFont(octaveFont);
    g.drawText(octave, row.withTrimmedTop(row.getHeight() * 0.5f),
               juce::Justification::centredLeft, false);
  } else {
    g.setColour(line);
    g.setFont(noteFont);
    g.drawText("--", noteArea, juce::Justification::centred, false);
  }

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
  g.setFont(sans(18.0f, inTune));
  g.setColour(inTune ? accent() : playing ? textSoft : muted);
  g.drawText(hint, hintArea, juce::Justification::centred, false);

  paintGauge(g);
  paintStats(g);
}

void TunerPage::paintGauge(juce::Graphics &g) {
  using namespace colours;
  const bool playing = reading.hz > 0.0;
  const bool inTune = reading.isInTune();

  auto area = gaugeArea.toFloat();
  const auto captions = area.removeFromTop(16.0f);
  const auto labels = area.removeFromBottom(16.0f);
  area.removeFromBottom(4.0f);
  // Inset so the end labels and the pinned needle stay inside the panel.
  const auto track = area.reduced(10.0f, 2.0f);
  const float mid = track.getCentreY();
  const auto xOf = [&track](float cents) {
    return juce::jmap(cents, -gaugeCents, gaugeCents, track.getX(),
                      track.getRight());
  };

  g.setFont(caption());
  g.setColour(mutedSoft);
  g.drawText("FLAT", captions, juce::Justification::centredLeft, false);
  g.drawText("SHARP", captions, juce::Justification::centredRight, false);

  // The in-tune zone, then a tick every 5 cents and a longer one every 25.
  const auto zone = (float)tuning::inTuneCents;
  g.setColour(accent().withAlpha(inTune ? 0.42f : 0.16f));
  g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(
                             xOf(-zone), track.getY(), xOf(zone),
                             track.getBottom()),
                         2.0f);
  g.setFont(mono(10.5f));
  for (int cents = -50; cents <= 50; cents += 5) {
    const bool major = cents % 25 == 0;
    const float half = cents == 0 ? track.getHeight() * 0.5f
                       : major    ? track.getHeight() * 0.32f
                                  : track.getHeight() * 0.14f;
    const float x = xOf((float)cents);
    g.setColour(major ? muted : line);
    g.fillRect(x - 0.5f, mid - half, 1.0f, half * 2.0f);
    if (major) {
      g.setColour(muted);
      g.drawText((cents > 0 ? "+" : "") + juce::String(cents),
                 juce::Rectangle<float>(40.0f, labels.getHeight())
                     .withCentre({x, labels.getCentreY()}),
                 juce::Justification::centred, false);
    }
  }
  if (!playing)
    return;

  // The needle, with the distance from centre drawn in like a fader's value.
  const float x = xOf(needle), centre = xOf(0.0f);
  const auto colour = inTune ? accent() : text;
  g.setColour(colour.withAlpha(0.35f));
  g.fillRect(juce::Rectangle<float>::leftTopRightBottom(
      juce::jmin(x, centre), mid - 1.5f, juce::jmax(x, centre), mid + 1.5f));
  if (inTune) {
    g.setGradientFill(juce::ColourGradient(
        accent().withAlpha(0.5f), {x, mid}, accent().withAlpha(0.0f),
        {x + 24.0f, mid}, true));
    g.fillEllipse(juce::Rectangle<float>(48.0f, 48.0f).withCentre({x, mid}));
  }
  g.setColour(colour);
  g.fillRoundedRectangle(
      juce::Rectangle<float>(5.0f, track.getHeight()).withCentre({x, mid}), 2.5f);
}

void TunerPage::paintStats(juce::Graphics &g) {
  using namespace colours;
  const bool playing = reading.hz > 0.0;
  const bool hasTarget = reading.string >= 0;

  g.setColour(line);
  g.fillRect(statsArea.getX(), statsArea.getY(), statsArea.getWidth(), 1);

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
  auto b = statsArea.withTrimmedTop(12);
  const int width = b.getWidth() / (int)stats.size();
  for (const auto &[title, value] : stats) {
    auto column = b.removeFromLeft(width);
    g.setFont(caption());
    g.setColour(muted);
    g.drawText(title, column.removeFromTop(16), juce::Justification::centredLeft,
               false);
    g.setFont(mono(13.0f));
    g.setColour(playing || value != none ? textSoft : mutedSoft);
    g.drawText(value, column, juce::Justification::centredLeft, false);
  }
}

void TunerPage::paintStrings(juce::Graphics &g) {
  using namespace colours;
  drawPanel(g, stringsPanel, 0, "tuning");

  // Column headings over their columns.
  g.setFont(caption());
  g.setColour(muted);
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

  // A ledger of the strings, the one being tuned picked out in the accent.
  for (std::size_t s = 0; s < rows.size(); ++s) {
    const auto &row = rows[s];
    const auto note = strings[s];
    const bool active = reading.string == (int)s;
    if (active) {
      g.setColour(accent().withAlpha(0.1f));
      g.fillRoundedRectangle(row.bounds.toFloat().expanded(6.0f, 0.0f).reduced(0.0f, 1.0f),
                             6.0f);
    }
    g.setColour(lineSoft);
    g.fillRect(row.bounds.getX(), row.bounds.getY(), row.bounds.getWidth(), 1);

    g.setFont(mono(13.0f));
    g.setColour(text);
    g.drawText(juce::String(tuning::octaveOf(note.midi)), row.octave,
               juce::Justification::centred, false);
    g.setFont(mono(12.0f));
    g.setColour(active ? textSoft : muted);
    g.drawText(juce::String(tuning::frequencyOf(note.midi), 1) + " Hz", row.hz,
               juce::Justification::centredRight, false);
  }
  g.setColour(lineSoft);
  g.fillRect(headerArea.getX(), rows.back().bounds.getBottom(),
             headerArea.getWidth(), 1);

  g.setFont(mono(11.0f));
  g.setColour(muted);
  g.drawFittedText("click a string's number to lock onto it\n"
                   "string 1 is the thinnest",
                   footHintArea, juce::Justification::centredLeft, 2, 1.0f);
}
} // namespace evil::gui
