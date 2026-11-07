#include "AllocationTracker.h"
#include "record/Mp4Writer.h"
#include "record/Recorder.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace evil;
using namespace evil::test;

namespace {
/** Reads the parts of an MP4 file its players depend on. Nothing here decodes
    the audio: JUCE has no AAC decoder on every platform. */
struct Mp4 {
  juce::MemoryBlock file;

  struct Box {
    const std::uint8_t *data = nullptr;
    size_t size = 0;

    std::uint32_t u32(size_t at) const {
      REQUIRE(at + 4 <= size);
      return ((std::uint32_t)data[at] << 24) | ((std::uint32_t)data[at + 1] << 16) |
             ((std::uint32_t)data[at + 2] << 8) | (std::uint32_t)data[at + 3];
    }

    /** The first box of this type directly inside this one, past `skip` bytes. */
    std::optional<Box> find(const char *type, size_t skip = 0) const {
      for (size_t at = skip; at + 8 <= size;) {
        size_t boxSize = u32(at);
        REQUIRE(boxSize >= 8);
        REQUIRE(at + boxSize <= size);
        if (std::memcmp(data + at + 4, type, 4) == 0)
          return Box{data + at + 8, boxSize - 8};
        at += boxSize;
      }
      return std::nullopt;
    }

    Box path(std::initializer_list<const char *> types) const {
      Box box = *this;
      for (const auto *type : types) {
        INFO("looking for " << type);
        const auto found = box.find(type);
        REQUIRE(found.has_value());
        box = *found;
      }
      return box;
    }
  };

  explicit Mp4(const juce::File &source) { REQUIRE(source.loadFileAsData(file)); }

  Box root() const { return {(const std::uint8_t *)file.getData(), file.getSize()}; }
  Box sampleTable() const {
    return root().path({"moov", "trak", "mdia", "minf", "stbl"});
  }
  std::uint32_t numPackets() const { return sampleTable().path({"stsz"}).u32(8); }
  std::uint32_t timescale() const {
    return root().path({"moov", "trak", "mdia", "mdhd"}).u32(12);
  }
  /** The audio the edit list plays, and how far in it starts. */
  std::uint32_t playLength() const {
    return root().path({"moov", "trak", "edts", "elst"}).u32(8);
  }
  std::uint32_t skipped() const {
    return root().path({"moov", "trak", "edts", "elst"}).u32(12);
  }
  /** The AudioSpecificConfig: 2 bytes for AAC-LC, packed bit by bit. */
  int decoderConfig() const {
    const auto stsd = sampleTable().path({"stsd"});
    const auto mp4a = stsd.find("mp4a", 8);
    REQUIRE(mp4a.has_value());
    const auto esds = mp4a->find("esds", 28);
    REQUIRE(esds.has_value());
    const auto *d = esds->data;
    const size_t last = esds->size;
    // Find the DecoderSpecificInfo's tag: it's last but for the 3-byte SL descriptor.
    REQUIRE(last >= 7);
    REQUIRE(d[last - 7] == 5);
    REQUIRE(d[last - 6] == 2);
    return (d[last - 5] << 8) | d[last - 4];
  }
};

std::vector<float> guitarLike(int n, double rate) {
  std::vector<float> x((size_t)n);
  juce::Random noise(3);
  for (int i = 0; i < n; ++i) {
    const double t = (double)i / rate;
    double v = 0.0;
    for (int k = 1; k <= 12; ++k)
      v += 0.25 / k * std::sin(juce::MathConstants<double>::twoPi * 110.0 * k * t);
    x[(size_t)i] = (float)(v * std::exp(-2.0 * std::fmod(t, 0.5))) +
                   0.01f * (noise.nextFloat() - 0.5f);
  }
  return x;
}

struct TempFolder {
  juce::File dir = juce::File::createTempFile("evil-recording");
  TempFolder() { dir.createDirectory(); }
  ~TempFolder() { dir.deleteRecursively(); }
  int count(const juce::String &pattern) const {
    return dir.getNumberOfChildFiles(juce::File::findFiles, pattern);
  }
};
} // namespace

