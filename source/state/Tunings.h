#pragma once

#include <juce_events/juce_events.h>

#include <array>
#include <optional>
#include <vector>

namespace evil::tuning {
inline constexpr int numStrings = 6;
inline constexpr int lowestNote = 28, highestNote = 88; // E1 and E6, as MIDI
/** This close to the target counts as in tune. */
inline constexpr double inTuneCents = 3.0;

/** A pitch as a MIDI note, and how to spell it: 54 is both F#3 and Gb3. */
struct Note {
  int midi = 69;
  bool flat = false;
};

/** "Gb", without the octave. */
juce::String noteName(Note);
/** The octave as in scientific pitch: 69 is A4. */
int octaveOf(int midi);
double frequencyOf(int midi);
/** How far hz is above the note, in cents; negative if below. */
double centsFrom(double hz, int midi);

/** Every name a note can be picked by: "C", "C#", "Db", "D", ... */
const juce::StringArray &noteNames();

/** String 1, the thinnest, comes first. */
using Strings = std::array<Note, (std::size_t)numStrings>;
/** Scientific pitch, string 1 first: "E4 B3 G3 D3 A2 E2". */
juce::String toText(const Strings &);
/** nullopt unless it has exactly six notes that a string can be tuned to. */
std::optional<Strings> fromText(const juce::String &);

/** The note of this name a string (0 is string 1) would be tuned to: the one
    closest to its standard pitch, or the lower of the two if they're equally
    far. So string 1 and "D" is D4, and string 4 and "Gb" is Gb3. */
std::optional<Note> nearestNote(std::size_t string, const juce::String &name);

struct Tuning {
  juce::String name;
  Strings strings;
};
const std::vector<Tuning> &builtinTunings();

/** What the tuner makes of a pitch. */
struct Reading {
  double hz = 0.0;     // 0 if nothing is playing
  int string = -1;     // index into the tuning; -1 if there's nothing to tune
  Note target;         // that string's note
  double cents = 0.0;  // hz relative to the target; positive is sharp
  Note heard;          // the semitone closest to hz, whatever the tuning
  double heardCents = 0.0;

  bool isInTune() const noexcept;
};

/** lockedString is the string being tuned, or -1 to take the one whose note is
    closest to hz. An hz of 0 means silence. */
Reading read(double hz, const Strings &, int lockedString);

/** The tuning the tuner is set to, and the ones the player has saved. They
    belong to the player, not the sound, so presets and sessions don't touch
    them: they're in EvilAmplifier/tunings.xml beside the user presets, shared
    by every open editor through a SharedResourcePointer. Broadcasts a change
    when any of it changes. Message thread only. */
class TuningLibrary final : public juce::ChangeBroadcaster {
public:
  TuningLibrary();
  explicit TuningLibrary(juce::File file);

  static juce::File getDefaultFile();

  const Strings &getCurrent() const noexcept { return current; }
  void setCurrent(const Strings &);

  /** The built-in tunings, then the player's, sorted by name. */
  std::vector<Tuning> getAll() const;
  /** Where these notes are in getAll(), or -1. Spelling doesn't matter. */
  int indexOf(const Strings &) const;
  bool isUserTuning(int index) const noexcept;

  /** Replaces a saved tuning of the same name, ignoring case. */
  bool saveUserTuning(const juce::String &name, const Strings &,
                      juce::String &error);
  void deleteUserTuning(const juce::String &name);

private:
  void load();
  bool save() const;

  juce::File file;
  Strings current;
  std::vector<Tuning> user;
};
} // namespace evil::tuning
