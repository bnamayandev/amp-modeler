"""Generate a reamp test signal for capturing your own amp or pedal.

    python -m evilamp.capture_signal capture/ --di my_guitar_di.wav

Writes capture/capture_input.wav (48 kHz, 24-bit) and capture/capture.json. Play the WAV
out of your interface through a reamp box into the device, record the device's output on
another input at the same sample rate (48 kHz), and train:

    python -m evilamp.train --capture-meta capture/capture.json --capture-output recorded.wav \\
        --out my_amp.nam --name "My amp" --gear-make ... --gear-model ...

Layout of the signal:
    silence | 4 calibration blips | train region | validation region | test region | silence

The blips let the trainer measure your interface's round-trip latency exactly. The train,
validation and test regions use different random content, and the test region is never
seen in training, so its ESR is an honest held-out score. Real guitar DI (--di) makes a
far better capture than synthetic material alone: record 3+ minutes of varied playing
(chords, single notes, palm mutes, dynamics) with your guitar straight into the interface.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal

from . import audio

RATE = 48000


def _sweep(rng, seconds: float, level_db: float) -> np.ndarray:
    t = np.arange(int(seconds * RATE)) / RATE
    x = signal.chirp(t, 40.0, t[-1], 18000.0, method="logarithmic")
    return x * 10 ** (level_db / 20) * np.hanning(len(x)) ** 0.1


def _noise_bursts(rng, seconds: float) -> np.ndarray:
    """Band-limited noise with random levels and envelopes, guitar-ish spectrum."""
    n = int(seconds * RATE)
    sos = signal.butter(2, [70, 6000], "bandpass", fs=RATE, output="sos")
    x = signal.sosfilt(sos, rng.standard_normal(n))
    x /= np.max(np.abs(x))
    env = np.zeros(n)
    pos = 0
    while pos < n:
        length = int(rng.uniform(0.15, 1.2) * RATE)
        level = 10 ** (rng.uniform(-40, -3) / 20)
        attack = np.minimum(1.0, np.arange(length) / (0.004 * RATE))
        decay = np.exp(-np.arange(length) / (rng.uniform(0.05, 0.8) * RATE))
        env[pos : pos + length] = (level * attack * decay)[: n - pos]
        pos += length + int(rng.uniform(0.0, 0.2) * RATE)
    return x * env


def _plucks(rng, seconds: float) -> np.ndarray:
    """Karplus-Strong plucked strings: single notes and chords at varied dynamics."""
    n = int(seconds * RATE)
    out = np.zeros(n)
    pos = 0
    while pos < n:
        length = int(rng.uniform(0.3, 2.0) * RATE)
        voices = rng.choice([1, 1, 2, 3, 4, 6])
        level = 10 ** (rng.uniform(-30, -2) / 20)
        note = np.zeros(length)
        for _ in range(voices):
            freq = 82.41 * 2 ** (rng.integers(0, 30) / 12)
            period = max(2, int(RATE / freq))
            buf = rng.uniform(-1, 1, period)
            y = np.empty(length)
            for i in range(length):
                y[i] = buf[i % period]
                buf[i % period] = 0.996 * 0.5 * (buf[i % period] + buf[(i + 1) % period])
            note += y
        note *= level / max(np.max(np.abs(note)), 1e-9)
        out[pos : pos + length] += note[: n - pos]
        pos += int(length * rng.uniform(0.6, 1.0))
    return out


def _region(rng, seconds: float, di: np.ndarray | None) -> np.ndarray:
    parts = [
        _sweep(rng, 4.0, -18),
        _sweep(rng, 4.0, -6),
        _noise_bursts(rng, seconds * 0.3),
        _plucks(rng, seconds * 0.4),
    ]
    if di is not None and len(di):
        parts.append(di)
    gap = np.zeros(int(0.25 * RATE))
    x = np.concatenate([np.concatenate([p, gap]) for p in parts])
    return np.clip(x, -0.98, 0.98)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out_dir")
    p.add_argument("--di", help="your own guitar DI recording(s), split across train/val/test", nargs="*", default=[])
    p.add_argument("--train-seconds", type=float, default=150.0, help="synthetic material in the train region")
    p.add_argument("--seed", type=int, default=2026)
    args = p.parse_args()

    rng = np.random.default_rng(args.seed)
    di = np.concatenate([audio.load_mono(f, RATE)[0] for f in args.di]) if args.di else None
    # Split the DI 80/10/10 so validation and test contain real playing the model never saw.
    di_parts = (None, None, None)
    if di is not None:
        a, b = int(len(di) * 0.8), int(len(di) * 0.9)
        di_parts = (di[:a], di[a:b], di[b:])

    lead = np.zeros(RATE)
    blips, blip_section = [], []
    for k in range(4):
        click = np.zeros(RATE // 2)
        click[0] = 0.5
        blips.append(len(lead) + sum(len(c) for c in blip_section))
        blip_section.append(click)
    regions = {
        "train": _region(rng, args.train_seconds, di_parts[0]),
        "val": _region(rng, max(20.0, args.train_seconds * 0.15), di_parts[1]),
        "test": _region(rng, max(20.0, args.train_seconds * 0.15), di_parts[2]),
    }

    pieces = [lead, *blip_section]
    segments = {}
    pos = sum(len(x) for x in pieces)
    for name, x in regions.items():
        segments[name] = [pos, pos + len(x)]
        pieces.append(x)
        pos += len(x)
    pieces.append(np.zeros(RATE))
    x = np.concatenate(pieces)

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    audio.write_wav(out / "capture_input.wav", x, RATE, subtype="PCM_24")
    meta = {
        "signal_file": "capture_input.wav",
        "sample_rate": RATE,
        "blips": blips,
        "segments": segments,
        "seconds": len(x) / RATE,
    }
    (out / "capture.json").write_text(json.dumps(meta, indent=2))
    print(f"wrote {out / 'capture_input.wav'} ({len(x) / RATE:.1f} s) and capture.json")
    for name, (a, b) in segments.items():
        print(f"  {name:>5}: {(b - a) / RATE:6.1f} s")


if __name__ == "__main__":
    main()
