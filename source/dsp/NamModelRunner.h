#pragma once

#include "StreamingResampler.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace nam {
class DSP;
}

namespace evil {
/** What the GUI and logs show about a loaded model. */
struct ModelInfo {
  juce::String source; // "builtin:<name>" or an absolute file path
  juce::String name;
  juce::String architecture;
  juce::String gear; // "make model", if the metadata has it
  double sampleRate = 48000.0;
  bool hasLoudness = false;
  double loudnessDb = 0.0;
  double testEsr =
      -1.0; // held-out ESR from the training metadata; < 0 if unknown
  double validationEsr = -1.0;
  int numWeights = 0;
};

/** Runs one NAM model at the rate it was trained at, from a host running at any
   rate.

    When the host rate differs from the model's (and the model can't adapt
   itself), audio goes host -> StreamingResampler -> model -> StreamingResampler
   -> host, with a small fixed FIFO delay that guarantees exactly one output per
   input every block. Feeding a 48 kHz model 44.1 kHz audio unconverted would
   shift every resonance it learned by 9%.

    prepare() allocates and prewarms; process() is real-time safe. */
class NamModelRunner {
public:
  NamModelRunner(std::unique_ptr<nam::DSP> model, ModelInfo info);
  ~NamModelRunner();

  void prepare(double hostSampleRate, int maxHostBlockSize);
  bool isPreparedFor(double hostSampleRate,
                     int maxHostBlockSize) const noexcept;

  /** n must not exceed the prepared maximum block size. in and out may alias.
   */
  void process(const float *in, float *out, int n) noexcept;

  int getLatencySamples() const noexcept { return latency; }
  bool isResampling() const noexcept { return resampling; }

  const ModelInfo &getInfo() const noexcept { return info; }

private:
  void runModel(float *in, float *out, int n) noexcept;

  std::unique_ptr<nam::DSP> model;
  ModelInfo info;
  double modelRate = 48000.0;

  double preparedHostRate = 0.0;
  int preparedBlockSize = 0;
  bool resampling = false;
  int latency = 0;

  std::vector<float> inputCopy;
  StreamingResampler toModelRate, toHostRate;
  int maxModelBlock = 0;
  std::vector<float> modelIn, modelOut;
  std::vector<float> pending; // host-rate output not yet handed out
  int pendingCount = 0;
};
} // namespace evil
