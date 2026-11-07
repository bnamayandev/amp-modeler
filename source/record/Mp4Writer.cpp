#include "Mp4Writer.h"

#include <faac.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace evil {
namespace {
constexpr auto max32 = (std::uint64_t)std::numeric_limits<std::uint32_t>::max();

constexpr int fallbackRate = 48000;

// About 1.4 MB a minute. Rates below 48 kHz scale it down: they have less to code,
// and the format caps a frame's size.
constexpr int maxBitRate = 192000, bitsPerSample = 4;

constexpr int inputChunk = 4096;
constexpr int packetsPerChunk = 256; // about 5 s, and a few hundred KB

// FAAC takes floats on the scale of 16-bit samples.
constexpr float encoderFullScale = 32768.0f;

bool hasAacRateCode(int rate) {
  static constexpr std::array<int, 12> rates{96000, 88200, 64000, 48000, 44100, 32000,
                                             24000, 22050, 16000, 12000, 11025, 8000};
  return std::find(rates.begin(), rates.end(), rate) != rates.end();
}

void write16(juce::OutputStream &out, int value) { out.writeShortBigEndian((short)value); }

void write32(juce::OutputStream &out, std::uint64_t value) {
  out.writeIntBigEndian((int)(std::uint32_t)value);
}

void write64(juce::OutputStream &out, std::uint64_t value) {
  out.writeInt64BigEndian((juce::int64)value);
}

void writeTag(juce::OutputStream &out, const char *tag) { out.write(tag, 4); }

void writeZeros(juce::OutputStream &out, int count) {
  out.writeRepeatedByte(0, (size_t)count);
}

/** A field that's 64 bits wide in a version 1 box and 32 in version 0. */
void writeTime(juce::OutputStream &out, int version, std::uint64_t value) {
  if (version == 1)
    write64(out, value);
  else
    write32(out, value);
}

template <typename Fill> void box(juce::OutputStream &out, const char *type, Fill &&fill) {
  juce::MemoryOutputStream body;
  fill(body);
  write32(out, body.getDataSize() + 8);
  writeTag(out, type);
  out.write(body.getData(), body.getDataSize());
}

template <typename Fill>
void fullBox(juce::OutputStream &out, const char *type, int version, int flags, Fill &&fill) {
  box(out, type, [&](juce::OutputStream &body) {
    write32(body, ((std::uint32_t)version << 24) | (std::uint32_t)flags);
    fill(body);
  });
}

void writeUnityMatrix(juce::OutputStream &out) {
  for (const std::uint32_t value :
       {0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u})
    write32(out, value);
}

/** An MPEG-4 descriptor with a one-byte length, which all of these fit. */
template <typename Fill> void descriptor(juce::OutputStream &out, int tag, Fill &&fill) {
  juce::MemoryOutputStream body;
  fill(body);
  jassert(body.getDataSize() < 128);
  out.writeByte((char)tag);
  out.writeByte((char)body.getDataSize());
  out.write(body.getData(), body.getDataSize());
}
} // namespace

