#include "TestHelpers.h"
#include "dsp/PitchDetector.h"
#include "state/Tunings.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace evil;
using namespace evil::test;
using namespace evil::tuning;

namespace {
constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

/** A plucked string: partials that die away, the higher ones sooner, and run a
    little sharp as a real string's do. A neck pickup can make the second
    partial louder than the fundamental, so its level is a parameter. */
std::vector<float> pluck(double hz, double rate, int n,
                         double secondPartial = 0.5, double stiffness = 2.0e-5) {
  std::vector<float> x((size_t)n, 0.0f);
  for (int k = 1; k <= 12; ++k) {
    const double partial = hz * k * std::sqrt(1.0 + stiffness * k * k);
    if (partial > 0.45 * rate)
      break;
    const double level = k == 2 ? secondPartial : 1.0 / k;
    const double decay = 1.0 + 0.5 * k;
    for (int i = 0; i < n; ++i) {
      const double t = i / rate;
      x[(size_t)i] += (float)(0.1 * level * std::exp(-decay * t) *
                              std::sin(twoPi * partial * t));
    }
  }
  return x;
}

/** Runs the signal through in blocks; the pitch at the end of each, skipping
    the attack. Silent frames come back as 0. */
std::vector<double> framesOf(const std::vector<float> &x, double rate,
                             double skipSeconds = 0.15) {
  PitchDetector detector;
  detector.prepare(rate);
  std::vector<double> frames;
  const int hop = (int)(rate / 15.0);
  for (int start = 0; start + hop <= (int)x.size(); start += hop) {
    detector.push(x.data() + start, hop);
    if (start >= (int)(skipSeconds * rate))
      frames.push_back(detector.detect());
  }
  return frames;
}

double worstCents(const std::vector<double> &frames, double expectedHz) {
  double worst = 0.0;
  for (double hz : frames)
    worst = std::max(worst, hz > 0.0 ? std::abs(1200.0 * std::log2(hz / expectedHz))
                                     : 100000.0);
  return worst;
}

Strings strings(const char *text) { return *fromText(text); }
} // namespace

TEST_CASE("The pitch detector reads a pure tone to within a cent, over the "
          "whole range",
          "[tuner]") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
  for (int midi = lowestNote; midi <= highestNote; midi += 3) {
    const double hz = frequencyOf(midi);
    const auto x = sine(hz, rate, (int)(rate * 0.8), 0.2f);
    const auto frames = framesOf(x, rate, 0.2);
    CAPTURE(rate, midi, hz);
    REQUIRE_FALSE(frames.empty());
    CHECK(worstCents(frames, hz) < 1.0);
  }
}

TEST_CASE("The pitch detector tells a few cents apart", "[tuner]") {
  const double hz = frequencyOf(45) * std::pow(2.0, 4.0 / 1200.0);
  const auto frames = framesOf(sine(hz, 48000.0, 24000, 0.2f), 48000.0);
  REQUIRE_FALSE(frames.empty());
  for (double f : frames)
    CHECK(centsFrom(f, 45) == Catch::Approx(4.0).margin(0.25));
}

TEST_CASE("The pitch detector follows a plucked string without octave errors, "
          "even with a weak fundamental",
          "[tuner]") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  const double secondPartial = GENERATE(0.5, 3.0);
  for (int midi : {33, 36, 38, 40, 45, 50, 54, 55, 59, 62, 64, 76}) {
    const double hz = frequencyOf(midi);
    const auto frames =
        framesOf(pluck(hz, rate, (int)(rate * 1.2), secondPartial), rate);
    CAPTURE(rate, secondPartial, midi);
    REQUIRE_FALSE(frames.empty());
    CHECK(worstCents(frames, hz) < 3.0);
  }
}

TEST_CASE("A stiff wound string, whose partials run well sharp, still reads as "
          "its note",
          "[tuner]") {
  constexpr double rate = 48000.0;
  for (int midi : {33, 38, 40, 45, 50}) {
    const double hz = frequencyOf(midi);
    const auto frames = framesOf(pluck(hz, rate, 57600, 3.0, 2.0e-4), rate);
    CAPTURE(midi);
    REQUIRE_FALSE(frames.empty());
    CHECK(worstCents(frames, hz) < 10.0);
  }
}

