"""Audio I/O, resampling, alignment and error metrics for capture data."""

from __future__ import annotations

from fractions import Fraction
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal


def load_mono(path: str | Path, target_rate: int | None = None) -> tuple[np.ndarray, int]:
    """Load a WAV as float64 mono. Multichannel files must be mono-compatible: channel 0 is used."""
    data, rate = sf.read(str(path), dtype="float64", always_2d=True)
    x = data[:, 0]
    if target_rate is not None and rate != target_rate:
        x = resample(x, rate, target_rate)
        rate = target_rate
    return x, rate


def resample(x: np.ndarray, from_rate: int, to_rate: int) -> np.ndarray:
    """Polyphase resampling with a Kaiser-windowed anti-aliasing filter (scipy default)."""
    ratio = Fraction(to_rate, from_rate)
    return signal.resample_poly(x, ratio.numerator, ratio.denominator)


def estimate_delay(x: np.ndarray, y: np.ndarray, max_delay: int = 2048) -> int:
    """Samples by which y lags x, from the cross-correlation peak of their first minute.

    Works on distorted output because the fundamental structure of the input survives the
    nonlinearity. Positive means the output is late (the usual case for a reamp loop).
    """
    n = min(len(x), len(y), 48000 * 60)
    a = x[:n] - x[:n].mean()
    b = y[:n] - y[:n].mean()
    corr = signal.correlate(b, a, mode="full", method="fft")
    lags = signal.correlation_lags(len(b), len(a), mode="full")
    window = np.abs(lags) <= max_delay
    return int(lags[window][np.argmax(np.abs(corr[window]))])


def align(x: np.ndarray, y: np.ndarray, delay: int) -> tuple[np.ndarray, np.ndarray]:
    """Shift y earlier by `delay` samples (or x, if negative) and trim both to equal length."""
    if delay > 0:
        y = y[delay:]
    elif delay < 0:
        x = x[-delay:]
    n = min(len(x), len(y))
    return x[:n], y[:n]


def esr(target: np.ndarray, prediction: np.ndarray) -> float:
    """Error-to-signal ratio: sum((y - y_hat)^2) / sum(y^2). 0 is perfect; 1 is as bad as silence."""
    target = np.asarray(target, dtype=np.float64)
    prediction = np.asarray(prediction, dtype=np.float64)
    return float(np.sum((target - prediction) ** 2) / max(np.sum(target**2), 1e-20))


def rms_db(x: np.ndarray) -> float:
    return float(20.0 * np.log10(max(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)), 1e-12)))


def write_wav(path: str | Path, x: np.ndarray, rate: int, subtype: str = "FLOAT") -> None:
    sf.write(str(path), np.asarray(x, dtype=np.float32), rate, subtype=subtype)