std::unique_ptr<Mp4Writer> Mp4Writer::create(const juce::File &file, double sampleRate,
                                             juce::String &error) {
  std::unique_ptr<Mp4Writer> writer(new Mp4Writer());
  writer->inputRate = juce::roundToInt(sampleRate);
  writer->fileRate = hasAacRateCode(writer->inputRate) ? writer->inputRate : fallbackRate;

  faac_params params;
  auto status = faac_params_init(&params, sizeof(params));
  if (status == FAAC_OK) {
    params.sample_rate = (std::uint32_t)writer->fileRate;
    params.num_channels = 1;
    params.mpeg_version = FAAC_MPEG4;
    params.object_type = FAAC_OBJ_LOW;
    params.use_tns = true; // keeps pre-echo off the pick attacks
    params.use_pns = false;
    params.bit_rate = (std::uint32_t)juce::jmin(maxBitRate, bitsPerSample * writer->fileRate);
    params.rate_control = FAAC_RC_ABR;
    params.output_format = FAAC_STREAM_RAW;
    params.input_format = FAAC_INPUT_FLOAT;
    status = faac_encoder_open(&params, &writer->encoder);
  }

  faac_encoder_info info{};
  info.struct_size = sizeof(info);
  const std::uint8_t *config = nullptr;
  std::uint32_t configSize = 0;
  if (status == FAAC_OK)
    status = faac_encoder_get_info(writer->encoder, &info);
  if (status == FAAC_OK)
    status = faac_encoder_asc(writer->encoder, &config, &configSize);
  if (status != FAAC_OK) {
    error = "The AAC encoder wouldn't start: " + juce::String(faac_strerror(status));
    return nullptr;
  }

  writer->frameSamples = (int)info.frame_samples;
  writer->encoderDelay = (int)info.encoder_delay;
  writer->decoderConfig.assign(config, config + configSize);
  writer->packet.resize(info.max_output_bytes);
  writer->scaled.resize(inputChunk);

  if (writer->fileRate != writer->inputRate) {
    writer->resampler = std::make_unique<StreamingResampler>();
    writer->resampler->prepare(writer->inputRate, writer->fileRate, inputChunk);
    writer->resampled.resize((size_t)writer->resampler->maxOutputsFor(inputChunk));
  }

  constexpr std::int64_t secondsFrom1904To1970 = 2082844800;
  writer->createdAt =
      (std::uint32_t)(juce::Time::currentTimeMillis() / 1000 + secondsFrom1904To1970);

  file.deleteFile();
  auto stream = std::make_unique<juce::FileOutputStream>(file);
  if (stream->failedToOpen()) {
    error = "Couldn't create " + file.getFullPathName() + ": " +
            stream->getStatus().getErrorMessage();
    return nullptr;
  }

  box(*stream, "ftyp", [](juce::OutputStream &body) {
    writeTag(body, "isom");
    write32(body, 512);
    for (const auto *brand : {"isom", "iso2", "mp41"})
      writeTag(body, brand);
  });

  // The data's header is filled in once its size is known. An empty box goes in
  // front of it, which a take too long for a 32-bit size turns into the extra
  // room that a 64-bit one needs.
  writer->mdatHeader = stream->getPosition();
  box(*stream, "free", [](juce::OutputStream &) {});
  write32(*stream, 0);
  writeTag(*stream, "mdat");
  writer->dataStart = stream->getPosition();

  if (stream->getStatus().failed()) {
    error = "Couldn't write to " + file.getFullPathName() + ": " +
            stream->getStatus().getErrorMessage();
    return nullptr;
  }
  writer->out = std::move(stream);
  return writer;
}

Mp4Writer::~Mp4Writer() {
  if (encoder != nullptr)
    faac_encoder_close(&encoder);
}

bool Mp4Writer::fail(const juce::String &message) {
  if (error.isEmpty())
    error = message;
  return false;
}

bool Mp4Writer::write(const float *samples, int numSamples) {
  if (error.isNotEmpty())
    return false;

  for (int done = 0; done < numSamples;) {
    const int count = juce::jmin(numSamples - done, inputChunk);
    for (int i = 0; i < count; ++i) {
      const float x = samples[done + i];
      scaled[(size_t)i] =
          std::isfinite(x) ? juce::jlimit(-1.0f, 1.0f, x) * encoderFullScale : 0.0f;
    }
    done += count;
    samplesIn += count;

    if (resampler == nullptr) {
      if (!encode(scaled.data(), count))
        return false;
      continue;
    }
    resampler->push(scaled.data(), count);
    for (int n; (n = resampler->pop(resampled.data(), (int)resampled.size())) > 0;)
      if (!encode(resampled.data(), n))
        return false;
  }
  return true;
}

bool Mp4Writer::encode(const float *samples, int numSamples) {
  // In up to a frame at a time, since the encoder gives out one frame per call.
  for (int done = 0; done < numSamples;) {
    const int count = juce::jmin(numSamples - done, frameSamples);
    std::uint32_t bytes = 0;
    const auto status = faac_encoder_encode(encoder, samples + done, (std::uint32_t)count,
                                            packet.data(), (std::uint32_t)packet.size(), &bytes);
    if (status != FAAC_OK)
      return fail("The AAC encoder failed: " + juce::String(faac_strerror(status)));
    if (bytes > 0 && !writePacket(bytes))
      return false;
    done += count;
    samplesEncoded += count;
  }
  return true;
}

bool Mp4Writer::writePacket(std::uint32_t numBytes) {
  if (!out->write(packet.data(), numBytes))
    return fail("Couldn't write the recording: " + out->getStatus().getErrorMessage());
  packetSizes.push_back(numBytes);
  largestPacket = juce::jmax(largestPacket, numBytes);
  dataBytes += numBytes;
  return true;
}

