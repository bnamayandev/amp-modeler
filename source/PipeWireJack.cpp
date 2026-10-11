#include "PipeWireJack.h"

#if JUCE_LINUX && defined(JucePlugin_Build_Standalone) &&                     \
    JucePlugin_Build_Standalone
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <cstdlib>
#include <dlfcn.h>

namespace evil::standalone {
namespace {
const juce::String jackTypeName = "JACK";

/** JUCE dlopens "libjack.so.0" when it first lists JACK devices. PipeWire's
    libjack has that soname too, so loading it first makes JUCE use it, as
    `pw-jack` would. False when this isn't a PipeWire session or PipeWire's
    JACK isn't installed: JUCE's own JACK support is then left alone. */
bool loadPipeWireJack() {
  const auto runtimeDir =
      juce::SystemStats::getEnvironmentVariable("XDG_RUNTIME_DIR", {});
  if (runtimeDir.isEmpty() ||
      !juce::File(runtimeDir).getChildFile("pipewire-0").exists())
    return false;

  for (const auto *libDir : {"/usr/lib/x86_64-linux-gnu",
                             "/usr/lib/aarch64-linux-gnu", "/usr/lib64",
                             "/usr/lib"}) {
    const auto path = juce::String(libDir) + "/pipewire-0.3/jack/libjack.so.0";
    if (juce::File(path).existsAsFile() &&
        dlopen(path.toRawUTF8(), RTLD_NOW | RTLD_GLOBAL) != nullptr)
      return true; // kept loaded for the life of the app
  }
  return false;
}

/** JUCE's JACK device, recreated with a PIPEWIRE_QUANTUM request whenever
    open() asks for a rate or size the graph isn't running at. */
class PipeWireJackDevice final : public juce::AudioIODevice {
public:
  PipeWireJackDevice(juce::AudioIODeviceType &jackType,
                     const juce::String &outputName,
                     const juce::String &inputName)
      : AudioIODevice(outputName.isEmpty() ? inputName : outputName,
                      jackTypeName),
        outName(outputName), inName(inputName), jack(jackType),
        inner(jack.createDevice(outName, inName)) {}

  const juce::String outName, inName;

  juce::StringArray getOutputChannelNames() override {
    return inner != nullptr ? inner->getOutputChannelNames()
                            : juce::StringArray();
  }
  juce::StringArray getInputChannelNames() override {
    return inner != nullptr ? inner->getInputChannelNames()
                            : juce::StringArray();
  }
  juce::Array<double> getAvailableSampleRates() override {
    return {44100.0, 48000.0, 88200.0, 96000.0};
  }
  juce::Array<int> getAvailableBufferSizes() override {
    return {32, 64, 128, 256, 512, 1024, 2048};
  }
  int getDefaultBufferSize() override { return 128; }

  juce::String open(const juce::BigInteger &inputChannels,
                    const juce::BigInteger &outputChannels, double sampleRate,
                    int bufferSize) override {
    const int rate = juce::roundToInt(sampleRate);
    if (rate > 0 && bufferSize > 0 && !isRunningAt(rate, bufferSize)) {
      // The variable is read only when a JACK client opens.
      inner.reset();
      setenv("PIPEWIRE_QUANTUM",
             (juce::String(bufferSize) + "/" + juce::String(rate)).toRawUTF8(),
             1);
      inner.reset(jack.createDevice(outName, inName));
    }
    if (inner == nullptr)
      return "No JACK client running";

    const auto error =
        inner->open(inputChannels, outputChannels, sampleRate, bufferSize);

    // PipeWire switches the graph once the client is active; wait for that so
    // the processor is prepared for the size it will really get.
    for (int i = 0; error.isEmpty() && i < 50 && !isRunningAt(rate, bufferSize);
         ++i)
      juce::Thread::sleep(5);
    return error;
  }