TEST_CASE("A recorded file's index accounts for all its audio", "[recording]") {
  const auto [rate, frequencyIndex] = GENERATE(std::pair{48000, 3}, std::pair{44100, 4},
                                               std::pair{96000, 0}, std::pair{22050, 7});
  CAPTURE(rate);
  const int numSamples = rate * 3 + 123; // not a whole number of frames
  const auto audio = guitarLike(numSamples, rate);

  TempFolder folder;
  const auto path = folder.dir.getChildFile("take.mp4");
  juce::String error;
  auto writer = Mp4Writer::create(path, rate, error);
  REQUIRE(writer != nullptr);
  // In odd pieces, the way the queue hands them over.
  for (int done = 0; done < numSamples;) {
    const int count = juce::jmin(numSamples - done, 1 + (done * 7) % 5000);
    REQUIRE(writer->write(audio.data() + done, count));
    done += count;
  }
  REQUIRE(writer->finish());
  CHECK(writer->getNumSamples() == numSamples);
  writer.reset();

  const Mp4 mp4(path);
  const auto root = mp4.root();
  const auto ftyp = root.find("ftyp");
  REQUIRE(ftyp.has_value());
  CHECK(std::memcmp(ftyp->data, "isom", 4) == 0);

  // Every packet in the data box, in the order the index says.
  const auto table = mp4.sampleTable();
  const auto mdat = root.find("mdat");
  REQUIRE(mdat.has_value());
  const auto sizes = table.path({"stsz"});
  CHECK(sizes.u32(4) == 0);
  const auto packets = mp4.numPackets();
  REQUIRE(packets > 0);
  std::uint64_t total = 0;
  for (std::uint32_t i = 0; i < packets; ++i)
    total += sizes.u32(12 + 4 * (size_t)i);
  CHECK(total == mdat->size);

  const auto offsets = table.path({"stco"});
  CHECK(offsets.u32(8) == (std::uint32_t)(mdat->data - root.data));

  // Chunks: the offsets walk the packets, full chunks then a short last one.
  const auto chunks = table.path({"stsc"});
  const auto numChunks = offsets.u32(4);
  const auto perChunk = chunks.u32(8 + 4);
  std::uint64_t walked = offsets.u32(8);
  for (std::uint32_t c = 0; c < numChunks; ++c) {
    CHECK(offsets.u32(8 + 4 * (size_t)c) == walked);
    for (std::uint32_t i = 0; i < perChunk && c * perChunk + i < packets; ++i)
      walked += sizes.u32(12 + 4 * (size_t)(c * perChunk + i));
  }
  CHECK(walked == (std::uint64_t)(mdat->data - root.data) + mdat->size);

  // All the audio is in there and plays exactly: nothing cut, nothing added.
  CHECK(mp4.timescale() == (std::uint32_t)rate);
  const auto times = table.path({"stts"});
  REQUIRE(times.u32(4) == 1);
  CHECK(times.u32(8) == packets);
  const auto frameLength = times.u32(12);
  CHECK(frameLength == 1024);
  CHECK(mp4.playLength() == (std::uint32_t)numSamples);
  CHECK(mp4.skipped() + mp4.playLength() <= (std::uint64_t)packets * frameLength);
  CHECK(mp4.skipped() + mp4.playLength() > ((std::uint64_t)packets - 3) * frameLength);

  // AAC-LC, this rate, mono.
  CHECK(mp4.decoderConfig() == ((2 << 11) | (frequencyIndex << 7) | (1 << 3)));
}

TEST_CASE("A rate AAC has no code for is recorded at 48 kHz", "[recording]") {
  constexpr int rate = 192000, numSamples = rate * 2;
  const auto audio = guitarLike(numSamples, rate);

  TempFolder folder;
  const auto path = folder.dir.getChildFile("take.mp4");
  juce::String error;
  auto writer = Mp4Writer::create(path, rate, error);
  REQUIRE(writer != nullptr);
  CHECK(writer->getFileSampleRate() == 48000);
  REQUIRE(writer->write(audio.data(), numSamples));
  REQUIRE(writer->finish());
  CHECK(writer->getNumSamples() == numSamples / 4);
  writer.reset();

  const Mp4 mp4(path);
  CHECK(mp4.timescale() == 48000);
  CHECK(mp4.playLength() == (std::uint32_t)(numSamples / 4));
  CHECK(mp4.decoderConfig() == ((2 << 11) | (3 << 7) | (1 << 3)));
}

