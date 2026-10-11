#pragma once

namespace evil::standalone {
/** Linux standalone on a PipeWire desktop: makes the audio settings' JACK
    entry go through PipeWire (no `pw-jack` needed) and lets its sample rate
    and buffer size menus actually change them. JUCE's own JACK backend can
    only report what the server runs at, so it offers a single fixed value.
    PipeWire instead takes a per-client request from PIPEWIRE_QUANTUM
    ("<frames>/<rate>"), read when the JACK client opens and dropped when it
    closes; quitting the app puts the desktop back on PipeWire's defaults.

    Call from the processor's constructor, before the audio device opens. A
    no-op outside the Linux standalone. */
void prepareAudioDevices();
} // namespace evil::standalone