TEST_CASE("The pitch detector stays quiet without a note", "[tuner]") {
  constexpr double rate = 48000.0;
  constexpr int n = 24000;

  SECTION("silence") {
    for (double hz : framesOf(std::vector<float>(n, 0.0f), rate))
      CHECK(hz == 0.0);
  }
  SECTION("mains hum at the level of a quiet pickup") {
    for (double hz : framesOf(sine(60.0, rate, n, 0.0005f), rate))
      CHECK(hz == 0.0);
  }
  SECTION("noise") {
    juce::Random random(5);
    std::vector<float> x(n);
    for (auto &s : x)
      s = 0.2f * (random.nextFloat() - 0.5f);
    for (double hz : framesOf(x, rate))
      CHECK(hz == 0.0);
  }
  SECTION("before it has heard enough") {
    PitchDetector detector;
    CHECK(detector.detect() == 0.0);
    const auto x = sine(110.0, rate, 200);
    detector.push(x.data(), 200); // not prepared: ignored
    detector.prepare(rate);
    detector.push(x.data(), 200);
    CHECK(detector.detect() == 0.0);
  }
}

TEST_CASE("The pitch detector survives non-finite input", "[tuner]") {
  constexpr double rate = 48000.0;
  auto x = sine(110.0, rate, 24000, 0.2f);
  x[100] = std::numeric_limits<float>::quiet_NaN();
  x[101] = std::numeric_limits<float>::infinity();
  const auto frames = framesOf(x, rate);
  REQUIRE_FALSE(frames.empty());
  CHECK(worstCents(frames, 110.0) < 1.0);
}

TEST_CASE("Tracking rides out a wrong frame, holds a dying note, then lets go",
          "[tuner]") {
  constexpr double rate = 48000.0;
  constexpr int hop = 3200; // 15 Hz
  constexpr double seconds = hop / rate;
  PitchDetector detector;
  detector.prepare(rate);
  const auto play = [&](const std::vector<float> &x) {
    double hz = 0.0;
    for (int start = 0; start + hop <= (int)x.size(); start += hop) {
      detector.push(x.data() + start, hop);
      hz = detector.track(seconds);
    }
    return hz;
  };

  CHECK(centsFrom(play(sine(110.0, rate, hop * 12, 0.2f)), 45) ==
        Catch::Approx(0.0).margin(1.0));

  // One frame's worth of something else, and back: never strays.
  const auto burst = sine(440.0, rate, hop, 0.2f);
  detector.push(burst.data(), hop);
  CHECK(centsFrom(detector.track(seconds), 45) ==
        Catch::Approx(0.0).margin(1.0));
  CHECK(centsFrom(play(sine(110.0, rate, hop * 6, 0.2f)), 45) ==
        Catch::Approx(0.0).margin(1.0));

  // A new note takes over.
  CHECK(centsFrom(play(sine(196.0, rate, hop * 8, 0.2f)), 55) ==
        Catch::Approx(0.0).margin(1.0));

  // Silence: still there for a moment, then gone.
  const std::vector<float> silence((size_t)hop * 3, 0.0f);
  CHECK(play(silence) > 0.0);
  const std::vector<float> longSilence((size_t)hop * 12, 0.0f);
  CHECK(play(longSilence) == 0.0);
}

TEST_CASE("Notes are named, spelled and octaved like a guitarist would",
          "[tuner]") {
  CHECK(frequencyOf(69) == Catch::Approx(440.0));
  CHECK(frequencyOf(40) == Catch::Approx(82.407).margin(0.001));
  CHECK(centsFrom(440.0 * std::pow(2.0, 7.0 / 1200.0), 69) ==
        Catch::Approx(7.0));
  CHECK(noteName({54, true}) == "Gb");
  CHECK(noteName({54, false}) == "F#");
  CHECK(noteName({64, true}) == "E");
  CHECK(octaveOf(64) == 4);
  CHECK(octaveOf(40) == 2);
  CHECK(noteNames().size() == 17);
  CHECK(noteNames().contains("Gb"));
  CHECK(noteNames().contains("F#"));
}