TEST_CASE("Bad samples don't reach the encoder", "[recording]") {
  auto audio = guitarLike(48000, 48000.0);
  audio[1000] = std::numeric_limits<float>::quiet_NaN();
  audio[2000] = std::numeric_limits<float>::infinity();
  audio[3000] = 1000.0f;

  TempFolder folder;
  const auto path = folder.dir.getChildFile("take.mp4");
  juce::String error;
  auto writer = Mp4Writer::create(path, 48000.0, error);
  REQUIRE(writer != nullptr);
  REQUIRE(writer->write(audio.data(), (int)audio.size()));
  REQUIRE(writer->finish());
}

TEST_CASE("A very short take is still a complete file", "[recording]") {
  const auto numSamples = GENERATE(1, 100, 1024, 1025);
  CAPTURE(numSamples);
  const auto audio = guitarLike(numSamples, 48000.0);

  TempFolder folder;
  const auto path = folder.dir.getChildFile("take.mp4");
  juce::String error;
  auto writer = Mp4Writer::create(path, 48000.0, error);
  REQUIRE(writer != nullptr);
  REQUIRE(writer->write(audio.data(), numSamples));
  REQUIRE(writer->finish());
  writer.reset();

  const Mp4 mp4(path);
  REQUIRE(mp4.numPackets() > 0);
  CHECK(mp4.playLength() == (std::uint32_t)numSamples);
  CHECK(mp4.skipped() + mp4.playLength() <= (std::uint64_t)mp4.numPackets() * 1024);
}

TEST_CASE("The recorder saves a take to the folder, under a name of its own",
          "[recording]") {
  constexpr double rate = 48000.0;
  constexpr int block = 128, blocks = 1500; // 4 s
  const auto audio = guitarLike(block * blocks, rate);

  TempFolder folder;
  Recorder recorder;
  CHECK_FALSE(recorder.isRecording());
  recorder.push(audio.data(), block); // ignored: not recording

  juce::String error;
  REQUIRE(recorder.start(folder.dir, rate, error));
  CHECK(recorder.isRecording());

  long allocations = 0, frees = 0;
  for (int b = 0; b < blocks; ++b) {
    {
      ScopedAllocationTracking tracking;
      recorder.push(audio.data() + b * block, block);
      allocations += tracking.allocationCount();
      frees += tracking.deallocationCount();
    }
    if (b % 100 == 0) // let the writer run in between, as it would in real time
      std::this_thread::yield();
  }
  CHECK(allocations == 0);
  CHECK(frees == 0);
  CHECK(recorder.getSeconds() == blocks * block / rate);

  // Until it ends, only a file that says it's unfinished.
  CHECK(folder.count("*.mp4") == 0);
  CHECK(folder.count("*.mp4.part") == 1);

  recorder.stop();
  CHECK_FALSE(recorder.isRecording());
  CHECK(folder.count("*.mp4.part") == 0);
  REQUIRE(folder.count("*.mp4") == 1);

  const auto take = recorder.takeFinished();
  REQUIRE(take.has_value());
  CHECK(take->error.isEmpty());
  CHECK(take->droppedSeconds == 0.0);
  CHECK(take->seconds == blocks * block / rate);
  CHECK(take->file.getFileName().startsWith("Evil Amplifier "));
  CHECK(take->file.hasFileExtension(".mp4"));
  CHECK(Mp4(take->file).playLength() == (std::uint32_t)(blocks * block));
  CHECK_FALSE(recorder.takeFinished().has_value()); // only says so once

  // The next take, in the same second, can't land on the first.
  recorder.push(audio.data(), block); // ignored again
  REQUIRE(recorder.start(folder.dir, rate, error));
  recorder.push(audio.data(), block * 10);
  recorder.stop();
  const auto second = recorder.takeFinished();
  REQUIRE(second.has_value());
  CHECK(second->error.isEmpty());
  CHECK(second->file != take->file);
  CHECK(folder.count("*.mp4") == 2);
  CHECK(Mp4(second->file).playLength() == (std::uint32_t)(block * 10));
}

