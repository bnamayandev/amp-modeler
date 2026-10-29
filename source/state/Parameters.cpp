#include "Parameters.h"

namespace evil::params {
namespace {
using Float = juce::AudioParameterFloat;
using Bool = juce::AudioParameterBool;
using Choice = juce::AudioParameterChoice;

juce::AudioParameterFloatAttributes decibels() {
  return juce::AudioParameterFloatAttributes()
      .withLabel("dB")
      .withStringFromValueFunction(
          [](float v, int) { return juce::String(v, 1) + " dB"; });
}

juce::NormalisableRange<float> range(float lo, float hi, float centre) {
  juce::NormalisableRange<float> r(lo, hi, 0.01f);
  r.setSkewForCentre(centre);
  return r;
}

/** Bipolar +/-maxDb with a square-law (parabolic) taper: fine control around
   0 dB, where small cuts and boosts live, and the extremes still reachable. */
juce::NormalisableRange<float> parabolic(float maxDb) {
  return {-maxDb, maxDb,
          [](float lo, float hi, float x) {
            const float u = 2.0f * x - 1.0f;
            return juce::jmap(u * std::abs(u), -1.0f, 1.0f, lo, hi);
          },
          [](float lo, float hi, float v) {
            const float u = juce::jlimit(-1.0f, 1.0f,
                                         juce::jmap(v, lo, hi, -1.0f, 1.0f));
            return 0.5f + 0.5f * std::copysign(std::sqrt(std::abs(u)), u);
          },
          [](float lo, float hi, float v) { // 0.01 dB steps
            return juce::jlimit(lo, hi, std::round(v * 100.0f) / 100.0f);
          }};
}

/** Unitless pedal knob shown 0-10. */
juce::AudioParameterFloatAttributes zeroToTen() {
  return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
      [](float v, int) { return juce::String(v * 10.0f, 1); });
}

juce::AudioParameterFloatAttributes hertz() {
  return juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(
      [](float v, int) { return juce::String(v, 2) + " Hz"; });
}

juce::AudioParameterFloatAttributes percent() {
  return juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
      [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
}

void addUnit(juce::AudioProcessorValueTreeState::ParameterLayout &layout,
             const char *id, const juce::String &name, float defaultValue,
             juce::AudioParameterFloatAttributes attributes = zeroToTen()) {
  layout.add(std::make_unique<Float>(
      juce::ParameterID{id, 1}, name,
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), defaultValue,
      std::move(attributes)));
}

void addLevel(juce::AudioProcessorValueTreeState::ParameterLayout &layout,
              const char *id, const juce::String &name) {
  layout.add(std::make_unique<Float>(
      juce::ParameterID{id, 1}, name,
      juce::NormalisableRange<float>(-12.0f, 12.0f, 0.01f), 0.0f, decibels()));
}

std::atomic<float> *raw(juce::AudioProcessorValueTreeState &s, const char *id) {
  auto *p = s.getRawParameterValue(id);
  jassert(p != nullptr);
  return p;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  layout.add(std::make_unique<Float>(
      juce::ParameterID{input, 1}, "Input",
      juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f, decibels()));
  layout.add(
      std::make_unique<Bool>(juce::ParameterID{gateOn, 1}, "Gate", true));
  layout.add(std::make_unique<Float>(
      juce::ParameterID{gateThreshold, 1}, "Gate Threshold",
      juce::NormalisableRange<float>(-100.0f, -20.0f, 0.1f), -65.0f,
      decibels()));
  layout.add(
      std::make_unique<Bool>(juce::ParameterID{cabOn, 1}, "Cabinet", true));
  for (size_t b = 0; b < eqBands.size(); ++b)
    layout.add(std::make_unique<Float>(
        juce::ParameterID{eqBands[b], 1}, "EQ " + eqBandLabel(b) + "Hz",
        parabolic(Equaliser::maxGainDb), 0.0f, decibels()));
  layout.add(std::make_unique<Float>(juce::ParameterID{output, 1}, "Output",
                                     range(-40.0f, 12.0f, 0.0f), 0.0f,
                                     decibels()));

  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{transpose, 1}, "Transpose",
      -PitchShifter::maxSemitones, PitchShifter::maxSemitones, 0,
      juce::AudioParameterIntAttributes().withLabel("st").withStringFromValueFunction(
          [](int v, int) {
            return (v > 0 ? "+" : "") + juce::String(v) + " st";
          })));

  layout.add(std::make_unique<Bool>(juce::ParameterID{wahOn, 1}, "Wah", false));
  addUnit(layout, wahPosition, "Wah Pedal", 0.5f);
  addUnit(layout, wahAuto, "Wah Auto", 0.0f);
  addUnit(layout, wahResonance, "Wah Resonance", 0.5f);

  layout.add(std::make_unique<Bool>(juce::ParameterID{compOn, 1}, "Compressor", false));
  addUnit(layout, compSustain, "Comp Sustain", 0.5f);
  addUnit(layout, compBlend, "Comp Blend", 1.0f, percent());
  addLevel(layout, compLevel, "Comp Level");

  layout.add(std::make_unique<Bool>(juce::ParameterID{fuzzOn, 1}, "Fuzz", false));
  addUnit(layout, fuzzAmount, "Fuzz", 0.7f);
  addUnit(layout, fuzzTone, "Fuzz Tone", 0.5f);
  addLevel(layout, fuzzLevel, "Fuzz Level");

  layout.add(std::make_unique<Bool>(juce::ParameterID{odOn, 1}, "Overdrive Pedal", false));
  addUnit(layout, odDrive, "OD Drive", 0.5f);
  addUnit(layout, odTone, "OD Tone", 0.5f);
  addLevel(layout, odLevel, "OD Level");

  layout.add(std::make_unique<Bool>(juce::ParameterID{distOn, 1}, "Distortion", false));
  addUnit(layout, distAmount, "Distortion", 0.6f);
  addUnit(layout, distTone, "Dist Tone", 0.5f);
  addLevel(layout, distLevel, "Dist Level");

  layout.add(std::make_unique<Bool>(juce::ParameterID{boostOn, 1}, "Boost", false));
  layout.add(std::make_unique<Float>(
      juce::ParameterID{boostGain, 1}, "Boost Gain",
      juce::NormalisableRange<float>(0.0f, 20.0f, 0.01f), 9.0f, decibels()));
  addLevel(layout, boostBass, "Boost Bass");
  addLevel(layout, boostTreble, "Boost Treble");

  layout.add(std::make_unique<Bool>(juce::ParameterID{phaserOn, 1}, "Phaser", false));
  layout.add(std::make_unique<Float>(juce::ParameterID{phaserRate, 1}, "Phaser Rate",
                                     range(0.05f, 8.0f, 0.8f), 0.5f, hertz()));
  addUnit(layout, phaserDepth, "Phaser Depth", 0.7f);
  addUnit(layout, phaserFeedback, "Phaser Feedback", 0.3f);

  layout.add(std::make_unique<Bool>(juce::ParameterID{flangerOn, 1}, "Flanger", false));
  layout.add(std::make_unique<Float>(juce::ParameterID{flangerRate, 1}, "Flanger Rate",
                                     range(0.05f, 5.0f, 0.5f), 0.25f, hertz()));
  addUnit(layout, flangerDepth, "Flanger Depth", 0.7f);
  addUnit(layout, flangerFeedback, "Flanger Feedback", 0.5f);

  layout.add(std::make_unique<Bool>(juce::ParameterID{chorusOn, 1}, "Chorus", false));
  layout.add(std::make_unique<Float>(juce::ParameterID{chorusRate, 1}, "Chorus Rate",
                                     range(0.1f, 8.0f, 1.0f), 0.8f, hertz()));
  addUnit(layout, chorusDepth, "Chorus Depth", 0.5f);
  addUnit(layout, chorusMix, "Chorus Mix", 0.5f, percent());

  layout.add(std::make_unique<Bool>(juce::ParameterID{tremOn, 1}, "Tremolo", false));
  layout.add(std::make_unique<Float>(juce::ParameterID{tremRate, 1}, "Tremolo Rate",
                                     range(0.5f, 15.0f, 4.0f), 5.0f, hertz()));
  addUnit(layout, tremDepth, "Tremolo Depth", 0.6f, percent());
  addUnit(layout, tremShape, "Tremolo Shape", 0.3f);

  layout.add(std::make_unique<Bool>(juce::ParameterID{delayOn, 1}, "Delay", false));
  layout.add(std::make_unique<Float>(
      juce::ParameterID{delayTime, 1}, "Delay Time",
      range(20.0f, DelayPedal::maxTimeMs, 400.0f), 380.0f,
      juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; })));
  layout.add(std::make_unique<Float>(
      juce::ParameterID{delayFeedback, 1}, "Delay Feedback",
      juce::NormalisableRange<float>(0.0f, 0.95f, 0.001f), 0.35f, percent()));
  addUnit(layout, delayMix, "Delay Mix", 0.35f, percent());

  layout.add(std::make_unique<Bool>(juce::ParameterID{reverbOn, 1}, "Reverb", false));
  addUnit(layout, reverbDecay, "Reverb Decay", 0.5f);
  addUnit(layout, reverbTone, "Reverb Tone", 0.6f);
  addUnit(layout, reverbMix, "Reverb Mix", 0.3f, percent());

  const auto notAutomatable = [] {
    return juce::AudioParameterFloatAttributes().withAutomatable(false);
  };
  layout.add(std::make_unique<Float>(
      juce::ParameterID{metronomeBpm, 1}, "Metronome Tempo",
      juce::NormalisableRange<float>(Metronome::minBpm, Metronome::maxBpm, 1.0f), 120.0f,
      notAutomatable().withLabel("bpm").withStringFromValueFunction(
          [](float v, int) { return juce::String(juce::roundToInt(v)) + " bpm"; })));
  layout.add(std::make_unique<Choice>(
      juce::ParameterID{metronomeBeats, 1}, "Metronome Signature",
      juce::StringArray{"2/4", "3/4", "4/4", "5/4", "6/4", "7/4"}, 2,
      juce::AudioParameterChoiceAttributes().withAutomatable(false)));
  layout.add(std::make_unique<Float>(
      juce::ParameterID{metronomeLevel, 1}, "Metronome Level",
      juce::NormalisableRange<float>(-40.0f, 0.0f, 0.1f), -8.0f,
      notAutomatable().withLabel("dB").withStringFromValueFunction(
          [](float v, int) { return juce::String(v, 1) + " dB"; })));
  return layout;
}