  void close() override {
    if (inner != nullptr)
      inner->close();
  }
  bool isOpen() override { return inner != nullptr && inner->isOpen(); }
  void start(juce::AudioIODeviceCallback *callback) override {
    if (inner != nullptr)
      inner->start(callback);
  }
  void stop() override {
    if (inner != nullptr)
      inner->stop();
  }
  bool isPlaying() override { return inner != nullptr && inner->isPlaying(); }
  juce::String getLastError() override {
    return inner != nullptr ? inner->getLastError() : juce::String();
  }
  int getCurrentBufferSizeSamples() override {
    return inner != nullptr ? inner->getCurrentBufferSizeSamples() : 0;
  }
  double getCurrentSampleRate() override {
    return inner != nullptr ? inner->getCurrentSampleRate() : 0.0;
  }
  int getCurrentBitDepth() override {
    return inner != nullptr ? inner->getCurrentBitDepth() : 0;
  }
  juce::BigInteger getActiveOutputChannels() const override {
    return inner != nullptr ? inner->getActiveOutputChannels()
                            : juce::BigInteger();
  }
  juce::BigInteger getActiveInputChannels() const override {
    return inner != nullptr ? inner->getActiveInputChannels()
                            : juce::BigInteger();
  }
  int getOutputLatencyInSamples() override {
    return inner != nullptr ? inner->getOutputLatencyInSamples() : 0;
  }
  int getInputLatencyInSamples() override {
    return inner != nullptr ? inner->getInputLatencyInSamples() : 0;
  }
  int getXRunCount() const noexcept override {
    return inner != nullptr ? inner->getXRunCount() : -1;
  }

private:
  bool isRunningAt(int rate, int bufferSize) {
    return inner != nullptr &&
           inner->getCurrentBufferSizeSamples() == bufferSize &&
           juce::roundToInt(inner->getCurrentSampleRate()) == rate;
  }

  juce::AudioIODeviceType &jack;
  std::unique_ptr<juce::AudioIODevice> inner;
};

/** Takes the place of JUCE's JACK type under the same name, so saved audio
    settings still find it. Device lists come from JUCE's type. */
class PipeWireJackType final : public juce::AudioIODeviceType,
                               private juce::AudioIODeviceType::Listener {
public:
  PipeWireJackType()
      : AudioIODeviceType(jackTypeName),
        jack(juce::AudioIODeviceType::createAudioIODeviceType_JACK()) {
    jack->addListener(this);
  }
  ~PipeWireJackType() override { jack->removeListener(this); }

  void scanForDevices() override { jack->scanForDevices(); }
  juce::StringArray getDeviceNames(bool wantInputNames) const override {
    return jack->getDeviceNames(wantInputNames);
  }
  int getDefaultDeviceIndex(bool forInput) const override {
    return jack->getDefaultDeviceIndex(forInput);
  }
  int getIndexOfDevice(juce::AudioIODevice *device,
                       bool asInput) const override {
    auto *d = dynamic_cast<PipeWireJackDevice *>(device);
    return d != nullptr
               ? getDeviceNames(asInput).indexOf(asInput ? d->inName
                                                         : d->outName)
               : -1;
  }
  bool hasSeparateInputsAndOutputs() const override { return true; }
  juce::AudioIODevice *
  createDevice(const juce::String &outputDeviceName,
               const juce::String &inputDeviceName) override {
    if (getIndexOf(outputDeviceName, false) < 0 &&
        getIndexOf(inputDeviceName, true) < 0)
      return nullptr;
    return new PipeWireJackDevice(*jack, outputDeviceName, inputDeviceName);
  }

private:
  int getIndexOf(const juce::String &name, bool asInput) const {
    return name.isEmpty() ? -1 : getDeviceNames(asInput).indexOf(name);
  }
  void audioDeviceListChanged() override { callDeviceChangeListeners(); }

  std::unique_ptr<juce::AudioIODeviceType> jack;
};
} // namespace

void prepareAudioDevices() {
  auto *holder = juce::StandalonePluginHolder::getInstance();
  if (holder == nullptr || !loadPipeWireJack())
    return;

  auto &devices = holder->deviceManager;
  for (auto *type : devices.getAvailableDeviceTypes())
    if (type->getTypeName() == jackTypeName) {
      devices.removeAudioDeviceType(type);
      break;
    }
  auto replacement = std::make_unique<PipeWireJackType>();
  replacement->scanForDevices();
  devices.addAudioDeviceType(std::move(replacement));
}
} // namespace evil::standalone

#else

namespace evil::standalone {
void prepareAudioDevices() {}
} // namespace evil::standalone

#endif
