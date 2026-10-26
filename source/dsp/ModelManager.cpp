#include "ModelManager.h"

#include "EvilBinaryData.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <NAM/get_dsp.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace evil {
namespace {
struct BuiltinModel {
  const char *id;       // the part after "builtin:"
  const char *resource; // juce_add_binary_data's name for the embedded file
};

constexpr BuiltinModel builtinModels[] = {
    {"BlackstarHT1", "BlackstarHT1_nam"},
};

juce::String jsonString(const nlohmann::json &object, const char *key) {
  if (object.is_object())
    if (const auto it = object.find(key); it != object.end() && it->is_string())
      return juce::String::fromUTF8(it->get<std::string>().c_str());
  return {};
}

double jsonNumber(const nlohmann::json &object, const char *key,
                  double fallback) {
  if (object.is_object())
    if (const auto it = object.find(key); it != object.end() && it->is_number())
      return it->get<double>();
  return fallback;
}
} // namespace

NamModelRunner *ModelManager::unloadToken() noexcept {
  // A distinct address that is never dereferenced.
  static char token;
  return reinterpret_cast<NamModelRunner *>(&token);
}

ModelManager::ModelManager() {
  worker = std::thread([this] { workerLoop(); });
}

ModelManager::~ModelManager() {
  {
    std::lock_guard lock(requestMutex);
    quit = true;
  }
  requestCondition.notify_all();
  worker.join();

  if (auto *p = pending.exchange(nullptr); p != nullptr && p != unloadToken())
    delete p;
  delete active;
  delete fadingOut;
  collectGarbage();
}

void ModelManager::enableFastTanh() {
  static std::once_flag once;
  std::call_once(once,
                 [] { nam::activations::Activation::enable_fast_tanh(); });
}

juce::StringArray ModelManager::getBuiltinNames() {
  juce::StringArray names;
  for (const auto &m : builtinModels)
    names.add(m.id);
  return names;
}

ModelManager::LoadResult ModelManager::buildRunner(const juce::String &source) {
  LoadResult result;
  std::string text;
  juce::String fallbackName;

  if (source.startsWith(builtinPrefix)) {
    const auto id = source.fromFirstOccurrenceOf(builtinPrefix, false, false);
    const auto *entry =
        std::find_if(std::begin(builtinModels), std::end(builtinModels),
                     [&](const BuiltinModel &m) { return id == m.id; });
    int size = 0;
    const char *data =
        entry != std::end(builtinModels)
            ? EvilBinaryData::getNamedResource(entry->resource, size)
            : nullptr;
    if (data == nullptr) {
      result.error = "Unknown built-in model: " + id;
      return result;
    }
    text.assign(data, (size_t)size);
    fallbackName = id;
  } else {
    const juce::File file(source);
    if (!file.existsAsFile()) {
      result.error = "Model file not found: " + source;
      return result;
    }
    if (file.getSize() > 256 * 1024 * 1024) {
      result.error = "Model file is too large: " + file.getFileName();
      return result;
    }
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) {
      result.error = "Could not read " + file.getFullPathName();
      return result;
    }
    text.assign(static_cast<const char *>(bytes.getData()), bytes.getSize());
    fallbackName = file.getFileNameWithoutExtension();
  }

  try {
    const auto json = nlohmann::json::parse(text);
    nam::dspData data;
    auto dsp = nam::get_dsp(json, data);
    if (dsp == nullptr) {
      result.error = "Model could not be created";
      return result;
    }
    if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1) {
      result.error = "Only mono (1 in, 1 out) models are supported";
      return result;
    }

    ModelInfo info;
    info.source = source;
    info.architecture = juce::String(data.architecture);
    info.numWeights = (int)data.weights.size();
    info.name = jsonString(data.metadata, "name");
    if (info.name.isEmpty())
      info.name = fallbackName;
    info.gear = (jsonString(data.metadata, "gear_make") + " " +
                 jsonString(data.metadata, "gear_model"))
                    .trim();
    if (data.metadata.is_object())
      if (const auto it = data.metadata.find("training");
          it != data.metadata.end()) {
        info.testEsr = jsonNumber(*it, "test_esr", -1.0);
        info.validationEsr = jsonNumber(*it, "validation_esr", -1.0);
      }

    result.runner =
        std::make_unique<NamModelRunner>(std::move(dsp), std::move(info));
  } catch (const std::exception &e) {
    result.error = "Invalid model: " + juce::String(e.what());
  }
  return result;
}

void ModelManager::workerLoop() {
  for (;;) {
    juce::String source;
    std::uint64_t id = 0;
    {
      std::unique_lock lock(requestMutex);
      requestCondition.wait(lock, [this] { return hasRequest || quit; });
      if (quit)
        return;
      source = requestSource;
      id = requestIdForWorker;
      hasRequest = false;
    }

    auto result = buildRunner(source);
    if (id != latestRequestId.load())
      continue; // superseded while loading

    if (result.runner == nullptr) {
      setStatus({State::error, {}, result.error});
      continue;
    }
    const auto info = result.runner->getInfo();
    post(std::move(result.runner), id);
    if (id == latestRequestId.load())
      setStatus({State::ready, info, {}});
  }
}

void ModelManager::post(std::unique_ptr<NamModelRunner> runner,
                        std::uint64_t requestId) {
  std::lock_guard lock(specMutex);
  if (requestId != latestRequestId.load())
    return;
  if (specRate > 0.0)
    runner->prepare(
        specRate,
        specBlockSize); // allocates and prewarms, off the audio thread
  if (auto *old = pending.exchange(runner.release(), std::memory_order_acq_rel);
      old != nullptr && old != unloadToken())
    delete old; // the audio thread never saw it
}

