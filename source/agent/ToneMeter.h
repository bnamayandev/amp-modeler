#pragma once

#include "Tools.h"
#include "dsp/SignalChain.h"

namespace evil::agent {
/** Plays a test guitar through a private copy of the chain with these settings, and
    again with every pedal and the EQ out of it, and measures both. Allocates and
    takes a moment: worker thread only. Modulation, delay and reverb are left out,
    as they'd only blur what's being measured. matchLevels as the amp has it, so the
    levels read as the player hears them. */
ToneReading measureTone(SignalChain::Settings, const juce::String &modelSource,
                        const juce::String &irSource, bool matchLevels);
} // namespace evil::agent