bool Mp4Writer::finish() {
  if (error.isNotEmpty())
    return false;

  // The resampler holds back what it needs to read past its last output. Pushing
  // that much silence lets the real audio's outputs out, and only those are kept.
  if (resampler != nullptr) {
    const auto expected = samplesIn * fileRate / inputRate;
    std::vector<float> silence((size_t)juce::jmin(inputChunk, resampler->getLookaheadInputSamples()));
    for (int left = resampler->getLookaheadInputSamples(); left > 0;) {
      const int count = juce::jmin(left, (int)silence.size());
      resampler->push(silence.data(), count);
      left -= count;
      for (int n; (n = resampler->pop(resampled.data(), (int)resampled.size())) > 0;)
        if (const auto keep = juce::jmin((std::int64_t)n, expected - samplesEncoded); keep > 0)
          if (!encode(resampled.data(), (int)keep))
            return false;
    }
  }

  for (;;) { // no input means flush; it ends with an empty frame
    std::uint32_t bytes = 0;
    const auto status = faac_encoder_encode(encoder, nullptr, 0, packet.data(),
                                            (std::uint32_t)packet.size(), &bytes);
    if (status != FAAC_OK)
      return fail("The AAC encoder failed: " + juce::String(faac_strerror(status)));
    if (bytes == 0)
      break;
    if (!writePacket(bytes))
      return false;
  }

  writeIndex(*out);

  const auto mdatSize = (std::uint64_t)dataBytes + 8;
  if (mdatSize <= max32) {
    out->setPosition(mdatHeader + 8);
    write32(*out, mdatSize);
  } else {
    out->setPosition(mdatHeader);
    write32(*out, 1);
    writeTag(*out, "mdat");
    write64(*out, mdatSize + 8);
  }
  out->flush();

  if (out->getStatus().failed())
    return fail("Couldn't write the recording: " + out->getStatus().getErrorMessage());
  return true;
}

