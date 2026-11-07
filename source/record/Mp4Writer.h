#pragma once

#include "dsp/StreamingResampler.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

struct faac_encoder;

namespace evil {
/** Writes mono audio to an .mp4 file as AAC-LC, which plays on anything. There's
    no video track.

    The MP4's index goes at the end of the file, so the file only plays once
    finish() has run. Nothing here is real-time safe: see Recorder for how the
    audio thread gets samples to it. */
class Mp4Writer {
public:
  /** Creates the file, replacing any already there. Returns null and says why
      if it can't. A rate AAC has no code for is resampled to 48 kHz. */
  static std::unique_ptr<Mp4Writer> create(const juce::File &file, double sampleRate,
                                           juce::String &error);
  ~Mp4Writer();

  /** Samples past full scale are clipped, as a sound card would, and non-finite
      ones silenced. Returns false once writing has failed; see getError(). */
  bool write(const float *samples, int numSamples);

  /** Drains the encoder and writes the index. Call once, after the last write. */
  bool finish();

  const juce::String &getError() const noexcept { return error; }

  /** How much audio the file plays, at getFileSampleRate(). */
  std::int64_t getNumSamples() const noexcept { return samplesEncoded; }
  int getFileSampleRate() const noexcept { return fileRate; }

private:
  Mp4Writer() = default;

  bool encode(const float *samples, int numSamples);
  bool writePacket(std::uint32_t numBytes);
  void writeIndex(juce::OutputStream &);
  bool fail(const juce::String &message);

  faac_encoder *encoder = nullptr;
  std::unique_ptr<juce::FileOutputStream> out;
  std::unique_ptr<StreamingResampler> resampler; // only for a rate AAC doesn't have

  int inputRate = 0, fileRate = 0;
  int frameSamples = 0, encoderDelay = 0;
  std::vector<std::uint8_t> decoderConfig; // AudioSpecificConfig, from the encoder
  std::vector<std::uint8_t> packet;
  std::vector<float> scaled, resampled;

  std::vector<std::uint32_t> packetSizes;
  std::uint32_t largestPacket = 0;
  std::int64_t mdatHeader = 0, dataStart = 0, dataBytes = 0;
  std::int64_t samplesIn = 0, samplesEncoded = 0;
  std::uint32_t createdAt = 0; // seconds since 1904, as MP4 counts them
  juce::String error;

  JUCE_DECLARE_NON_COPYABLE(Mp4Writer)
};
} // namespace evil