void ModelManager::setStatus(Status newStatus) {
  {
    std::lock_guard lock(statusMutex);
    status = std::move(newStatus);
  }
  sendChangeMessage();
}

void ModelManager::prepare(double sampleRate, int maxBlockSize) {
  std::lock_guard lock(specMutex);
  specRate = specRateSeenByAudio = sampleRate;
  specBlockSize = specBlockSeenByAudio = maxBlockSize;
  inputCopy.assign((size_t)maxBlockSize, 0.0f);
  fadeBuffer.assign((size_t)maxBlockSize, 0.0f);
  fadeLength = std::max(1, (int)(0.04 * sampleRate));

  // Audio is stopped while preparing, so finishing a crossfade here is safe.
  if (fading) {
    delete fadingOut;
    fadingOut = nullptr;
    fading = false;
  }
  if (active != nullptr)
    active->prepare(sampleRate, maxBlockSize);
  if (auto *p = pending.load(); p != nullptr && p != unloadToken())
    p->prepare(sampleRate, maxBlockSize);
  activeLatency = active != nullptr ? active->getLatencySamples() : 0;
}

void ModelManager::requestLoad(const juce::String &source) {
  const auto id = ++latestRequestId;
  {
    std::lock_guard lock(statusMutex);
    requestedSource = source;
  }
  setStatus({State::loading, {}, source});
  {
    std::lock_guard lock(requestMutex);
    requestSource = source;
    requestIdForWorker = id;
    hasRequest = true;
  }
  requestCondition.notify_one();
}

void ModelManager::requestUnload() {
  ++latestRequestId; // cancels any load in flight
  {
    std::lock_guard lock(statusMutex);
    requestedSource = {};
  }
  {
    std::lock_guard lock(specMutex);
    if (auto *old = pending.exchange(unloadToken(), std::memory_order_acq_rel);
        old != nullptr && old != unloadToken())
      delete old;
  }
  setStatus({State::empty, {}, {}});
}

bool ModelManager::loadSynchronously(const juce::String &source,
                                     juce::String &error) {
  const auto id = ++latestRequestId;
  {
    std::lock_guard lock(statusMutex);
    requestedSource = source;
  }
  auto result = buildRunner(source);
  if (result.runner == nullptr) {
    error = result.error;
    setStatus({State::error, {}, error});
    return false;
  }
  const auto info = result.runner->getInfo();
  post(std::move(result.runner), id);
  setStatus({State::ready, info, {}});
  return true;
}

juce::String ModelManager::getRequestedSource() const {
  std::lock_guard lock(statusMutex);
  return requestedSource;
}

ModelManager::Status ModelManager::getStatus() const {
  std::lock_guard lock(statusMutex);
  return status;
}

void ModelManager::collectGarbage() {
  const auto scope = trashFifo.read(trashFifo.getNumReady());
  scope.forEach([this](int index) {
    delete trash[(size_t)index];
    trash[(size_t)index] = nullptr;
  });
}

// --- audio thread
// ---------------------------------------------------------------------------

bool ModelManager::retire(NamModelRunner *runner) noexcept {
  if (runner == nullptr)
    return true;
  if (trashFifo.getFreeSpace() < 1)
    return false;
  const auto scope = trashFifo.write(1);
  scope.forEach([this, runner](int index) { trash[(size_t)index] = runner; });
  return true;
}

void ModelManager::pickUpPending() noexcept {
  // One swap at a time, and only with room to retire what we replace.
  if (fading || trashFifo.getFreeSpace() < 2)
    return;

  auto *p = pending.exchange(nullptr, std::memory_order_acq_rel);
  if (p == nullptr)
    return;

  NamModelRunner *incoming = p == unloadToken() ? nullptr : p;
  if (incoming != nullptr &&
      !incoming->isPreparedFor(specRateSeenByAudio, specBlockSeenByAudio)) {
    jassertfalse; // prepare() and post() keep pending models prepared; this
                  // shouldn't happen
    retire(incoming);
    return;
  }
  if (incoming == nullptr && active == nullptr)
    return;

  fadingOut = active;
  active = incoming;
  fading = true;
  fadePosition = 0;
  activeLatency.store(active != nullptr ? active->getLatencySamples() : 0,
                      std::memory_order_relaxed);
}

void ModelManager::render(NamModelRunner *runner, const float *in, float *out,
                          int n) noexcept {
  if (runner == nullptr) {
    std::memcpy(out, in, sizeof(float) * (size_t)n);
    return;
  }
  runner->process(in, out, n);
}

void ModelManager::process(const float *in, float *out, int n) noexcept {
  jassert(n <= (int)inputCopy.size());
  n = std::min(n, (int)inputCopy.size());
  if (n <= 0)
    return;

  pickUpPending();
  std::memcpy(inputCopy.data(), in, sizeof(float) * (size_t)n);

  render(active, inputCopy.data(), out, n);

  if (!fading)
    return;

  render(fadingOut, inputCopy.data(), fadeBuffer.data(), n);
  for (int i = 0; i < n; ++i) {
    const float t =
        std::min(1.0f, (float)(fadePosition + i + 1) / (float)fadeLength);
    out[i] = fadeBuffer[(size_t)i] + t * (out[i] - fadeBuffer[(size_t)i]);
  }
  fadePosition += n;
  if (fadePosition >= fadeLength && retire(fadingOut)) {
    fadingOut = nullptr;
    fading = false;
  }
}
} // namespace evil
