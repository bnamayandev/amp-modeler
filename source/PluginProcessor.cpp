#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace evil {
namespace {
constexpr int bypassDelayCapacity = 1 << 14;
} // namespace

SharedLogFile::SharedLogFile()
    : logger(juce::FileLogger::createDefaultAppLogger(
          "EvilAmplifier", "EvilAmplifier.log",
          "Evil Amplifier " JucePlugin_VersionString)) {}

EvilAmplifierProcessor::EvilAmplifierProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::mono(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "EvilAmplifier", params::createLayout()),
      snapshot(state),
      presets(
          state,
          [this] {
            return PresetManager::Sources{getModels().getRequestedSource(),
                                          getCab().getSource()};
          },
          [this](const PresetManager::Sources &s) { applySources(s); }) {
  ModelManager::enableFastTanh(); // before any model is built
  juce::String error;
  if (!chain.getCab().load(params::defaultIrSource, error))
    log("Default IR failed to load: " + error);
  getModels().addChangeListener(this);
  getModels().requestLoad(params::defaultModelSource);
  startTimerHz(10);
}

EvilAmplifierProcessor::~EvilAmplifierProcessor() {
  stopTimer();
  getModels().removeChangeListener(this);
}

bool EvilAmplifierProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto in = layouts.getMainInputChannelSet();
  const auto out = layouts.getMainOutputChannelSet();
  const auto monoOrStereo = [](const juce::AudioChannelSet &s) {
    return s == juce::AudioChannelSet::mono() ||
           s == juce::AudioChannelSet::stereo();
  };
  return monoOrStereo(in) && monoOrStereo(out);
}

void EvilAmplifierProcessor::prepareToPlay(double sampleRate,
                                           int samplesPerBlock) {
  prepared = false;
  currentSampleRate = sampleRate;
  const int maxBlock = juce::jmax(1, samplesPerBlock);
  chain.prepare(sampleRate, maxBlock);

  bypassDelay.setSize(2, bypassDelayCapacity);
  bypassDelay.clear();
  bypassWrite = 0;

  const int latency = chain.computeLatencySamples(snapshot.read());
  setLatencySamples(latency);
  reportedLatency = latency;
  prepared = true;
}

void EvilAmplifierProcessor::releaseResources() { prepared = false; }

void EvilAmplifierProcessor::writeBypassHistory(
    const juce::AudioBuffer<float> &buffer, int startSample,
    int numSamples) noexcept {
  // Keeps the most recent input so engaging bypass outputs correctly delayed
  // audio at once.
  const int numIn =
      juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  if (numIn == 0 || bypassDelay.getNumSamples() != bypassDelayCapacity)
    return;
  for (int ch = 0; ch < 2; ++ch) {
    const auto *src = buffer.getReadPointer(ch < numIn ? ch : 0, startSample);
    auto *ring = bypassDelay.getWritePointer(ch);
    int w = bypassWrite;
    for (int i = 0; i < numSamples; ++i) {
      ring[w] = src[i];
      w = (w + 1) & (bypassDelayCapacity - 1);
    }
  }
  bypassWrite = (bypassWrite + numSamples) & (bypassDelayCapacity - 1);
}

void EvilAmplifierProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                          juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;
  const auto startTicks = juce::Time::getHighResolutionTicks();
  const int n = buffer.getNumSamples();
  const int numIn = getTotalNumInputChannels();
  const int numOut = getTotalNumOutputChannels();
  if (n == 0)
    return;
  if (numIn == 0 || buffer.getNumChannels() == 0 || !prepared) {
    buffer.clear();
    return;
  }

  writeBypassHistory(buffer, 0, n);

  // Guitar is mono: the chain runs on the first input channel and feeds every
  // output.
  auto *mono = buffer.getWritePointer(0);
  const float inPeak = buffer.getMagnitude(0, 0, n);
  if (inPeak > inputPeak.load(std::memory_order_relaxed))
    inputPeak.store(inPeak, std::memory_order_relaxed);

  chain.process(mono, n, snapshot.read());

  // Never pass non-finite samples to the host (or to someone's ears).
  for (int i = 0; i < n; ++i)
    if (!std::isfinite(mono[i])) {
      juce::FloatVectorOperations::clear(mono, n);
      break;
    }

  for (int ch = 1; ch < juce::jmin(numOut, buffer.getNumChannels()); ++ch)
    buffer.copyFrom(ch, 0, mono, n);

  const float outPeak = buffer.getMagnitude(0, 0, n);
  if (outPeak > outputPeak.load(std::memory_order_relaxed))
    outputPeak.store(outPeak, std::memory_order_relaxed);

  const auto elapsed = juce::Time::highResolutionTicksToSeconds(
      juce::Time::getHighResolutionTicks() - startTicks);
  cpu.record((float)(elapsed * currentSampleRate / n));
}

void EvilAmplifierProcessor::processBlockBypassed(
    juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
  const int n = buffer.getNumSamples();
  const int numIn = getTotalNumInputChannels();
  const int numOut =
      juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());
  if (n == 0)
    return;
  if (numIn == 0 || !prepared) {
    buffer.clear();
    return;
  }

  const int latency =
      juce::jlimit(0, bypassDelayCapacity / 2, reportedLatency.load());
  for (int start = 0; start < n; start += bypassDelayCapacity / 2) {
    const int count = juce::jmin(bypassDelayCapacity / 2, n - start);
    writeBypassHistory(buffer, start, count);
    const int readStart = bypassWrite - count - latency;
    for (int ch = 0; ch < numOut; ++ch) {
      const auto *ring = bypassDelay.getReadPointer(juce::jmin(ch, 1));
      auto *dst = buffer.getWritePointer(ch, start);
      for (int i = 0; i < count; ++i)
        dst[i] = ring[(readStart + i) & (bypassDelayCapacity - 1)];
    }
  }
}

juce::AudioProcessorEditor *EvilAmplifierProcessor::createEditor() {
  return new EvilAmplifierEditor(*this);
}

int EvilAmplifierProcessor::getNumPrograms() {
  return juce::jmax(1, presets.getNumFactoryPresets());
}

int EvilAmplifierProcessor::getCurrentProgram() {
  const auto index = presets.getCurrentIndex();
  return index < presets.getNumFactoryPresets() ? index : 0;
}

void EvilAmplifierProcessor::setCurrentProgram(int index) {
  if (juce::isPositiveAndBelow(index, presets.getNumFactoryPresets()))
    presets.load(index);
}

const juce::String EvilAmplifierProcessor::getProgramName(int index) {
  if (juce::isPositiveAndBelow(index, presets.getNumFactoryPresets()))
    return presets.getNames()[index];
  return {};
}

void EvilAmplifierProcessor::getStateInformation(juce::MemoryBlock &destData) {
  auto tree = state.copyState();
  tree.setProperty(params::modelSourceProperty,
                   getModels().getRequestedSource(), nullptr);
  tree.setProperty(params::irSourceProperty, getCab().getSource(), nullptr);
  if (const auto xml = tree.createXml())
    copyXmlToBinary(*xml, destData);
}

void EvilAmplifierProcessor::setStateInformation(const void *data,
                                                 int sizeInBytes) {
  const auto xml = getXmlFromBinary(data, sizeInBytes);
  if (xml == nullptr || !xml->hasTagName(state.state.getType()))
    return;

  const auto tree = juce::ValueTree::fromXml(*xml);
  state.replaceState(tree);

  // replaceState() skips parameters whose denormalised value is unchanged, so a
  // bool the host left at e.g. 0.85 ("on") would keep reporting 0.85 instead of
  // the restored 1.0. Push the exact restored values so host and plugin agree.
  for (auto *p : getParameters())
    if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p)) {
      const float restored = ranged->convertTo0to1(
          state.getRawParameterValue(ranged->getParameterID())->load());
      if (!juce::exactlyEqual(restored, ranged->getValue()))
        ranged->setValueNotifyingHost(restored);
    }

  // A missing property means "default"; an empty one means the user unloaded
  // it.
  PresetManager::Sources sources;
  sources.model = tree.hasProperty(params::modelSourceProperty)
                      ? tree.getProperty(params::modelSourceProperty).toString()
                      : juce::String(params::defaultModelSource);
  sources.ir = tree.hasProperty(params::irSourceProperty)
                   ? tree.getProperty(params::irSourceProperty).toString()
                   : juce::String(params::defaultIrSource);
  applySources(sources);
}

