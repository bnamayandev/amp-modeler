#pragma once

#include "dsp/Metronome.h"
#include "dsp/SignalChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evil::params {
// Parameter IDs are part of saved state and host automation: never rename them.
inline constexpr const char *input = "input";
inline constexpr const char *gateOn = "gate_on";
inline constexpr const char *gateThreshold = "gate_threshold";
inline constexpr const char *ampMode = "amp_mode";
inline constexpr const char *drive = "drive";
inline constexpr const char *tone = "tone";
inline constexpr const char *oversampling = "oversampling";
inline constexpr const char *normalise = "normalise";
inline constexpr const char *cabOn = "cab_on";
// Graphic EQ bands, low to high (Equaliser::frequencies).
inline constexpr std::array<const char *, Equaliser::numBands> eqBands{
    "eq_50",  "eq_100", "eq_200", "eq_400",  "eq_800",
    "eq_1k6", "eq_3k2", "eq_6k4", "eq_12k8"};
inline constexpr const char *output = "output";

// The three-band EQ the nine bands replaced: only read, to upgrade old state.
inline constexpr std::array<const char *, 3> legacyEq{"bass", "mid", "treble"};

// Transposer and pedals.
inline constexpr const char *transpose = "transpose";
inline constexpr const char *compOn = "comp_on";
inline constexpr const char *compSustain = "comp_sustain";
inline constexpr const char *compBlend = "comp_blend";
inline constexpr const char *compLevel = "comp_level";
inline constexpr const char *fuzzOn = "fuzz_on";
inline constexpr const char *fuzzAmount = "fuzz_fuzz";
inline constexpr const char *fuzzTone = "fuzz_tone";
inline constexpr const char *fuzzLevel = "fuzz_level";
inline constexpr const char *odOn = "od_on";
inline constexpr const char *odDrive = "od_drive";
inline constexpr const char *odTone = "od_tone";
inline constexpr const char *odLevel = "od_level";
inline constexpr const char *chorusOn = "chorus_on";
inline constexpr const char *chorusRate = "chorus_rate";
inline constexpr const char *chorusDepth = "chorus_depth";
inline constexpr const char *chorusMix = "chorus_mix";
inline constexpr const char *delayOn = "delay_on";
inline constexpr const char *delayTime = "delay_time";
inline constexpr const char *delayFeedback = "delay_feedback";
inline constexpr const char *delayMix = "delay_mix";
inline constexpr const char *reverbOn = "reverb_on";
inline constexpr const char *reverbDecay = "reverb_decay";
inline constexpr const char *reverbTone = "reverb_tone";
inline constexpr const char *reverbMix = "reverb_mix";

// Metronome (saved with the session, not automatable).
inline constexpr const char *metronomeBpm = "metronome_bpm";
inline constexpr const char *metronomeBeats = "metronome_beats";
inline constexpr const char *metronomeLevel = "metronome_level";

/** Settings that belong to the player rather than the sound: presets neither
   store nor change them. */
bool isGlobal(const juce::String &parameterId);

/** A band's centre frequency as its fader is labelled, e.g. "400" or "1.6k". */
juce::String eqBandLabel(size_t band);

/** Replaces the bass/mid/treble of a preset or session saved before the
   nine-band EQ with the nearest band settings, in place. Each child element
   tagged `tag` holds one parameter, as "id" and "value" attributes. */
void upgradeLegacyEq(juce::XmlElement &saved, juce::StringRef tag);

// Non-parameter state saved alongside the parameters.
inline constexpr const char *modelSourceProperty = "modelSource";
inline constexpr const char *irSourceProperty = "irSource";
inline constexpr const char *defaultModelSource = "builtin:BlackstarHT1";
inline constexpr const char *defaultIrSource = "builtin:EvilCab4x12";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Cached raw parameter pointers, read lock-free on the audio thread. */
class Snapshot {
public:
  explicit Snapshot(juce::AudioProcessorValueTreeState &state);
  SignalChain::Settings read() const noexcept;
  Metronome::Settings readMetronome(bool running) const noexcept;

private:
  std::atomic<float> *input;
  std::atomic<float> *gateOn;
  std::atomic<float> *gateThreshold;
  std::atomic<float> *ampMode;
  std::atomic<float> *drive;
  std::atomic<float> *tone;
  std::atomic<float> *oversampling;
  std::atomic<float> *normalise;
  std::atomic<float> *cabOn;
  std::array<std::atomic<float> *, Equaliser::numBands> eq;
  std::atomic<float> *output;
  std::atomic<float> *transpose;
  std::atomic<float> *compOn, *compSustain, *compBlend, *compLevel;
  std::atomic<float> *fuzzOn, *fuzzAmount, *fuzzTone, *fuzzLevel;
  std::atomic<float> *odOn, *odDrive, *odTone, *odLevel;
  std::atomic<float> *chorusOn, *chorusRate, *chorusDepth, *chorusMix;
  std::atomic<float> *delayOn, *delayTime, *delayFeedback, *delayMix;
  std::atomic<float> *reverbOn, *reverbDecay, *reverbTone, *reverbMix;
  std::atomic<float> *metronomeBpm, *metronomeBeats, *metronomeLevel;
};
} // namespace evil::params
