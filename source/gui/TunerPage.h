#pragma once

#include "dsp/PitchDetector.h"
#include "state/Tunings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace evil::gui {
/** The tuner page: the meter on the left, and on the right the tuning it tunes
    to, one note per string, with the built-in and saved tunings to start from.

    It works out the string being played, or a click on a string's number locks
    it to that one, for when a string is too far off to be told apart. */
class TunerPage final : public juce::Component, private juce::ChangeListener {
public:
  TunerPage();
  ~TunerPage() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  /** Feed samples as they arrive, then call refresh() at the GUI rate. */
  void push(const float *data, int n) noexcept { detector.push(data, n); }
  void refresh(double sampleRate, float secondsSinceLast);
  /** Forgets what it heard, e.g. when the page comes back into view. */
  void reset();

  /** Shows these notes without saving them, for evil-snapshot. */
  void showStrings(const tuning::Strings &);

private:
  struct StringRow {
    juce::TextButton number;
    juce::ComboBox noteBox;
    juce::TextButton octaveDown{"-"}, octaveUp{"+"};
    juce::Rectangle<int> bounds, octave, hz;
  };

  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  void setNote(std::size_t string, tuning::Note note);
  void lockString(int string);
  void updateReading(double hz);
  void refreshControls();
  void refreshTuningList();
  void saveDialog();
  void paintMeter(juce::Graphics &);
  void paintGauge(juce::Graphics &);
  void paintStats(juce::Graphics &);
  void paintStrings(juce::Graphics &);

  juce::SharedResourcePointer<tuning::TuningLibrary> library;
  PitchDetector detector;
  tuning::Strings strings;
  tuning::Reading reading;
  int lockedString = -1;
  float needle = 0.0f; // cents, eased towards the reading
  float secondsSinceAnalysis = 0.0f;
  bool analyseNext = true;

  juce::ComboBox tuningBox;
  juce::TextButton saveButton{"save"}, deleteButton{"delete"},
      autoButton{"auto"};
  std::array<StringRow, (std::size_t)tuning::numStrings> rows;

  juce::Rectangle<int> meterPanel, stringsPanel, captionArea, noteArea,
      hintArea, gaugeArea, statsArea, headerArea, footHintArea;
};
} // namespace evil::gui
