"""NAM-compatible WaveNet in PyTorch, with .nam export and import.

The network matches the "A1" WaveNet that NeuralAmpModelerCore (NAM Core) runs: stacks of
non-gated dilated causal convolutions with tanh, a residual stream through each layer
array, and a skip ("head") stream summed over every layer. Weights are flattened in the
exact order NAM Core's ``set_weights_`` consumes them:

    for each layer array:
        rechannel 1x1 (no bias)
        for each layer: conv (K taps, bias), input_mixin 1x1 (no bias), layer1x1 (bias)
        head_rechannel 1x1 (bias if head_bias)
    head_scale

tests/test_nam_parity.cpp checks a model exported here against NAM Core sample by sample.
"""

from __future__ import annotations

import datetime as _dt
import json
from dataclasses import dataclass, field
from pathlib import Path

import torch
from torch import nn

NAM_FILE_VERSION = "0.5.4"  # NAM Core v0.6.0 fully supports 0.5.0 through 0.7.0

_LITE_DILATIONS_A = [1, 2, 4, 8, 16, 32, 64]
_LITE_DILATIONS_B = [128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
_STANDARD_DILATIONS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]


@dataclass
class LayerArrayConfig:
    input_size: int
    condition_size: int
    head_size: int
    channels: int
    kernel_size: int
    dilations: list[int]
    head_bias: bool
    activation: str = "Tanh"

    def to_json(self) -> dict:
        return {
            "input_size": self.input_size,
            "condition_size": self.condition_size,
            "head_size": self.head_size,
            "channels": self.channels,
            "kernel_size": self.kernel_size,
            "dilations": list(self.dilations),
            "activation": self.activation,
            "gated": False,
            "head_bias": self.head_bias,
        }


@dataclass
class WaveNetConfig:
    layers: list[LayerArrayConfig]
    head_scale: float = 0.02

    def to_json(self) -> dict:
        return {"layers": [la.to_json() for la in self.layers], "head": None, "head_scale": self.head_scale}

    @staticmethod
    def from_json(cfg: dict) -> "WaveNetConfig":
        if cfg.get("head") is not None:
            raise ValueError("Post-stack heads are not supported by this trainer")
        arrays = []
        for la in cfg["layers"]:
            if la.get("gated", False) or la.get("activation", "Tanh") != "Tanh":
                raise ValueError("Only non-gated Tanh WaveNets are supported by this trainer")
            if "head_size" not in la:
                raise ValueError("Only the legacy head_size/head_bias layer format is supported")
            arrays.append(
                LayerArrayConfig(
                    input_size=la["input_size"],
                    condition_size=la["condition_size"],
                    head_size=la["head_size"],
                    channels=la["channels"],
                    kernel_size=la["kernel_size"],
                    dilations=list(la["dilations"]),
                    head_bias=la["head_bias"],
                )
            )
        return WaveNetConfig(layers=arrays, head_scale=float(cfg["head_scale"]))


def _two_array_config(channels: int, head: int, dil_a: list[int], dil_b: list[int]) -> WaveNetConfig:
    return WaveNetConfig(
        layers=[
            LayerArrayConfig(1, 1, head, channels, 3, dil_a, head_bias=False),
            LayerArrayConfig(channels, 1, 1, head, 3, dil_b, head_bias=True),
        ]
    )


# Sizes follow the NAM trainer's named architectures.
ARCHITECTURES = {
    "standard": lambda: _two_array_config(16, 8, _STANDARD_DILATIONS, _STANDARD_DILATIONS),
    "lite": lambda: _two_array_config(12, 6, _LITE_DILATIONS_A, _LITE_DILATIONS_B),
    "feather": lambda: _two_array_config(8, 4, _LITE_DILATIONS_A, _LITE_DILATIONS_B),
    "nano": lambda: _two_array_config(4, 2, _LITE_DILATIONS_A, _LITE_DILATIONS_B),
}


class _Layer(nn.Module):
    def __init__(self, condition_size: int, channels: int, kernel_size: int, dilation: int):
        super().__init__()
        self.conv = nn.Conv1d(channels, channels, kernel_size, dilation=dilation, bias=True)
        self.input_mixin = nn.Conv1d(condition_size, channels, 1, bias=False)
        self.layer1x1 = nn.Conv1d(channels, channels, 1, bias=True)

    def forward(self, x: torch.Tensor, c: torch.Tensor, out_length: int):
        z = self.conv(x)
        z = torch.tanh(z + self.input_mixin(c[:, :, -z.shape[2] :]))
        return x[:, :, -z.shape[2] :] + self.layer1x1(z), z[:, :, -out_length:]


