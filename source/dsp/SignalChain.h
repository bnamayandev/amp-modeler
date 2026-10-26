#pragma once

#include "CabSim.h"
#include "Equaliser.h"
#include "ModelManager.h"
#include "NoiseGate.h"
#include "Pedals.h"
#include "PitchShifter.h"
#include "ReverbPedal.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace evil {
/** Everything the plugin does to the guitar signal, independent of the plugin
   wrapper so tests and the CLI run exactly the same code:

        input gain -> noise gate -> transposer -> wah -> compressor -> fuzz
   -> overdrive -> distortion -> boost -> NAM model -> cab IR -> EQ -> phaser
   -> flanger -> chorus -> tremolo -> delay -> reverb -> output level

    Pedals before the amp sit where they would on a pedalboard; modulation and
   ambience go after the cab, like an effects loop.

    Mono. process() splits blocks larger than the prepared maximum, so a host
   that sends more samples than it announced can't overrun any internal buffer.
 */
class SignalChain {
public:
  struct Settings {
    float inputDb = 0.0f;
    bool gateOn = true;
    float gateThresholdDb = -65.0f;
    int transposeSemitones = 0;
    WahPedal::Settings wah;
    CompressorPedal::Settings compressor;
    FuzzPedal::Settings fuzz;
    OverdrivePedal::Settings overdrive;
    DistortionPedal::Settings distortion;
    BoostPedal::Settings boost;
    bool cabOn = true;
    Equaliser::Gains eqDb{};
    PhaserPedal::Settings phaser;
    FlangerPedal::Settings flanger;
    ChorusPedal::Settings chorus;
    TremoloPedal::Settings tremolo;
    DelayPedal::Settings delay;
    ReverbPedal::Settings reverb;
    float outputDb = 0.0f;
  };

  void prepare(double sampleRate, int maxBlockSize);
  void reset() noexcept;
  void process(float *data, int n, const Settings &settings) noexcept;

  /** Total latency of the model playing now. */
  int getLatencySamples() const noexcept {
    return models.getActiveLatencySamples() + CabSim::getLatencySamples();
  }

  ModelManager &getModels() noexcept { return models; }
  CabSim &getCab() noexcept { return cab; }
  bool isGateOpen() const noexcept { return gate.isOpen(); }
  float getCompressorReductionDb() const noexcept {
    return compressor.getGainReductionDb();
  }

private:
  void processChunk(float *data, int n, const Settings &settings) noexcept;

  int maxBlock = 0;
  bool primed =
      false; // snap gains to the first block's settings after prepare()
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
      inputGain, outputGain;
  NoiseGate gate;
  PitchShifter transposer;
  WahPedal wah;
  CompressorPedal compressor;
  FuzzPedal fuzz;
  OverdrivePedal overdrive;
  DistortionPedal distortion;
  BoostPedal boost;
  ModelManager models;
  CabSim cab;
  Equaliser eq;
  PhaserPedal phaser;
  FlangerPedal flanger;
  ChorusPedal chorus;
  TremoloPedal tremolo;
  DelayPedal delay;
  ReverbPedal reverb;
  std::array<FootswitchFade, 12> footswitches;
  std::vector<float> scratch; // dry copy while a footswitch crossfades
};
} // namespace evil
