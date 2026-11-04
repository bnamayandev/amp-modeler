#include "Tunings.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace evil::tuning {
namespace {
constexpr std::array<const char *, 12> sharpNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<const char *, 12> flatNames{
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
constexpr std::array<int, (std::size_t)numStrings> standardPitches{64, 59, 55,
                                                                   50, 45, 40};

constexpr const char *rootTag = "Tunings";
constexpr const char *tuningTag = "Tuning";
constexpr const char *currentAttribute = "current";
constexpr const char *nameAttribute = "name";
constexpr const char *stringsAttribute = "strings";

/** The pitch class and spelling of a name such as "Gb", in a Note's fields. */
std::optional<Note> parseName(const juce::String &name) {
  for (std::size_t pitch = 0; pitch < 12; ++pitch) {
    if (name == sharpNames[pitch])
      return Note{(int)pitch, false};
    if (name == flatNames[pitch])
      return Note{(int)pitch, true};
  }
  return std::nullopt;
}

std::optional<Note> parseNote(const juce::String &text) {
  const auto digits = text.indexOfAnyOf("0123456789");
  if (digits <= 0)
    return std::nullopt;
  const auto octave = text.substring(digits);
  const auto spelled = parseName(text.substring(0, digits));
  if (!spelled || !octave.containsOnly("0123456789") || octave.length() > 2)
    return std::nullopt;
  const int midi = (octave.getIntValue() + 1) * 12 + spelled->midi;
  if (midi < lowestNote || midi > highestNote)
    return std::nullopt;
  return Note{midi, spelled->flat};
}

juce::String noteText(Note note) {
  return noteName(note) + juce::String(octaveOf(note.midi));
}

bool sameNotes(const Strings &a, const Strings &b) {
  return std::equal(a.begin(), a.end(), b.begin(), [](Note x, Note y) {
    return x.midi == y.midi;
  });
}
} // namespace

juce::String noteName(Note note) {
  const auto pitch = (std::size_t)(note.midi % 12);
  return note.flat ? flatNames[pitch] : sharpNames[pitch];
}

int octaveOf(int midi) { return midi / 12 - 1; }

double frequencyOf(int midi) {
  return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

double centsFrom(double hz, int midi) {
  return 1200.0 * std::log2(hz / frequencyOf(midi));
}

const juce::StringArray &noteNames() {
  static const juce::StringArray names = [] {
    juce::StringArray list;
    for (std::size_t pitch = 0; pitch < 12; ++pitch) {
      list.add(sharpNames[pitch]);
      list.addIfNotAlreadyThere(flatNames[pitch]);
    }
    return list;
  }();
  return names;
}

juce::String toText(const Strings &strings) {
  juce::StringArray notes;
  for (const auto &note : strings)
    notes.add(noteText(note));
  return notes.joinIntoString(" ");
}

std::optional<Strings> fromText(const juce::String &text) {
  const auto notes = juce::StringArray::fromTokens(text.trim(), " ", {});
  if (notes.size() != numStrings)
    return std::nullopt;
  Strings strings;
  for (int i = 0; i < numStrings; ++i) {
    const auto note = parseNote(notes[i]);
    if (!note)
      return std::nullopt;
    strings[(std::size_t)i] = *note;
  }
  return strings;
}

std::optional<Note> nearestNote(std::size_t string, const juce::String &name) {
  const auto spelled = parseName(name);
  if (!spelled || string >= standardPitches.size())
    return std::nullopt;
  const int standard = standardPitches[string];
  const int up = (((spelled->midi - standard) % 12) + 12) % 12;
  return Note{standard + (up <= 5 ? up : up - 12), spelled->flat};
}

const std::vector<Tuning> &builtinTunings() {
  static const std::vector<Tuning> tunings = [] {
    const auto tuning = [](const char *name, const char *notes) {
      return Tuning{name, *fromText(notes)};
    };
    return std::vector<Tuning>{
        tuning("Standard", "E4 B3 G3 D3 A2 E2"),
        tuning("Half step down", "Eb4 Bb3 Gb3 Db3 Ab2 Eb2"),
        tuning("Whole step down", "D4 A3 F3 C3 G2 D2"),
        tuning("Drop D", "E4 B3 G3 D3 A2 D2"),
        tuning("Drop C#", "D#4 A#3 F#3 C#3 G#2 C#2"),
        tuning("Drop C", "D4 A3 F3 C3 G2 C2"),
        tuning("DADGAD", "D4 A3 G3 D3 A2 D2"),
        tuning("Open D", "D4 A3 F#3 D3 A2 D2"),
        tuning("Open E", "E4 B3 G#3 E3 B2 E2"),
        tuning("Open G", "D4 B3 G3 D3 G2 D2"),
    };
  }();
  return tunings;
}

bool Reading::isInTune() const noexcept {
  return hz > 0.0 && std::abs(cents) <= inTuneCents;
}

Reading read(double hz, const Strings &strings, int lockedString) {
  Reading r;
  r.hz = hz;
  r.string = lockedString;
  if (hz > 0.0 && r.string < 0) {
    auto closest = std::numeric_limits<double>::max();
    for (int s = 0; s < numStrings; ++s) {
      const auto off = std::abs(centsFrom(hz, strings[(std::size_t)s].midi));
      if (off < closest) {
        closest = off;
        r.string = s;
      }
    }
  }
  if (r.string >= 0)
    r.target = strings[(std::size_t)r.string];
  if (hz > 0.0) {
    r.cents = centsFrom(hz, r.target.midi);
    r.heard.midi = juce::roundToInt(69.0 + centsFrom(hz, 69) / 100.0);
    r.heard.flat = r.target.flat;
    r.heardCents = centsFrom(hz, r.heard.midi);
  }
  return r;
}

TuningLibrary::TuningLibrary() : TuningLibrary(getDefaultFile()) {}

TuningLibrary::TuningLibrary(juce::File f)
    : file(std::move(f)), current(builtinTunings().front().strings) {
  load();
}

juce::File TuningLibrary::getDefaultFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("EvilAmplifier")
      .getChildFile("tunings.xml");
}

void TuningLibrary::load() {
  const auto xml = juce::parseXML(file);
  if (xml == nullptr || !xml->hasTagName(rootTag))
    return;
  if (const auto strings = fromText(xml->getStringAttribute(currentAttribute)))
    current = *strings;
  for (auto *e : xml->getChildWithTagNameIterator(tuningTag)) {
    const auto name = e->getStringAttribute(nameAttribute);
    const auto strings = fromText(e->getStringAttribute(stringsAttribute));
    if (name.isNotEmpty() && strings)
      user.push_back({name, *strings});
  }
}

bool TuningLibrary::save() const {
  juce::XmlElement xml(rootTag);
  xml.setAttribute(currentAttribute, toText(current));
  for (const auto &tuning : user) {
    auto *e = xml.createNewChildElement(tuningTag);
    e->setAttribute(nameAttribute, tuning.name);
    e->setAttribute(stringsAttribute, toText(tuning.strings));
  }
  return file.getParentDirectory().createDirectory() && xml.writeTo(file);
}

void TuningLibrary::setCurrent(const Strings &strings) {
  current = strings;
  save();
  sendChangeMessage();
}

std::vector<Tuning> TuningLibrary::getAll() const {
  auto all = builtinTunings();
  all.insert(all.end(), user.begin(), user.end());
  return all;
}

int TuningLibrary::indexOf(const Strings &strings) const {
  const auto all = getAll();
  for (std::size_t i = 0; i < all.size(); ++i)
    if (sameNotes(all[i].strings, strings))
      return (int)i;
  return -1;
}

bool TuningLibrary::isUserTuning(int index) const noexcept {
  const int firstUser = (int)builtinTunings().size();
  return index >= firstUser && index < firstUser + (int)user.size();
}

bool TuningLibrary::saveUserTuning(const juce::String &rawName,
                                   const Strings &strings,
                                   juce::String &error) {
  const auto name = rawName.trim();
  if (name.isEmpty()) {
    error = "Enter a name for the tuning";
    return false;
  }
  for (const auto &builtin : builtinTunings())
    if (builtin.name.equalsIgnoreCase(name)) {
      error = builtin.name + " is a built-in tuning: pick another name";
      return false;
    }

  const auto before = user;
  const auto sameName = [&name](const Tuning &t) {
    return t.name.equalsIgnoreCase(name);
  };
  user.erase(std::remove_if(user.begin(), user.end(), sameName), user.end());
  user.push_back({name, strings});
  std::sort(user.begin(), user.end(), [](const Tuning &a, const Tuning &b) {
    return a.name.compareIgnoreCase(b.name) < 0;
  });
  if (!save()) {
    user = before;
    error = "Could not write " + file.getFullPathName();
    return false;
  }
  sendChangeMessage();
  return true;
}

void TuningLibrary::deleteUserTuning(const juce::String &name) {
  user.erase(std::remove_if(user.begin(), user.end(),
                            [&name](const Tuning &t) {
                              return t.name.equalsIgnoreCase(name);
                            }),
             user.end());
  save();
  sendChangeMessage();
}
} // namespace evil::tuning