TEST_CASE("Picking a note name takes the octave closest to the string's "
          "standard pitch",
          "[tuner]") {
  // The examples from the feature's request.
  const auto string1 = nearestNote(0, "D");
  REQUIRE(string1);
  CHECK(string1->midi == 62); // D4, two below E4
  const auto string4 = nearestNote(3, "Gb");
  REQUIRE(string4);
  CHECK(string4->midi == 54); // Gb3, four above D3
  CHECK(string4->flat);
  CHECK(noteName(*string4) == "Gb");

  CHECK(nearestNote(5, "D")->midi == 38); // Drop D on the low string
  CHECK(nearestNote(5, "E")->midi == 40);
  CHECK(nearestNote(0, "E")->midi == 64);
  CHECK(nearestNote(5, "Bb")->midi == 34); // a tritone either way: goes down
  CHECK(nearestNote(5, "A#")->midi == 34);
  CHECK_FALSE(nearestNote(5, "H"));
  CHECK_FALSE(nearestNote(6, "E"));

  // Every name on every string stays in reach of the octave buttons.
  for (std::size_t s = 0; s < (std::size_t)numStrings; ++s)
    for (const auto &name : noteNames()) {
      const auto note = nearestNote(s, name);
      REQUIRE(note);
      CHECK(noteName(*note) == name);
      CHECK(note->midi >= lowestNote);
      CHECK(note->midi <= highestNote);
    }
}

TEST_CASE("Tunings read and write as text, keeping the spelling", "[tuner]") {
  const auto s = strings("D4 B3 G3 Gb3 A2 E2");
  CHECK(s[0].midi == 62);
  CHECK(s[3].midi == 54);
  CHECK(s[3].flat);
  CHECK(toText(s) == "D4 B3 G3 Gb3 A2 E2");
  CHECK(toText(strings("D4 B3 G3 F#3 A2 E2")) == "D4 B3 G3 F#3 A2 E2");

  for (const auto *bad :
       {"", "E4 B3 G3 D3 A2", "E4 B3 G3 D3 A2 E2 E1", "E4 B3 G3 D3 A2 H2",
        "E4 B3 G3 D3 A2 E", "E4 B3 G3 D3 A2 2", "E4 B3 G3 D3 A2 E0",
        "E4 B3 G3 D3 A2 F6", "E4 B3 G3 D3 A2 E-2", "E4 B3 G3 D3 A2 E2x"})
    CHECK_FALSE(fromText(bad));

  for (const auto &tuning : builtinTunings()) {
    CAPTURE(tuning.name);
    const auto again = fromText(toText(tuning.strings));
    REQUIRE(again);
    CHECK(toText(*again) == toText(tuning.strings));
  }
}

TEST_CASE("The built-in tunings are the ones they say they are", "[tuner]") {
  const auto &all = builtinTunings();
  REQUIRE(all.front().name == "Standard");
  const std::array<double, 6> standard{329.63, 246.94, 196.00,
                                       146.83, 110.00, 82.41};
  for (std::size_t i = 0; i < standard.size(); ++i)
    CHECK(frequencyOf(all.front().strings[i].midi) ==
          Catch::Approx(standard[i]).margin(0.01));
  const auto dropD = std::find_if(all.begin(), all.end(), [](const Tuning &t) {
    return t.name == "Drop D";
  });
  REQUIRE(dropD != all.end());
  CHECK(frequencyOf(dropD->strings[5].midi) ==
        Catch::Approx(73.42).margin(0.01));
}

TEST_CASE("A reading finds the closest string, or sticks to a locked one",
          "[tuner]") {
  const auto s = strings("D4 B3 G3 Gb3 A2 E2");

  SECTION("nothing playing") {
    const auto idle = read(0.0, s, -1);
    CHECK(idle.string == -1);
    CHECK_FALSE(idle.isInTune());
    const auto waiting = read(0.0, s, 3);
    CHECK(waiting.string == 3);
    CHECK(waiting.target.midi == 54);
    CHECK_FALSE(waiting.isInTune());
  }
  SECTION("auto picks the nearest target, reading flat as negative") {
    const auto r = read(frequencyOf(54) * std::pow(2.0, -20.0 / 1200.0), s, -1);
    CHECK(r.string == 3);
    CHECK(noteName(r.target) == "Gb");
    CHECK(r.cents == Catch::Approx(-20.0));
    CHECK_FALSE(r.isInTune());
    CHECK(r.heard.midi == 54);
    CHECK(noteName(r.heard) == "Gb"); // spelled as the target is
  }
  SECTION("a locked string reads however far off the note is") {
    const auto r = read(frequencyOf(64), s, 0); // still E4, wanted D4
    CHECK(r.string == 0);
    CHECK(r.cents == Catch::Approx(200.0));
    CHECK(r.heard.midi == 64);
    CHECK(r.heardCents == Catch::Approx(0.0).margin(1.0e-6));
  }
  SECTION("in tune means within the tolerance") {
    CHECK(read(frequencyOf(62) * std::pow(2.0, 2.5 / 1200.0), s, -1).isInTune());
    CHECK_FALSE(
        read(frequencyOf(62) * std::pow(2.0, 3.5 / 1200.0), s, -1).isInTune());
  }
}

