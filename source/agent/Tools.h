#pragma once

#include "Provider.h"
#include "Tone3000.h"

#include <array>
#include <functional>
#include <optional>

namespace evil::agent {
struct ParameterInfo {
  juce::String id, name, label;
  juce::String text; // the value as the panel shows it
  float min = 0.0f, max = 1.0f, defaultValue = 0.0f, value = 0.0f;
  float position = 0.0f; // where the pointer is, 0 full left .. 1 full right
  bool isSwitch = false;
};

/** What the agent can read off the sound it has built, as there's no listening:
    its spectrum, how hard it clips and how much it compresses, as set and with
    every pedal and the EQ out of the way. */
struct ToneReading {
  juce::String error; // set if it couldn't be measured
  std::array<float, 9> bandDb{}, bandDbCaptureAlone{}; // the EQ's bands
  float levelDb = 0.0f, levelDbCaptureAlone = 0.0f;
  // How much louder the output gets for picking 12 dB harder: 12 is wide open.
  float touchDb = 0.0f, touchDbCaptureAlone = 0.0f;
  // Harmonic distortion of a single note, picked gently and hard.
  float distortionLightPercent = 0.0f, distortionHardPercent = 0.0f;
  float distortionLightCaptureAlone = 0.0f, distortionHardCaptureAlone = 0.0f;
};

struct LoadedCapture {
  juce::String name, source;
  std::optional<double> loudnessDb;
};

/** The amp as the agent sees it: the sound-shaping controls and what's loaded.
    Implemented over the real processor by ProcessorAmp, and faked in the tests. */
class AmpControl {
public:
  virtual ~AmpControl() = default;

  /** Leaves out what belongs to the player (transposer, metronome). */
  virtual std::vector<ParameterInfo> getParameters() const = 0;
  virtual bool setParameter(const juce::String &id, float value) = 0;
  /** The value that puts this control's pointer at a position, 0 to 1. */
  virtual float valueAtPosition(const juce::String &id, float position) const = 0;

  virtual LoadedCapture getModel() const = 0;
  virtual LoadedCapture getIr() const = 0;
  /** These block until the file is playing or has failed; empty means success. */
  virtual juce::String loadModel(const juce::String &source) = 0;
  virtual juce::String unloadModel() = 0;
  virtual juce::String loadIr(const juce::String &source) = 0;
  virtual juce::StringArray getBuiltinModels() const = 0;
  virtual juce::StringArray getBuiltinIrs() const = 0;

  /** For undo: the whole sound, as a preset holds it. */
  virtual std::unique_ptr<juce::XmlElement> captureState() = 0;
  virtual void restoreState(const juce::XmlElement &) = 0;

  /** Plays a test guitar through a copy of the amp as it's set. Takes a moment. */
  virtual ToneReading measureTone() = 0;
};

/** The agent's hands. There's deliberately no general fetch or file access: all
    it can do is move the amp's controls within limits and load files from
    TONE3000, by ID, because everything a model reads on the web is untrusted.

    A model's mistake or a malicious page can't reach further than a bad tone, and
    any turn can be undone. */
class ToolBox {
public:
  /** Worker-thread progress, shown in the chat as it happens. */
  using Note =
      std::function<void(const juce::String &, const std::vector<Source> &links)>;

  ToolBox(AmpControl &, Tone3000 &, Note, Tone3000::ShouldCancel);

  static std::vector<ToolSpec> specs();

  struct Outcome {
    ToolResult result;
    bool changedAmp = false;
  };
  /** A failure comes back as a result, for the model to read and work around. */
  Outcome execute(const ToolCall &);

  /** After a sign-in that went nowhere it stops asking and gets on without
      TONE3000; this lets it ask again. Worker thread. */
  void allowSignInPrompt() noexcept { signInPromptAllowed = true; }

  /** How long a turn waits on someone entering the code. */
  static constexpr int signInWaitMs = 2 * 60 * 1000;

  // --- the limits, public for the tests ---
  static constexpr float maxOutputDb = 6.0f;
  static constexpr float maxOutputRisePerCallDb = 6.0f;
  static constexpr float maxInputDb = 12.0f;
  /** How far the output is pulled down while a new capture swaps in. */
  static constexpr float swapDuckDb = 24.0f;
  /** Without loudness figures, stay this much below where it was. */
  static constexpr float unknownLoudnessTrimDb = -6.0f;

  /** A knob position from a clock face: "10:30", "noon", "3", "2 o'clock". Pedal
      knobs run from 7:00 full left to 5:00 full right. -1 if it isn't one. */
  static float positionFromClock(const juce::String &);
  static juce::String clockFromPosition(float position);

  /** The value the agent may actually set. why says what was changed, if so. */
  static float limitValue(const ParameterInfo &, float wanted, juce::String &why);
  /** Output change that keeps a new capture about as loud as the old one. */
  static float loudnessTrimDb(std::optional<double> from, std::optional<double> to);

private:
  Outcome setParameters(const ToolCall &);
  Outcome resetControls(const ToolCall &);
  Outcome measureTone(const ToolCall &);
  Outcome loadBuiltin(const ToolCall &);
  Outcome listTones(const ToolCall &);
  Outcome inspectTone(const ToolCall &);
  Outcome loadCapture(const ToolCall &);

  bool setOutput(float db);
  std::optional<ParameterInfo> findParameter(const juce::String &id) const;
  /** Asks for the sign-in code if need be, once. Empty if ready. */
  juce::String ensureConnected();
  /** Swaps a model in without a jump in level; empty on success. */
  juce::String swapModel(const std::function<juce::String()> &load);

  AmpControl &amp;
  Tone3000 &tone3000;
  Note note;
  Tone3000::ShouldCancel shouldCancel;
  bool signInPromptAllowed = true;
};

/** A cleaned-up reply for the chat, which doesn't render Markdown. */
juce::String tidyForDisplay(const juce::String &modelText);

/** The system prompt, including every control and what its knob does at each
    position, read from the amp itself so it can't go out of date. */
juce::String buildSystemPrompt(const AmpControl &);

/** The amp right now, attached to each message so nothing goes stale when the
    player turns a knob: what's loaded, and every control not at its default. */
juce::String describeAmpState(AmpControl &);
} // namespace evil::agent
