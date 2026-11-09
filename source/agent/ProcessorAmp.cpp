#include "ProcessorAmp.h"

#include "ToneMeter.h"
#include "dsp/Loudness.h"
#include "state/Parameters.h"

#include <juce_events/juce_events.h>

#include <mutex>
#include <optional>

namespace evil::agent {
namespace {
/** Runs fn on the message thread and waits for it. Gives up if this thread is
    told to exit, so a plugin closing while the agent works can't deadlock: the
    message thread waits for the agent in ~Session, and if the agent were waiting
    for the message thread, neither would ever move. Returns whether fn ran. */
bool onMessageThread(const std::function<void()> &fn) {
  if (juce::MessageManager::existsAndIsCurrentThread()) {
    fn();
    return true;
  }

  struct Call {
    std::mutex mutex;
    bool abandoned = false;
    std::function<void()> fn;
    juce::WaitableEvent done;
  };
  auto call = std::make_shared<Call>();
  call->fn = fn;
  juce::MessageManager::callAsync([call] {
    std::lock_guard lock(call->mutex);
    if (!call->abandoned)
      call->fn();
    call->done.signal();
  });

  while (!call->done.wait(50))
    if (juce::Thread::currentThreadShouldExit()) {
      std::lock_guard lock(call->mutex);
      if (call->done.wait(0))
        return true;
      call->abandoned = true; // what fn captured is about to go away
      return false;
    }
  return true;
}

constexpr auto closing = "the amp is closing";
} // namespace

ProcessorAmp::ProcessorAmp(juce::AudioProcessorValueTreeState &s, ModelManager &m,
                           CabSim &c, PresetManager &p, LoadIr load)
    : state(s), models(m), cab(c), presets(p), loadIrHook(std::move(load)) {}

std::vector<ParameterInfo> ProcessorAmp::getParameters() const {
  std::vector<ParameterInfo> all;
  for (auto *p : state.processor.getParameters()) {
    const auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(p);
    if (ranged == nullptr || params::isGlobal(ranged->getParameterID()))
      continue;
    ParameterInfo info;
    info.id = ranged->getParameterID();
    info.name = ranged->getName(64);
    info.label = ranged->getLabel();
    info.text = ranged->getCurrentValueAsText();
    const auto range = ranged->getNormalisableRange();
    info.min = range.start;
    info.max = range.end;
    info.defaultValue = ranged->convertFrom0to1(ranged->getDefaultValue());
    info.position = ranged->getValue();
    info.value = ranged->convertFrom0to1(info.position);
    info.isSwitch = ranged->isBoolean();
    all.push_back(std::move(info));
  }
  return all;
}

bool ProcessorAmp::setParameter(const juce::String &id, float value) {
  if (params::isGlobal(id))
    return false;
  auto *parameter = state.getParameter(id);
  if (parameter == nullptr)
    return false;
  return onMessageThread([parameter, value] {
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    parameter->endChangeGesture();
  });
}

float ProcessorAmp::valueAtPosition(const juce::String &id, float position) const {
  const auto *parameter = state.getParameter(id);
  return parameter != nullptr
             ? parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, position))
             : 0.0f;
}

ToneReading ProcessorAmp::measureTone() {
  // The audio thread reads these same atomics; nothing here touches its chain.
  return evil::agent::measureTone(params::Snapshot(state).read(),
                                  models.getRequestedSource(), cab.getSource(),
                                  models.isMatchingLevels());
}

LoadedCapture ProcessorAmp::getModel() const {
  LoadedCapture capture;
  const auto status = models.getStatus();
  if (status.state != ModelManager::State::ready)
    return capture;
  capture.name = status.info.name;
  capture.source = status.info.source;
  capture.gearType = status.info.gearType;
  if (models.isMatchingLevels() && status.info.hasLevel)
    capture.loudnessDb = loudness::targetDb; // every capture plays at the same level
  else if (status.info.hasLoudness)
    capture.loudnessDb = status.info.loudnessDb;
  return capture;
}

LoadedCapture ProcessorAmp::getIr() const {
  return {cab.getDisplayName(), cab.getSource(), {}, {}};
}

juce::String ProcessorAmp::loadModel(const juce::String &source) {
  if (!onMessageThread([this, &source] { models.requestLoad(source); }))
    return closing;

  // Building and prewarming a network takes a moment, on ModelManager's worker.
  const auto deadline = juce::Time::getMillisecondCounter() + 60000u;
  while (juce::Time::getMillisecondCounter() < deadline) {
    if (juce::Thread::currentThreadShouldExit())
      return closing;
    if (models.getRequestedSource() != source)
      return "you picked another model while this one was loading";
    const auto status = models.getStatus();
    if (status.state == ModelManager::State::ready)
      return {};
    if (status.state == ModelManager::State::error)
      return status.message;
    juce::Thread::sleep(10);
  }
  return "the model took too long to load";
}

juce::String ProcessorAmp::unloadModel() {
  return onMessageThread([this] { models.requestUnload(); }) ? juce::String()
                                                             : juce::String(closing);
}

juce::String ProcessorAmp::loadIr(const juce::String &source) {
  juce::String error;
  bool ok = false;
  if (!onMessageThread([&] { ok = loadIrHook(source, error); }))
    return closing;
  return ok ? juce::String() : error;
}

juce::StringArray ProcessorAmp::getBuiltinModels() const {
  return ModelManager::getBuiltinNames();
}

juce::StringArray ProcessorAmp::getBuiltinIrs() const {
  return CabSim::getBuiltinNames();
}

std::unique_ptr<juce::XmlElement> ProcessorAmp::captureState() {
  std::unique_ptr<juce::XmlElement> xml;
  onMessageThread([this, &xml] { xml = presets.capture("agent undo"); });
  return xml != nullptr ? std::move(xml)
                        : std::make_unique<juce::XmlElement>("Preset");
}

void ProcessorAmp::restoreState(const juce::XmlElement &saved) {
  // The controls change at once but the old model takes a moment to load. If
  // the output went straight back, a hot capture that had been trimmed down
  // would play at the higher level until then. So hold it down until it's swapped.
  juce::XmlElement ducked(saved);
  std::optional<float> savedOutput;
  for (auto *e : ducked.getChildWithTagNameIterator("Param"))
    if (e->getStringAttribute("id") == params::output) {
      savedOutput = (float)e->getDoubleAttribute("value");
      const auto now = state.getRawParameterValue(params::output)->load();
      e->setAttribute("value", (double)(juce::jmin(now, *savedOutput) - ToolBox::swapDuckDb));
    }
  if (!onMessageThread([this, &ducked] { presets.apply(ducked); }))
    return;

  const auto deadline = juce::Time::getMillisecondCounter() + 10000u;
  while (models.getStatus().state == ModelManager::State::loading &&
         juce::Time::getMillisecondCounter() < deadline &&
         !juce::Thread::currentThreadShouldExit())
    juce::Thread::sleep(10);
  if (savedOutput.has_value())
    setParameter(params::output, *savedOutput);
}
} // namespace evil::agent