TEST_CASE("Custom tunings persist, replace by name and leave built-ins alone",
          "[tuner]") {
  const auto file = juce::File::createTempFile("evil-tunings.xml");
  file.deleteFile();
  const auto custom = strings("D4 B3 G3 Gb3 A2 E2");
  const auto numBuiltin = (int)builtinTunings().size();
  juce::String error;

  {
    TuningLibrary library(file);
    CHECK(toText(library.getCurrent()) == "E4 B3 G3 D3 A2 E2"); // the default
    CHECK(library.indexOf(library.getCurrent()) == 0);
    CHECK(library.indexOf(custom) == -1);

    library.setCurrent(custom);
    CHECK(library.saveUserTuning("  my tuning ", custom, error));
    CHECK(library.saveUserTuning("Another", strings("E4 B3 G3 D3 A2 Eb2"), error));
    CHECK(library.getAll().size() == (size_t)numBuiltin + 2);
    CHECK(library.getAll()[(size_t)numBuiltin].name == "Another"); // sorted
    CHECK(library.getAll()[(size_t)numBuiltin + 1].name == "my tuning");
    CHECK(library.indexOf(custom) == numBuiltin + 1);
    CHECK(library.isUserTuning(numBuiltin + 1));
    CHECK_FALSE(library.isUserTuning(0));
  }
  {
    TuningLibrary reopened(file);
    CHECK(toText(reopened.getCurrent()) == "D4 B3 G3 Gb3 A2 E2");
    CHECK(reopened.getAll().size() == (size_t)numBuiltin + 2);

    // Same name, any case: replaces.
    CHECK(reopened.saveUserTuning("MY TUNING", strings("D4 A3 G3 Gb3 A2 E2"),
                                  error));
    CHECK(reopened.getAll().size() == (size_t)numBuiltin + 2);
    CHECK(reopened.indexOf(custom) == -1);

    // Built-ins can't be overwritten, or nameless tunings saved.
    CHECK_FALSE(reopened.saveUserTuning("drop d", custom, error));
    CHECK(error.contains("built-in"));
    CHECK_FALSE(reopened.saveUserTuning("   ", custom, error));
    CHECK(reopened.getAll().size() == (size_t)numBuiltin + 2);

    reopened.deleteUserTuning("another");
    CHECK(reopened.getAll().size() == (size_t)numBuiltin + 1);
  }
  {
    TuningLibrary reopened(file);
    CHECK(reopened.getAll().size() == (size_t)numBuiltin + 1);
    CHECK(reopened.getAll()[(size_t)numBuiltin].name == "MY TUNING");
  }
  file.deleteFile();
}

TEST_CASE("A damaged tunings file costs only what's damaged", "[tuner]") {
  const auto file = juce::File::createTempFile("evil-tunings.xml");
  const auto numBuiltin = builtinTunings().size();

  SECTION("not XML at all") {
    file.replaceWithText("not xml <<<");
    TuningLibrary library(file);
    CHECK(library.getAll().size() == numBuiltin);
    CHECK(library.indexOf(library.getCurrent()) == 0);
  }
  SECTION("bad entries among good ones") {
    file.replaceWithText("<Tunings current=\"E4 B3 nonsense\">"
                         "<Tuning name=\"bad\" strings=\"H9\"/>"
                         "<Tuning strings=\"E4 B3 G3 D3 A2 D2\"/>"
                         "<Tuning name=\"good\" strings=\"D4 B3 G3 D3 A2 D2\"/>"
                         "</Tunings>");
    TuningLibrary library(file);
    CHECK(library.indexOf(library.getCurrent()) == 0);
    REQUIRE(library.getAll().size() == numBuiltin + 1);
    CHECK(library.getAll().back().name == "good");
  }
  file.deleteFile();
}
