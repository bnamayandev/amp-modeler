#pragma once

#include "AnalogStages.h"
#include "Biquad.h"
#include "DelayLine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace evil {
/** Click-free footswitch: crossfades between the dry input and an effect's
   output over 10 ms. Once fully off the effect is skipped, so an off pedal is a
   bit-exact pass-through and costs nothing; switching back on resets the
   effect first so no stale tail plays. */
class FootswitchFade {
public:
  void prepare(double sampleRate) {
    length = std::max(1, (int)(0.01 * sampleRate));
    reset();
  }
  void reset() noexcept {
    position = 0;
    primed = false;
  }

  /** Runs effect.process(data, n, settings) blended by settings.on. `scratch`
     holds at least n samples. */
  template <typename Effect, typename Settings>
  void process(Effect &effect, float *data, int n, const Settings &settings,
               float *scratch) noexcept {
    if (!primed) { // start in the first block's state, without a fade
      position = settings.on ? length : 0;
      if (settings.on)
        effect.reset();
      primed = true;
    }
    if (!settings.on && position == 0)
      return;
    if (settings.on && position == 0)
      effect.reset();
    if (settings.on && position == length) {
      effect.process(data, n, settings);
      return;
    }
    std::memcpy(scratch, data, sizeof(float) * (size_t)n);
    effect.process(data, n, settings);
    const int step = settings.on ? 1 : -1;
    for (int i = 0; i < n; ++i) {
      position = std::clamp(position + step, 0, length);
      const float t = (float)position / (float)length;
      data[i] = scratch[i] + t * (data[i] - scratch[i]);
    }
  }

private:
  int length = 480, position = 0;
  bool primed = false;
};

/** Wah: a resonant band-pass swept from 350 Hz (heel) to 2.2 kHz (toe), the
   range of the classic inductor wahs, as a state-variable filter that stays
   smooth under fast sweeps. Auto adds an envelope follower that opens the
   filter from the pedal position as you pick harder: all the way up it is an
   envelope filter. */
class WahPedal {
public:
  struct Settings {
    bool on = false;
    float position = 0.5f;   // 0 heel .. 1 toe
    float autoAmount = 0.0f; // 0 manual .. 1 the envelope sweeps to the toe
    float resonance = 0.5f;  // 0..1
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  float attackCoeff = 0.0f, releaseCoeff = 0.0f, envelope = 0.0f;
  float ic1 = 0.0f, ic2 = 0.0f;          // filter state
  float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f; // filter coefficients
  float k = 1.0f, outputGain = 1.0f;
  juce::SmoothedValue<float> position, autoAmount, resonance;
  bool primed = false;
};

/** Feed-forward compressor with a soft knee, auto make-up and a parallel blend.
   Sustain lowers the threshold and raises the ratio together. */
class CompressorPedal {
public:
  struct Settings {
    bool on = false;
    float sustain = 0.5f; // 0..1
    float blend = 1.0f;   // 0 dry .. 1 compressed
    float levelDb = 0.0f;
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