TEST_CASE("A take with no audio saves nothing and says so", "[recording]") {
  TempFolder folder;
  Recorder recorder;
  juce::String error;
  REQUIRE(recorder.start(folder.dir, 48000.0, error));
  recorder.stop();

  const auto take = recorder.takeFinished();
  REQUIRE(take.has_value());
  CHECK(take->error.isNotEmpty());
  CHECK(take->file == juce::File());
  CHECK(folder.dir.getNumberOfChildFiles(juce::File::findFiles) == 0);
}

TEST_CASE("The recorder says when it can't start", "[recording]") {
  TempFolder folder;
  Recorder recorder;
  juce::String error;

  SECTION("no audio device") {
    CHECK_FALSE(recorder.start(folder.dir, 0.0, error));
  }
  SECTION("the folder can't exist") {
    const auto blocker = folder.dir.getChildFile("a file");
    REQUIRE(blocker.replaceWithText("in the way"));
    CHECK_FALSE(recorder.start(blocker.getChildFile("Downloads"), 48000.0, error));
  }
  CHECK(error.isNotEmpty());
  CHECK_FALSE(recorder.isRecording());
}

TEST_CASE("The recorder makes the folder if it's missing", "[recording]") {
  TempFolder folder;
  const auto missing = folder.dir.getChildFile("a/b");
  Recorder recorder;
  juce::String error;
  REQUIRE(recorder.start(missing, 48000.0, error));
  const std::array<float, 2048> silence{};
  recorder.push(silence.data(), (int)silence.size());
  recorder.stop();
  const auto take = recorder.takeFinished();
  REQUIRE(take.has_value());
  CHECK(take->error.isEmpty());
  CHECK(take->file.getParentDirectory() == missing);
}

TEST_CASE("A new device rate ends the take, and the same one doesn't", "[recording]") {
  TempFolder folder;
  Recorder recorder;
  juce::String error;
  REQUIRE(recorder.start(folder.dir, 48000.0, error));
  const std::array<float, 2048> silence{};
  recorder.push(silence.data(), (int)silence.size());

  recorder.sampleRateChanged(48000.0);
  CHECK(recorder.isRecording());
  recorder.sampleRateChanged(44100.0);
  CHECK_FALSE(recorder.isRecording());
  const auto take = recorder.takeFinished();
  REQUIRE(take.has_value());
  CHECK(take->error.isEmpty());
  CHECK(take->file.existsAsFile());
}

TEST_CASE("Audio the queue had no room for is reported", "[recording]") {
  TempFolder folder;
  Recorder recorder;
  juce::String error;
  REQUIRE(recorder.start(folder.dir, 48000.0, error));
  std::vector<float> tooMuch(1 << 19);
  recorder.push(tooMuch.data(), (int)tooMuch.size());
  recorder.stop();
  const auto take = recorder.takeFinished();
  REQUIRE(take.has_value());
  CHECK(take->error.isEmpty());
  CHECK(take->droppedSeconds > 0.0);
}

TEST_CASE("A take still under way is finished by the destructor", "[recording]") {
  TempFolder folder;
  {
    Recorder recorder;
    juce::String error;
    REQUIRE(recorder.start(folder.dir, 48000.0, error));
    const auto audio = guitarLike(48000, 48000.0);
    recorder.push(audio.data(), (int)audio.size());
  }
  REQUIRE(folder.count("*.mp4") == 1);
  CHECK(folder.count("*.part") == 0);
}

TEST_CASE("Downloads is somewhere absolute, even with nothing set up", "[recording]") {
  const auto downloads = getDownloadsFolder();
  CHECK(juce::File::isAbsolutePath(downloads.getFullPathName()));
  CHECK(downloads != juce::File());
}