void Mp4Writer::writeIndex(juce::OutputStream &stream) {
  const auto numPackets = (std::uint64_t)packetSizes.size();
  const auto mediaLength = numPackets * (std::uint64_t)frameSamples;
  const auto playLength = (std::uint64_t)samplesEncoded;
  const auto rate = (std::uint64_t)fileRate;
  const int version = juce::jmax(mediaLength, playLength) > max32 ? 1 : 0;

  const auto writeDates = [&](juce::OutputStream &body) {
    writeTime(body, version, createdAt);
    writeTime(body, version, createdAt);
  };

  std::vector<std::uint64_t> chunkOffsets;
  {
    auto offset = (std::uint64_t)dataStart;
    for (size_t i = 0; i < packetSizes.size(); ++i) {
      if (i % (size_t)packetsPerChunk == 0)
        chunkOffsets.push_back(offset);
      offset += packetSizes[i];
    }
  }

  box(stream, "moov", [&](juce::OutputStream &moov) {
    fullBox(moov, "mvhd", version, 0, [&](juce::OutputStream &b) {
      writeDates(b);
      write32(b, rate);
      writeTime(b, version, playLength);
      write32(b, 0x00010000); // rate: 1.0
      write16(b, 0x0100);     // volume: 1.0
      writeZeros(b, 10);
      writeUnityMatrix(b);
      writeZeros(b, 24);
      write32(b, 2); // next track
    });

    box(moov, "trak", [&](juce::OutputStream &trak) {
      fullBox(trak, "tkhd", version, 3, [&](juce::OutputStream &b) { // enabled, in the movie
        writeDates(b);
        write32(b, 1); // track
        writeZeros(b, 4);
        writeTime(b, version, playLength);
        writeZeros(b, 8);
        write16(b, 0); // layer
        write16(b, 0); // alternate group
        write16(b, 0x0100); // volume: 1.0
        writeZeros(b, 2);
        writeUnityMatrix(b);
        write32(b, 0); // no picture: no width
        write32(b, 0); // or height
      });

      // Plays from past the encoder's delay, for exactly the audio that went in.
      box(trak, "edts", [&](juce::OutputStream &edts) {
        fullBox(edts, "elst", version, 0, [&](juce::OutputStream &b) {
          write32(b, 1);
          writeTime(b, version, playLength);
          writeTime(b, version, (std::uint64_t)encoderDelay);
          write16(b, 1); // at normal speed
          write16(b, 0);
        });
      });

      box(trak, "mdia", [&](juce::OutputStream &mdia) {
        fullBox(mdia, "mdhd", version, 0, [&](juce::OutputStream &b) {
          writeDates(b);
          write32(b, rate);
          writeTime(b, version, mediaLength);
          write16(b, 0x55c4); // language: undetermined
          write16(b, 0);
        });
        fullBox(mdia, "hdlr", 0, 0, [](juce::OutputStream &b) {
          write32(b, 0);
          writeTag(b, "soun");
          writeZeros(b, 12);
          b.write("SoundHandler", 13); // with its terminator
        });

        box(mdia, "minf", [&](juce::OutputStream &minf) {
          fullBox(minf, "smhd", 0, 0, [](juce::OutputStream &b) { write32(b, 0); });
          box(minf, "dinf", [](juce::OutputStream &dinf) {
            fullBox(dinf, "dref", 0, 0, [](juce::OutputStream &b) {
              write32(b, 1);
              fullBox(b, "url ", 0, 1, [](juce::OutputStream &) {}); // data's in this file
            });
          });

          box(minf, "stbl", [&](juce::OutputStream &stbl) {
            fullBox(stbl, "stsd", 0, 0, [&](juce::OutputStream &stsd) {
              write32(stsd, 1);
              box(stsd, "mp4a", [&](juce::OutputStream &b) {
                writeZeros(b, 6);
                write16(b, 1); // data reference
                writeZeros(b, 8);
                write16(b, 1);  // channels
                write16(b, 16); // sample size, as the format has it for AAC
                writeZeros(b, 4);
                write32(b, rate <= 0xffff ? rate << 16 : 0); // 16.16; the config has the real rate

                fullBox(b, "esds", 0, 0, [&](juce::OutputStream &esds) {
                  descriptor(esds, 3, [&](juce::OutputStream &es) {
                    write16(es, 1); // track
                    es.writeByte(0);
                    descriptor(es, 4, [&](juce::OutputStream &config) {
                      config.writeByte(0x40); // MPEG-4 audio
                      config.writeByte(0x15); // audio stream
                      config.writeByte((char)(largestPacket >> 16));
                      write16(config, (int)(largestPacket & 0xffff));
                      const auto seconds = (double)mediaLength / (double)rate;
                      const auto bitsPerSecond = [&](double bytes) {
                        return (std::uint64_t)(seconds > 0.0 ? 8.0 * bytes / seconds : 0.0);
                      };
                      write32(config, bitsPerSecond((double)largestPacket * (double)numPackets));
                      write32(config, bitsPerSecond((double)dataBytes));
                      descriptor(config, 5, [&](juce::OutputStream &specific) {
                        specific.write(decoderConfig.data(), decoderConfig.size());
                      });
                    });
                    descriptor(es, 6, [](juce::OutputStream &sl) { sl.writeByte(2); });
                  });
                });
              });
            });

            fullBox(stbl, "stts", 0, 0, [&](juce::OutputStream &b) { // every frame's as long
              write32(b, numPackets > 0 ? 1 : 0);
              if (numPackets > 0) {
                write32(b, numPackets);
                write32(b, (std::uint64_t)frameSamples);
              }
            });

            fullBox(stbl, "stsc", 0, 0, [&](juce::OutputStream &b) { // the last chunk may be short
              const auto chunks = (std::uint64_t)chunkOffsets.size();
              const auto lastChunk = numPackets % (std::uint64_t)packetsPerChunk;
              const bool shortLast = lastChunk != 0;
              const auto fullChunks = shortLast ? chunks - 1 : chunks;
              write32(b, (fullChunks > 0 ? 1u : 0u) + (shortLast ? 1u : 0u));
              if (fullChunks > 0) {
                write32(b, 1);
                write32(b, (std::uint64_t)packetsPerChunk);
                write32(b, 1);
              }
              if (shortLast) {
                write32(b, chunks);
                write32(b, lastChunk);
                write32(b, 1);
              }
            });

            fullBox(stbl, "stsz", 0, 0, [&](juce::OutputStream &b) {
              write32(b, 0); // sizes vary
              write32(b, numPackets);
              for (const auto size : packetSizes)
                write32(b, size);
            });

            const bool wide = !chunkOffsets.empty() && chunkOffsets.back() > max32;
            fullBox(stbl, wide ? "co64" : "stco", 0, 0, [&](juce::OutputStream &b) {
              write32(b, chunkOffsets.size());
              for (const auto offset : chunkOffsets)
                if (wide)
                  write64(b, offset);
                else
                  write32(b, offset);
            });
          });
        });
      });
    });
  });
}
} // namespace evil