void EvilAmplifierProcessor::applySources(
    const PresetManager::Sources &sources) {
  if (sources.model.isEmpty()) {
    if (getModels().getRequestedSource().isNotEmpty())
      getModels().requestUnload();
  } else if (sources.model != getModels().getRequestedSource()) {
    getModels().requestLoad(sources.model);
  }

  if (sources.ir.isNotEmpty() && sources.ir != getCab().getSource()) {
    juce::String error;
    if (!loadIr(sources.ir, error))
      log("IR failed to load: " + error);
  }
}

bool EvilAmplifierProcessor::loadIr(const juce::String &source,
                                    juce::String &error) {
  const bool ok = getCab().load(source, error);
  {
    const juce::ScopedLock lock(irErrorLock);
    lastIrError = ok ? juce::String() : error;
  }
  if (ok)
    log("Loaded IR " + source);
  return ok;
}

juce::String EvilAmplifierProcessor::getLastIrError() const {
  const juce::ScopedLock lock(irErrorLock);
  return lastIrError;
}

void EvilAmplifierProcessor::loadModel(const juce::String &source) {
  getModels().requestLoad(source);
}

void EvilAmplifierProcessor::unloadModel() { getModels().requestUnload(); }

void EvilAmplifierProcessor::updateLatency() {
  const int latency = chain.getLatencySamples();
  if (latency != getLatencySamples()) {
    setLatencySamples(latency);
    log("Reported latency changed to " + juce::String(latency) + " samples");
  }
  reportedLatency = latency;
}

void EvilAmplifierProcessor::timerCallback() {
  getModels().collectGarbage();
  if (prepared)
    updateLatency();

  // Log CPU usage every 30 s while audio is running.
  if (++ticksSinceLog < 300)
    return;
  ticksSinceLog = 0;
  const auto blocks = cpu.blocks.load();
  if (blocks == blocksAtLastLog)
    return;
  blocksAtLastLog = blocks;
  const auto status = getModels().getStatus();
  log(juce::String::formatted(
          "CPU per block: average %.2f%%, peak %.2f%% of the deadline; %u "
          "overruns; %d Hz",
          100.0 * cpu.average.load(), 100.0 * cpu.peakForLog.exchange(0.0f),
          (unsigned)cpu.overruns.load(), (int)currentSampleRate) +
      (status.state == ModelManager::State::ready
           ? "; model " + status.info.name
           : juce::String()));
}

void EvilAmplifierProcessor::changeListenerCallback(juce::ChangeBroadcaster *) {
  const auto status = getModels().getStatus();
  if (status.state == ModelManager::State::ready &&
      status.info.source != lastLoggedModelSource) {
    lastLoggedModelSource = status.info.source;
    const auto &i = status.info;
    log("Loaded model '" + i.name + "' (" + i.architecture + ", " +
        juce::String(i.numWeights) + " weights, " +
        juce::String(i.sampleRate, 0) + " Hz" +
        (i.testEsr >= 0.0 ? ", held-out ESR " + juce::String(i.testEsr, 4)
                          : juce::String()) +
        ") from " + i.source);
  } else if (status.state == ModelManager::State::error) {
    log("Model failed to load: " + status.message);
  }
}

void EvilAmplifierProcessor::log(const juce::String &message) {
  if (logFile->logger != nullptr)
    logFile->logger->logMessage(message);
}
} // namespace evil

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new evil::EvilAmplifierProcessor();
}