  /** Gain reduction of the last sample, in dB (>= 0). */
  float getGainReductionDb() const noexcept { return reductionDb; }

private:
  float attackCoeff = 0.0f, releaseCoeff = 0.0f, peakReleaseCoeff = 0.0f;
  float peak = 0.0f, reductionDb = 0.0f;
  juce::SmoothedValue<float> sustain, blend;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> level;
  bool primed = false, envelopePrimed = false;
};

/** Fuzz modelled on the Big Muff Pi circuit: a gain stage, the Sustain pot, two
   cascaded clipping stages with diodes and 470 pF in their feedback (which is
   what keeps it smooth), then the passive tone stack that crossfades a 408 Hz
   low-pass with a 1.86 kHz high-pass and scoops the mids at noon. */
class FuzzPedal {
public:
  struct Settings {
    bool on = false;
    float fuzz = 0.7f; // the Sustain knob, 0..1
    float tone = 0.5f; // 0 dark .. 1 bright
    float levelDb = 0.0f;
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double rate = 48000.0; // the oversampled rate
  analog::Oversampler oversampler;
  analog::DiodeClipper stage2, stage3;
  Biquad inputLowPass, feedback2, feedback3, coupling2, coupling3, toneStack;
  double sustainGain = 0.0, stage2Gain = 47.0;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> level;
  juce::SmoothedValue<float> fuzz, tone;
  bool primed = false;
};

/** Overdrive modelled on the Tube Screamer circuit: the op-amp amplifies only
   what's above 720 Hz, by 12 to 118 times, into diodes in its feedback loop, and
   the clean signal rides on top at unity. So low frequencies stay clean and
   relatively quiet, which is where its mid hump comes from. Then the tone
   section's 723 Hz low-pass and active treble control. Drive and Tone follow the
   real pots, so all of this holds at the real knob positions. */
class OverdrivePedal {
public:
  struct Settings {
    bool on = false;
    float drive = 0.5f; // 0..1
    float tone = 0.5f;  // 0 dark .. 1 bright
    float levelDb = 0.0f;
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double rate = 48000.0; // the oversampled rate
  analog::Oversampler oversampler;
  analog::DiodeClipper clipper;
  Biquad coupling, clipHighPass, clipLowPass, toneFilter;
  double feedbackOhms = 51.0e3;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> level;
  juce::SmoothedValue<float> drive, tone;
  bool primed = false;
};

/** Distortion modelled on the ProCo RAT circuit: an LM308 whose gain runs from
   unity to 180 times, and 13 times more above 1.5 kHz, as far as the slow op-amp
   can deliver it (only 30 dB at 10 kHz, which is why more distortion is also
   smoother). Then silicon diodes to ground and the Filter pot's low-pass, from
   475 Hz to 32 kHz. With the Distortion knob down it is nearly clean. */
class DistortionPedal {
public:
  struct Settings {
    bool on = false;
    float distortion = 0.6f; // 0..1
    float tone = 0.5f;       // 0 dark .. 1 bright (the real Filter knob reversed)
    float levelDb = 0.0f;
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double rate = 48000.0; // the oversampled rate
  analog::Oversampler oversampler;
  analog::ThirdOrderFilter gainStage;
  analog::DiodeClipper clipper;
  Biquad toneLowPass;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> level;
  juce::SmoothedValue<float> distortion, tone;
  bool primed = false;
};

/** Clean boost with bass and treble shelves, to hit the amp harder or lift a
   solo. Linear: it adds level, not distortion of its own. */
class BoostPedal {
public:
  struct Settings {
    bool on = false;
    float gainDb = 9.0f;   // 0..20
    float bassDb = 0.0f;   // +/-12, shelf at 120 Hz
    float trebleDb = 0.0f; // +/-12, shelf at 2.5 kHz
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  Biquad bassShelf, trebleShelf;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain;
  juce::SmoothedValue<float> bass, treble;
  bool primed = false;
};

/** Single-voice chorus: a 4-10 ms delay swept by a sine LFO, darkened a little
   like a bucket-brigade line, mixed with the dry signal. */
class ChorusPedal {
public:
  struct Settings {
    bool on = false;
    float rateHz = 0.8f;
    float depth = 0.5f; // 0..1
    float mix = 0.5f;   // 0 dry .. 1 wet
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  DelayLine line;
  OnsetRamp onset;
  double phase = 0.0;
  Biquad wetLowPass;
  juce::SmoothedValue<float> rate, depth, mix;
  bool primed = false;
};

/** Echo with a darkening, softly saturating feedback loop. Moving the time
   knob glides the delay like a tape echo instead of jumping. The echoes are
   added on top of the unchanged dry signal. */
class DelayPedal {
public:
  static constexpr float maxTimeMs = 2000.0f;

  struct Settings {
    bool on = false;
    float timeMs = 380.0f;
    float feedback = 0.35f; // 0..0.95
    float mix = 0.35f;      // echo level relative to the dry signal
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  DelayLine line;
  OnsetRamp onset;
  float delaySamples = 0.0f, glideCoeff = 0.0f;
  Biquad loopLowPass, loopHighPass;
  juce::SmoothedValue<float> feedback, mix;
  bool primed = false;
};
/** Four-stage phaser, the Phase 90 recipe: the signal through four swept
   first-order all-passes is mixed equally with the dry signal, which cuts two
   notches that move with the LFO. Feedback around the stages sharpens them
   into resonant peaks. */
class PhaserPedal {
public:
  struct Settings {
    bool on = false;
    float rateHz = 0.5f;
    float depth = 0.7f;    // 0..1
    float feedback = 0.3f; // 0..1
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  std::array<float, 4> stages{};
  float last = 0.0f;
  double phase = 0.0;
  juce::SmoothedValue<float> rate, depth, feedback;
  bool primed = false;
};

/** Flanger: a 0.7-7 ms delay swept by a slow LFO and mixed equally with the
   dry signal, so the comb filter's notches sweep through the whole spectrum.
   Feedback (regeneration) makes it ring like a jet. */
class FlangerPedal {
public:
  struct Settings {
    bool on = false;
    float rateHz = 0.25f;
    float depth = 0.7f;    // 0..1
    float feedback = 0.5f; // 0..1
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  DelayLine line;
  OnsetRamp onset;
  double phase = 0.0;
  juce::SmoothedValue<float> rate, depth, feedback;
  bool primed = false;
};

/** Tremolo: the volume pulses with an LFO that Shape squares off, from a
   smooth throb like an optical tremolo to a hard chop. */
class TremoloPedal {
public:
  struct Settings {
    bool on = false;
    float rateHz = 5.0f;
    float depth = 0.6f; // 0..1
    float shape = 0.3f; // 0 sine .. 1 square
  };

  void prepare(double sampleRate);
  void reset() noexcept;
  void process(float *data, int n, const Settings &s) noexcept;

private:
  double sampleRate = 48000.0;
  double phase = 0.0;
  juce::SmoothedValue<float> rate, depth, shape;
  bool primed = false;
};
} // namespace evil