bool isGlobal(const juce::String &id) {
  return id == transpose || id == metronomeBpm || id == metronomeBeats ||
         id == metronomeLevel;
}

juce::String eqBandLabel(size_t band) {
  const auto hz = Equaliser::frequencies[band];
  return hz < 1000.0 ? juce::String(juce::roundToInt(hz))
                     : juce::String(hz / 1000.0, 1) + "k";
}

void upgradeLegacyEq(juce::XmlElement &saved, juce::StringRef tag) {
  std::array<float, legacyEq.size()> legacy{};
  juce::Array<juce::XmlElement *> legacyElements;
  bool hasBands = false;
  for (auto *e : saved.getChildWithTagNameIterator(tag)) {
    const auto id = e->getStringAttribute("id");
    for (size_t i = 0; i < legacyEq.size(); ++i)
      if (id == legacyEq[i]) {
        legacy[i] = (float)e->getDoubleAttribute("value");
        legacyElements.add(e);
      }
    for (const auto *band : eqBands)
      hasBands = hasBands || id == band;
  }
  if (legacyElements.isEmpty())
    return;

  for (auto *e : legacyElements)
    saved.removeChildElement(e, true);
  if (hasBands)
    return;
  const auto gains = Equaliser::fromLegacy(legacy[0], legacy[1], legacy[2]);
  for (size_t b = 0; b < eqBands.size(); ++b) {
    auto *e = saved.createNewChildElement(tag);
    e->setAttribute("id", eqBands[b]);
    e->setAttribute("value", gains[b]);
  }
}

