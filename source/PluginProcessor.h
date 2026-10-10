#pragma once

#include "dsp/SignalChain.h"
#include "state/Parameters.h"
#include "state/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace evil {
/** Per-block processing time as a fraction of the block's real-time deadline.
   Written by the audio thread, read by the GUI and the log. */
struct CpuStats {
  std::atomic<float> average{0.0f}; // exponential moving average
  std::atomic<float> peakForDisplay{
      0.0f};                           // worst block since the GUI last looked
  std::atomic<float> peakForLog{0.0f}; // worst block since the last log line
  std::atomic<std::uint32_t> overruns{
      0}; // blocks that took longer than their deadline
  std::atomic<std::uint64_t> blocks{0};

  void record(float load) noexcept {
    const auto avg = average.load(std::memory_order_relaxed);
    average.store(avg + 0.02f * (load - avg), std::memory_order_relaxed);
    for (auto *peak : {&peakForDisplay, &peakForLog})
      if (load > peak->load(std::memory_order_relaxed))
        peak->store(load, std::memory_order_relaxed);
    if (load > 1.0f)
      overruns.fetch_add(1, std::memory_order_relaxed);
    blocks.fetch_add(1, std::memory_order_relaxed);
  }
};

/** One log file shared by every instance in the process. */
struct SharedLogFile {
  SharedLogFile();
  std::unique_ptr<juce::FileLogger> logger;
};

class EvilAmplifierProcessor final : public juce::AudioProcessor,
                                     private juce::Timer,
                                     private juce::ChangeListener {
public:
  EvilAmplifierProcessor();
  ~EvilAmplifierProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  void processBlockBypassed(juce::AudioBuffer<float> &,
                            juce::MidiBuffer &) override;
  using AudioProcessor::processBlock;
  using AudioProcessor::processBlockBypassed;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return CabSim::maxIrSeconds; }

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // --- for the editor (message thread) ---
  juce::AudioProcessorValueTreeState &getState() noexcept { return state; }
  PresetManager &getPresets() noexcept { return presets; }
  ModelManager &getModels() noexcept { return chain.getModels(); }
  CabSim &getCab() noexcept { return chain.getCab(); }
  CpuStats &getCpuStats() noexcept { return cpu; }
  bool loadIr(const juce::String &source, juce::String &error);
  void loadModel(const juce::String &source);
  void unloadModel();

  float takeInputPeak() noexcept {
    return inputPeak.exchange(0.0f, std::memory_order_relaxed);
  }
  float takeOutputPeak() noexcept {
    return outputPeak.exchange(0.0f, std::memory_order_relaxed);
  }
  bool isGateOpen() const noexcept { return chain.isGateOpen(); }
  juce::String getLastIrError() const;

private:
  void timerCallback() override;
  void writeBypassHistory(const juce::AudioBuffer<float> &buffer,
                          int startSample, int numSamples) noexcept;
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  void applySources(const PresetManager::Sources &sources);
  void updateLatency();
  void log(const juce::String &message);

  juce::AudioProcessorValueTreeState state;
  params::Snapshot snapshot;
  SignalChain chain;
  PresetManager presets;
  CpuStats cpu;
  std::atomic<float> inputPeak{0.0f}, outputPeak{0.0f};

  double currentSampleRate = 0.0;
  std::atomic<bool> prepared{false};

  // Bypass keeps the processed path's latency so toggling bypass doesn't shift
  // timing.
  juce::AudioBuffer<float> bypassDelay;
  int bypassWrite = 0;
  std::atomic<int> reportedLatency{0};

  juce::SharedResourcePointer<SharedLogFile> logFile;
  juce::CriticalSection irErrorLock;
  juce::String lastIrError;
  juce::String lastLoggedModelSource;
  std::uint64_t blocksAtLastLog = 0;
  int ticksSinceLog = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvilAmplifierProcessor)
};
} // namespace evil
