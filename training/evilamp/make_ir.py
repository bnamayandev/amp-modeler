"""Generate the bundled default cabinet impulse response.

This is a synthetic, minimum-phase approximation of a closed-back 4x12 with a close mic:
low-end resonance around 110 Hz, a lower-mid scoop, presence peaks at 2-4 kHz and a steep
speaker roll-off above ~5 kHz. It exists so the plugin sounds reasonable out of the box
without redistributing someone else's IR. Load a real IR for real use.

    python -m evilamp.make_ir ../resources/irs/EvilCab4x12.wav
"""

from __future__ import annotations

import argparse

import numpy as np
from scipy import signal

from .audio import write_wav


def _peaking(f0: float, gain_db: float, q: float, fs: float) -> np.ndarray:
    a = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / fs
    alpha = np.sin(w0) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w0), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w0), 1 - alpha / a]
    return signal.tf2sos(b, den)


def make_ir(fs: int = 48000, length: int = 4096) -> np.ndarray:
    sos = np.vstack(
        [
            signal.butter(2, 75, "highpass", fs=fs, output="sos"),
            _peaking(110, 5.0, 1.4, fs),  # cabinet/speaker resonance
            _peaking(450, -3.5, 0.9, fs),  # lower-mid scoop
            _peaking(2200, 4.0, 1.2, fs),  # presence
            _peaking(3800, 3.0, 2.0, fs),  # cone edge bump
            signal.butter(4, 5200, "lowpass", fs=fs, output="sos"),
            signal.butter(2, 7500, "lowpass", fs=fs, output="sos"),
        ]
    )
    impulse = np.zeros(length)
    impulse[0] = 1.0
    ir = signal.sosfilt(sos, impulse)
    # A weak reflection from the cabinet back panel, ~0.6 ms behind the direct sound.
    delay = int(0.0006 * fs)
    ir[delay:] += (
        0.18
        * signal.sosfilt(signal.butter(2, 1500, "lowpass", fs=fs, output="sos"), ir)[
            : length - delay
        ]
    )
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
