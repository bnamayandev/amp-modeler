"""Generate the bundled default cabinet impulse response.

This is a synthetic, minimum-phase approximation of a closed-back 4x12 with a dynamic mic close
to the cone. It isn't a recording of any cabinet: it exists so the plugin sounds right out of the
box without redistributing someone else's IR. Load a real IR for real use.

Its shape follows measurements of real speaker and SM57 IRs, in dB relative to 1 kHz:

- rolls off below about 100 Hz: -10 dB at 50 Hz
- 2 to 3 dB of low-mid body from 200 to 500 Hz, as a real closed cabinet has. Carving this out
  makes a cab thin and leaves the presence peak to sound sharp
- a presence peak of about 4 dB at 2 to 2.5 kHz, the top of the cone's usable range
- falls fast above 5 kHz: -7 dB at 6.3 kHz, -14 dB at 8 kHz, -21 dB at 10 kHz

Fine peaks and dips above 1 kHz stand in for cone break-up, which is never smooth, and a short
reflection adds the slight comb of a mic in front of a speaker. Without them it sounds like an
EQ curve rather than a speaker.

    python -m evilamp.make_ir ../resources/irs/EvilCab4x12.wav
"""

from __future__ import annotations

import argparse

import numpy as np
from scipy import signal

from .audio import write_wav

# (centre Hz, gain dB, Q)
_BROAD = [
    (105, 0.15, 1.6),  # cabinet resonance
    (300, 0.74, 0.7),  # low-mid body
    (800, -0.34, 1.2),
    (2700, 1.68, 1.3),  # presence
]
_BREAKUP = [
    (1350, -1.2, 5.0),
    (1900, 1.2, 6.0),
    (2750, 1.5, 7.0),
    (3600, -1.5, 6.0),
    (4400, 1.5, 8.0),
    (5300, -2.0, 6.0),
    (6600, 2.0, 7.0),
    (8200, -2.5, 6.0),
]


def _peaking(f0: float, gain_db: float, q: float, fs: float) -> np.ndarray:
    a = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / fs
    alpha = np.sin(w0) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w0), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w0), 1 - alpha / a]
    return signal.tf2sos(b, den)


def make_ir(fs: int = 48000, length: int = 4096) -> np.ndarray:
    sections = [signal.butter(2, 95, "highpass", fs=fs, output="sos")]
    sections += [_peaking(*band, fs) for band in _BROAD + _BREAKUP]
    sections += [
        signal.butter(3, 4800, "lowpass", fs=fs, output="sos"),
        signal.butter(2, 19500, "lowpass", fs=fs, output="sos"),
    ]
    sos = np.vstack(sections)
    impulse = np.zeros(length)
    impulse[0] = 1.0
    reflection = int(round(0.00042 * fs))
    impulse[reflection] = 0.12
    ir = signal.sosfilt(sos, impulse)
    fade = int(0.25 * length)
    ir[-fade:] *= np.hanning(2 * fade)[fade:]
    return ir / np.max(np.abs(ir)) * 0.9


def main() -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("out")
    p.add_argument("--rate", type=int, default=48000)
    args = p.parse_args()
    write_wav(args.out, make_ir(args.rate), args.rate, subtype="PCM_24")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
