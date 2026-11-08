#pragma once

#include "ProcessorAmp.h"
#include "dsp/SignalChain.h"
#include "state/Parameters.h"

namespace evil::agent {
/** The plugin's parameters, chain and presets with no audio or editor attached,
    so evil-cli and the tests can run the agent against the real amp code. */
class HeadlessRig final : public juce::AudioProcessor {
public:
  HeadlessRig()
      : AudioProcessor(BusesProperties().withOutput(
            "Output", juce::AudioChannelSet::stereo(), true)),
        state(*this, nullptr, "EvilAmplifier", params::createLayout()),
        presets(
            state,
            [this] {
              return PresetManager::Sources{chain.getModels().getRequestedSource(),
                                            chain.getCab().getSource()};
            },
            [this](const PresetManager::Sources &s) { apply(s); }),
        amp(state, chain.getModels(), chain.getCab(), presets,
            [this](const juce::String &source, juce::String &error) {
              return chain.getCab().load(source, error);
            }) {
    chain.setMatchingLevels(true); // as the plugin starts
    chain.prepare(48000.0, 128);
    juce::String error;
    chain.getCab().load(params::defaultIrSource, error);
  }

  ~HeadlessRig() override = default;

  juce::AudioProcessorValueTreeState state;
  SignalChain chain;
  PresetManager presets;
  ProcessorAmp amp;

  const juce::String getName() const override { return "HeadlessRig"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

private:
  void apply(const PresetManager::Sources &s) {
    if (s.model.isEmpty())
      chain.getModels().requestUnload();
    else if (s.model != chain.getModels().getRequestedSource())
      chain.getModels().requestLoad(s.model);
    juce::String error;
    if (s.ir.isNotEmpty() && s.ir != chain.getCab().getSource())
      chain.getCab().load(s.ir, error);
  }
};
} // namespace evil::agent