Snapshot::Snapshot(juce::AudioProcessorValueTreeState &s)
    : input(raw(s, params::input)), gateOn(raw(s, params::gateOn)),
      gateThreshold(raw(s, params::gateThreshold)),
      cabOn(raw(s, params::cabOn)),
      output(raw(s, params::output)),
      transpose(raw(s, params::transpose)), wahOn(raw(s, params::wahOn)),
      wahPosition(raw(s, params::wahPosition)), wahAuto(raw(s, params::wahAuto)),
      wahResonance(raw(s, params::wahResonance)), compOn(raw(s, params::compOn)),
      compSustain(raw(s, params::compSustain)),
      compBlend(raw(s, params::compBlend)),
      compLevel(raw(s, params::compLevel)), fuzzOn(raw(s, params::fuzzOn)),
      fuzzAmount(raw(s, params::fuzzAmount)),
      fuzzTone(raw(s, params::fuzzTone)), fuzzLevel(raw(s, params::fuzzLevel)),
      odOn(raw(s, params::odOn)), odDrive(raw(s, params::odDrive)),
      odTone(raw(s, params::odTone)), odLevel(raw(s, params::odLevel)),
      distOn(raw(s, params::distOn)), distAmount(raw(s, params::distAmount)),
      distTone(raw(s, params::distTone)), distLevel(raw(s, params::distLevel)),
      boostOn(raw(s, params::boostOn)), boostGain(raw(s, params::boostGain)),
      boostBass(raw(s, params::boostBass)),
      boostTreble(raw(s, params::boostTreble)),
      phaserOn(raw(s, params::phaserOn)),
      phaserRate(raw(s, params::phaserRate)),
      phaserDepth(raw(s, params::phaserDepth)),
      phaserFeedback(raw(s, params::phaserFeedback)),
      flangerOn(raw(s, params::flangerOn)),
      flangerRate(raw(s, params::flangerRate)),
      flangerDepth(raw(s, params::flangerDepth)),
      flangerFeedback(raw(s, params::flangerFeedback)),
      chorusOn(raw(s, params::chorusOn)),
      chorusRate(raw(s, params::chorusRate)),
      chorusDepth(raw(s, params::chorusDepth)),
      chorusMix(raw(s, params::chorusMix)), tremOn(raw(s, params::tremOn)),
      tremRate(raw(s, params::tremRate)), tremDepth(raw(s, params::tremDepth)),
      tremShape(raw(s, params::tremShape)), delayOn(raw(s, params::delayOn)),
      delayTime(raw(s, params::delayTime)),
      delayFeedback(raw(s, params::delayFeedback)),
      delayMix(raw(s, params::delayMix)), reverbOn(raw(s, params::reverbOn)),
      reverbDecay(raw(s, params::reverbDecay)),
      reverbTone(raw(s, params::reverbTone)),
      reverbMix(raw(s, params::reverbMix)),
      metronomeBpm(raw(s, params::metronomeBpm)),
      metronomeBeats(raw(s, params::metronomeBeats)),
      metronomeLevel(raw(s, params::metronomeLevel)) {
  for (size_t b = 0; b < eq.size(); ++b)
    eq[b] = raw(s, eqBands[b]);
}

