#pragma once

#include <atomic>
#include <cstdint>

namespace evil {
/** Click track mixed into the output. Beat one of each bar clicks higher.
   Tempo changes take effect smoothly: the position is kept in beats, so a new
   tempo just changes how fast it advances. */
class Metronome {
public:
  static constexpr float minBpm = 40.0f, maxBpm = 240.0f;

  struct Settings {
    bool running = false;
    float bpm = 120.0f;
    int beatsPerBar = 4;
    float levelDb = -8.0f;
  };

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;
  /** Adds the click to data (which keeps its content). */
  void process(float *data, int n, const Settings &s) noexcept;

  /** Makes the next block start a bar (thread-safe; e.g. on tap tempo). */
  void restartBar() noexcept { restartRequested.store(true); }

  /** For the GUI: the beat in the bar that last clicked (-1 when stopped), and
     a count that increases on every click. */
  int getBeatInBar() const noexcept { return beatInBarForGui.load(); }
  std::uint32_t getClickCount() const noexcept { return clickCount.load(); }

private:
  double sampleRate = 48000.0;
  double beatPosition = 0.0; // beats since the bar started
  int beatInBar = -1;
  int clickSample = 0, clickLength = 0;
  double clickPhase = 0.0, clickIncrement = 0.0;
  float clickDecay = 0.0f, clickEnvelope = 0.0f;
  bool wasRunning = false;
  std::atomic<bool> restartRequested{false};
  std::atomic<int> beatInBarForGui{-1};
  std::atomic<std::uint32_t> clickCount{0};
};
} // namespace evil
