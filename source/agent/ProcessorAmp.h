#pragma once

#include "Tools.h"
#include "dsp/CabSim.h"
#include "dsp/ModelManager.h"
#include "state/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace evil::agent {
/** The real amp behind the agent's tools. The agent's worker thread calls this;
    anything that touches parameters or sources hops to the message thread, like
    a knob turned by hand, and nothing here goes near the audio thread. */
class ProcessorAmp final : public AmpControl {
public:
  using LoadIr = std::function<bool(const juce::String &, juce::String &error)>;

  ProcessorAmp(juce::AudioProcessorValueTreeState &, ModelManager &, CabSim &,
               PresetManager &, LoadIr);

  std::vector<ParameterInfo> getParameters() const override;
  bool setParameter(const juce::String &id, float value) override;
  float valueAtPosition(const juce::String &id, float position) const override;
  ToneReading measureTone() override;
  LoadedCapture getModel() const override;
  LoadedCapture getIr() const override;
  juce::String loadModel(const juce::String &source) override;
  juce::String unloadModel() override;
  juce::String loadIr(const juce::String &source) override;
  juce::StringArray getBuiltinModels() const override;
  juce::StringArray getBuiltinIrs() const override;
  std::unique_ptr<juce::XmlElement> captureState() override;
  void restoreState(const juce::XmlElement &) override;

private:
  juce::AudioProcessorValueTreeState &state;
  ModelManager &models;
  CabSim &cab;
  PresetManager &presets;
  LoadIr loadIrHook;
};
} // namespace evil::agent