SignalChain::Settings Snapshot::read() const noexcept {
  const auto get = [](const std::atomic<float> *p) {
    return p->load(std::memory_order_relaxed);
  };
  SignalChain::Settings s;
  s.inputDb = get(input);
  s.gateOn = get(gateOn) >= 0.5f;
  s.gateThresholdDb = get(gateThreshold);
  s.cabOn = get(cabOn) >= 0.5f;
  for (size_t b = 0; b < eq.size(); ++b)
    s.eqDb[b] = get(eq[b]);
  s.outputDb = get(output);

  const auto on = [&](const std::atomic<float> *p) { return get(p) >= 0.5f; };
  s.transposeSemitones = juce::roundToInt(get(transpose));
  s.wah = {on(wahOn), get(wahPosition), get(wahAuto), get(wahResonance)};
  s.compressor = {on(compOn), get(compSustain), get(compBlend), get(compLevel)};
  s.fuzz = {on(fuzzOn), get(fuzzAmount), get(fuzzTone), get(fuzzLevel)};
  s.overdrive = {on(odOn), get(odDrive), get(odTone), get(odLevel)};
  s.distortion = {on(distOn), get(distAmount), get(distTone), get(distLevel)};
  s.boost = {on(boostOn), get(boostGain), get(boostBass), get(boostTreble)};
  s.phaser = {on(phaserOn), get(phaserRate), get(phaserDepth), get(phaserFeedback)};
  s.flanger = {on(flangerOn), get(flangerRate), get(flangerDepth),
               get(flangerFeedback)};
  s.chorus = {on(chorusOn), get(chorusRate), get(chorusDepth), get(chorusMix)};
  s.tremolo = {on(tremOn), get(tremRate), get(tremDepth), get(tremShape)};
  s.delay = {on(delayOn), get(delayTime), get(delayFeedback), get(delayMix)};
  s.reverb = {on(reverbOn), get(reverbDecay), get(reverbTone), get(reverbMix)};
  return s;
}

Metronome::Settings Snapshot::readMetronome(bool running) const noexcept {
  Metronome::Settings m;
  m.running = running;
  m.bpm = metronomeBpm->load(std::memory_order_relaxed);
  m.beatsPerBar =
      2 + juce::roundToInt(metronomeBeats->load(std::memory_order_relaxed));
  m.levelDb = metronomeLevel->load(std::memory_order_relaxed);
  return m;
}
} // namespace evil::params
