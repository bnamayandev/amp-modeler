#pragma once

#include <vector>

namespace evil::loudness {
/** How loud captures and IRs are, so they can all be played at the same level.

    A capture is measured as it loads, by playing a test guitar through it, and an
    IR by how much it raises or lowers that capture's level. Everything here runs
    at this rate, whatever the host's. */
inline constexpr double rate = 48000.0;

/** The level every capture plays at when levels are matched, as levelDb() reads
    the reference guitar through it: the built-in capture through the built-in
    cabinet, so the default sound is as loud with matching as without. */
inline constexpr double targetDb = -2.84;

/** Matching never turns anything up or down by more than this, so a capture that
    is all but silent, or broken, can't become a blast of noise. A quiet clean
    capture on TONE3000 can need 33 dB. */
inline constexpr double maxCorrectionDb = 40.0;

/** An open chord peaking at 1, every string a decaying stack of harmonics, its top
    end falling away faster: not a real guitar, but with its balance. The same every
    time, and at `rate`. */
const std::vector<float> &openChord();

/** What levels are compared on: that chord strummed at an everyday strength,
    between the tone meter's gentle and hard ones. */
const std::vector<float> &referenceGuitar();

/** The signal as ITU-R BS.1770's K-weighting hears it: less of the deepest lows
    and a little more of the top, which is roughly how loud an ear finds each
    frequency. At `rate`. */
std::vector<float> kWeighted(std::vector<float> signal);

/** The reference guitar played dry, K-weighted. */
const std::vector<float> &weightedReferenceGuitar();

/** Mean square level, in dB. */
double levelDb(const std::vector<float> &signal);

/** How much louder an impulse response makes a signal, in dB: below 0 if quieter.
    Both at `rate`. 0 for a silent signal. */
double gainThroughDb(const std::vector<float> &signal,
                     const std::vector<float> &impulseResponse);

/** An impulse response recorded at another rate, as a convolution plays it at
    `rate`: resampled, and scaled as juce::dsp::Convolution scales it, so it has
    the same frequency response. */
std::vector<float> atRate(const float *impulseResponse, int length, double sampleRate);
} // namespace evil::loudness