class _LayerArray(nn.Module):
    def __init__(self, cfg: LayerArrayConfig):
        super().__init__()
        self.cfg = cfg
        self.rechannel = nn.Conv1d(cfg.input_size, cfg.channels, 1, bias=False)
        self.layers = nn.ModuleList(
            _Layer(cfg.condition_size, cfg.channels, cfg.kernel_size, d) for d in cfg.dilations
        )
        self.head_rechannel = nn.Conv1d(cfg.channels, cfg.head_size, 1, bias=cfg.head_bias)

    @property
    def receptive_field(self) -> int:
        return 1 + (self.cfg.kernel_size - 1) * sum(self.cfg.dilations)

    def forward(self, x, c, head_input):
        out_length = x.shape[2] - (self.receptive_field - 1)
        x = self.rechannel(x)
        for layer in self.layers:
            x, head_term = layer(x, c, out_length)
            head_input = head_term if head_input is None else head_input[:, :, -out_length:] + head_term
        return self.head_rechannel(head_input), x

    def flat_weights(self) -> list[torch.Tensor]:
        w = [self.rechannel.weight.flatten()]
        for layer in self.layers:
            w += [
                layer.conv.weight.flatten(),
                layer.conv.bias.flatten(),
                layer.input_mixin.weight.flatten(),
                layer.layer1x1.weight.flatten(),
                layer.layer1x1.bias.flatten(),
            ]
        w.append(self.head_rechannel.weight.flatten())
        if self.cfg.head_bias:
            w.append(self.head_rechannel.bias.flatten())
        return w

    def parameters_in_export_order(self) -> list[torch.Tensor]:
        p = [self.rechannel.weight]
        for layer in self.layers:
            p += [layer.conv.weight, layer.conv.bias, layer.input_mixin.weight, layer.layer1x1.weight, layer.layer1x1.bias]
        p.append(self.head_rechannel.weight)
        if self.cfg.head_bias:
            p.append(self.head_rechannel.bias)
        return p


class WaveNet(nn.Module):
    """Input (batch, samples) -> output (batch, samples - receptive_field + 1)."""

    def __init__(self, config: WaveNetConfig):
        super().__init__()
        for prev, nxt in zip(config.layers, config.layers[1:]):
            if nxt.channels != prev.head_size:
                raise ValueError("Each layer array's channels must equal the previous array's head_size")
            if nxt.input_size != prev.channels:
                raise ValueError("Each layer array's input_size must equal the previous array's channels")
        if config.layers[-1].head_size != 1:
            raise ValueError("The last layer array must have head_size 1 (mono output)")
        self.config = config
        self.layer_arrays = nn.ModuleList(_LayerArray(la) for la in config.layers)
        self.head_scale = config.head_scale

    @property
    def receptive_field(self) -> int:
        return 1 + sum(la.receptive_field - 1 for la in self.layer_arrays)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = x[:, None, :]
        y, head_input = x, None
        for la in self.layer_arrays:
            head_input, y = la(y, x, head_input)
        return self.head_scale * head_input[:, 0, :]

    def forward_causal(self, x: torch.Tensor) -> torch.Tensor:
        """Same length in and out; history before the first sample is silence (as after NAM's prewarm)."""
        pad = torch.zeros(x.shape[0], self.receptive_field - 1, dtype=x.dtype, device=x.device)
        return self(torch.cat([pad, x], dim=1))

    @torch.no_grad()
    def process_long(self, x: torch.Tensor, chunk: int = 1 << 18) -> torch.Tensor:
        """Causal forward over a long 1-D signal in chunks, to bound memory."""
        rf = self.receptive_field
        padded = torch.cat([torch.zeros(rf - 1, dtype=x.dtype, device=x.device), x])
        out = []
        for start in range(0, x.numel(), chunk):
            stop = min(start + chunk, x.numel())
            out.append(self(padded[start : stop + rf - 1][None])[0])
        return torch.cat(out)

    def export_weights(self) -> list[float]:
        flat = []
        for la in self.layer_arrays:
            flat += la.flat_weights()
        weights = torch.cat([w.detach().float().cpu() for w in flat]).tolist()
        weights.append(float(self.head_scale))
        return weights

    def import_weights(self, weights: list[float]) -> None:
        params = [p for la in self.layer_arrays for p in la.parameters_in_export_order()]
        expected = sum(p.numel() for p in params) + 1
        if len(weights) != expected:
            raise ValueError(f"Weight count mismatch: file has {len(weights)}, architecture needs {expected}")
        flat = torch.tensor(weights[:-1], dtype=torch.float32)
        offset = 0
        with torch.no_grad():
            for p in params:
                n = p.numel()
                p.copy_(flat[offset : offset + n].view_as(p))
                offset += n
        self.head_scale = float(weights[-1])


@dataclass
class NamMetadata:
    name: str = ""
    modeled_by: str = ""
    gear_type: str = "amp"
    gear_make: str = ""
    gear_model: str = ""
    tone_type: str = ""
    loudness: float | None = None
    training: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        now = _dt.datetime.now()
        meta = {
            "date": {
                "year": now.year,
                "month": now.month,
                "day": now.day,
                "hour": now.hour,
                "minute": now.minute,
                "second": now.second,
            },
            "name": self.name,
            "modeled_by": self.modeled_by,
            "gear_type": self.gear_type,
            "gear_make": self.gear_make,
            "gear_model": self.gear_model,
            "tone_type": self.tone_type,
            "training": self.training,
        }
        if self.loudness is not None:
            meta["loudness"] = self.loudness
        return meta


def export_nam(model: WaveNet, path: str | Path, sample_rate: int, metadata: NamMetadata) -> None:
    doc = {
        "version": NAM_FILE_VERSION,
        "metadata": metadata.to_json(),
        "architecture": "WaveNet",
        "config": model.config.to_json(),
        "weights": model.export_weights(),
        "sample_rate": sample_rate,
    }
    Path(path).write_text(json.dumps(doc))


def load_nam(path: str | Path) -> tuple[WaveNet, dict]:
    doc = json.loads(Path(path).read_text())
    if doc["architecture"] != "WaveNet":
        raise ValueError(f"Unsupported architecture {doc['architecture']}")
    model = WaveNet(WaveNetConfig.from_json(doc["config"]))
    model.import_weights(doc["weights"])
    return model, doc
