"""Write golden data for the C++ parity test (tests/test_nam_parity.cpp).

A small randomly initialised WaveNet is exported to .nam and run in PyTorch on a test
signal. The C++ test loads the same .nam through NeuralAmpModelerCore and must reproduce
the PyTorch output, proving the exporter's weight layout matches NAM Core's.

    python -m evilamp.golden ../tests/data
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch

from .audio import write_wav
from .wavenet import LayerArrayConfig, NamMetadata, WaveNet, WaveNetConfig, export_nam


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("out_dir")
    args = p.parse_args()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    torch.manual_seed(1234)
    config = WaveNetConfig(
        layers=[
            LayerArrayConfig(1, 1, 3, 4, 3, [1, 2, 4, 8], head_bias=False),
            LayerArrayConfig(4, 1, 1, 3, 2, [16, 1, 32], head_bias=True),
        ],
        head_scale=0.25,
    )
    model = WaveNet(config)
    with torch.no_grad():  # larger weights than default init so every nonlinearity is exercised
        for prm in model.parameters():
            prm.mul_(2.0)

    rate = 48000
    n = 4800
    t = np.arange(n) / rate
    rng = np.random.default_rng(7)
    x = 0.6 * np.sin(2 * np.pi * 220 * t) * np.exp(-t * 8) + 0.2 * rng.standard_normal(n)
    x[: n // 10] = 0.0  # leading silence: exercises the post-prewarm state
    x = np.clip(x, -1, 1).astype(np.float32)

    with torch.no_grad():
        y = model.forward_causal(torch.from_numpy(x)[None])[0].numpy()

    export_nam(model, out / "parity_wavenet.nam", rate, NamMetadata(name="parity test", loudness=-20.0))
    write_wav(out / "parity_input.wav", x, rate)
    write_wav(out / "parity_output.wav", y, rate)
    print(f"wrote parity data to {out} (receptive field {model.receptive_field})")


if __name__ == "__main__":
    main()
