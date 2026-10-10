"""Train a NAM-compatible WaveNet on a reamp capture and export a .nam file.

Two ways to give it data:

1. Separate, already-aligned pairs (e.g. a published dataset):
     python -m evilamp.train --train-input in.wav --train-target out.wav \
         --val-input v_in.wav --val-target v_out.wav --test-input t_in.wav --test-target t_out.wav ...

2. One reamp of the signal made by `python -m evilamp.capture_signal` (your own capture):
     python -m evilamp.train --capture-meta capture.json --capture-output recorded.wav ...
   The JSON sidecar marks the latency blips and the train/validation/test regions, so the
   latency is measured from the blips and the held-out region is never trained on.

The held-out test ESR is printed, written to the report JSON, and stored in the model's
metadata (metadata.training.test_esr).
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

from . import audio
from .wavenet import ARCHITECTURES, NamMetadata, WaveNet, export_nam


def _pre_emphasis(x: torch.Tensor, coef: float) -> torch.Tensor:
    return x[:, 1:] - coef * x[:, :-1]


def _mrstft(y_hat: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    """Multi-resolution STFT loss (spectral convergence + log magnitude), auraloss defaults."""
    total = y.new_zeros(())
    for n_fft, hop, win in ((1024, 120, 600), (2048, 240, 1200), (512, 50, 240)):
        window = torch.hann_window(win, device=y.device)
        mag = (
            lambda s: torch.stft(s, n_fft, hop, win, window, return_complex=True)
            .abs()
            .clamp_min(1e-8)
        )  # noqa: E731
        m, m_hat = mag(y), mag(y_hat)
        total = (
            total
            + torch.linalg.norm(m - m_hat) / torch.linalg.norm(m)
            + F.l1_loss(torch.log(m_hat), torch.log(m))
        )
    return total / 3.0


def _detect_blip_delay(y: np.ndarray, blips: list[int], rate: int) -> list[int]:
    """Onset of the device's response to each calibration impulse, relative to the impulse."""
    delays = []
    pre = int(0.05 * rate)
    for pos in blips:
        noise = y[max(0, pos - pre) : pos]
        window = np.abs(y[pos : pos + int(0.05 * rate)])
        if len(window) == 0 or len(noise) == 0:
            continue
        threshold = max(10.0 * float(np.std(noise)), 0.1 * float(window.max()))
        above = np.nonzero(window > threshold)[0]
        if len(above):
            delays.append(int(above[0]))
    return delays


def _load_pairs(args) -> dict:
    rate = args.sample_rate
    if args.capture_meta:
        meta = json.loads(Path(args.capture_meta).read_text())
        signal_file = args.capture_input or str(
            Path(args.capture_meta).parent / meta["signal_file"]
        )
        x, x_rate = audio.load_mono(signal_file)
        y, y_rate = audio.load_mono(args.capture_output)
        if x_rate != meta["sample_rate"] or y_rate != meta["sample_rate"]:
            raise SystemExit(
                f"Capture must be recorded at the signal's rate ({meta['sample_rate']} Hz); got {y_rate} Hz. "
                "Set your interface to the same rate and re-record."
            )
        if args.delay == "auto":
            delays = _detect_blip_delay(y, meta["blips"], y_rate)
            if not delays:
                raise SystemExit(
                    "Could not find the calibration blips in the recording. Is the output level too low?"
                )
            spread = max(delays) - min(delays)
            if spread > 4:
                raise SystemExit(
                    f"Blip delays disagree by {spread} samples ({delays}); check the recording"
                )
            # Keep one sample of extra lag so the model never has to predict the future.
            delay = int(np.median(delays)) - 1
            print(f"Latency from blips: {delays} -> using {delay} samples")
        else:
            delay = int(args.delay)
        x, y = audio.align(x, y, delay)
        segs = meta["segments"]
        pairs = {k: (x[a:b], y[a:b]) for k, (a, b) in segs.items()}
        if x_rate != rate:
            pairs = {
                k: (audio.resample(a, x_rate, rate), audio.resample(b, x_rate, rate))
                for k, (a, b) in pairs.items()
            }
        return {"pairs": pairs, "delay": delay}

    def load(inp, tgt):
        xi, _ = audio.load_mono(inp, rate)
        yi, _ = audio.load_mono(tgt, rate)
        if len(xi) != len(yi):
            print(f"warning: {inp} and {tgt} differ in length; trimming to the shorter")
        estimate = audio.estimate_delay(xi, yi)
        delay = estimate if args.delay == "auto" else int(args.delay)
        print(
            f"{Path(tgt).name}: cross-correlation lag {estimate} samples, using {delay}"
        )
        return audio.align(xi, yi, delay), delay

    train, delay = load(args.train_input, args.train_target)
    pairs = {"train": train}
    if args.val_input:
        pairs["val"] = load(args.val_input, args.val_target)[0]
    if args.test_input:
        pairs["test"] = load(args.test_input, args.test_target)[0]
    # Carve missing held-out sets off the end of the training pair, never overlapping it.
    n = len(train[0])
    if "test" not in pairs:
        cut = int(n * 0.9)
        pairs["test"] = (train[0][cut:], train[1][cut:])
        pairs["train"] = train = (train[0][:cut], train[1][:cut])
        n = cut
    if "val" not in pairs:
        cut = int(n * 0.9)
        pairs["val"] = (train[0][cut:], train[1][cut:])
        pairs["train"] = (train[0][:cut], train[1][:cut])
    return {"pairs": pairs, "delay": delay}


def _evaluate(
    model: WaveNet, x: torch.Tensor, y: np.ndarray
) -> tuple[float, np.ndarray]:
    model.eval()
    prev = torch.backends.cudnn.allow_tf32
    torch.backends.cudnn.allow_tf32 = False  # full fp32 for reported numbers
    try:
        pred = model.process_long(x).double().cpu().numpy()
    finally:
        torch.backends.cudnn.allow_tf32 = prev
    model.train()
    return audio.esr(y, pred), pred


def main(argv=None) -> None:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    g = p.add_argument_group("data (pairs)")
    for split in ("train", "val", "test"):
        g.add_argument(f"--{split}-input")
        g.add_argument(f"--{split}-target")
    g = p.add_argument_group("data (own capture)")
    g.add_argument("--capture-meta", help="JSON written by evilamp.capture_signal")
    g.add_argument("--capture-input", help="override the signal file named in the JSON")
    g.add_argument("--capture-output", help="the device's recorded output")
    p.add_argument(
        "--delay",
        default="0",
        help="output latency in samples, or 'auto' (default 0: pre-aligned)",
    )
    p.add_argument(
        "--sample-rate",
        type=int,
        default=48000,
        help="rate the model is trained and runs at",
    )
    p.add_argument("--arch", choices=sorted(ARCHITECTURES), default="standard")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--batch-size", type=int, default=16)
    p.add_argument(
        "--segment", type=int, default=8192, help="output samples per training example"
    )
    p.add_argument("--lr", type=float, default=0.004)
    p.add_argument(
        "--lr-decay", type=float, default=0.007, help="per-epoch exponential decay"
    )
    p.add_argument("--mrstft-weight", type=float, default=2e-4)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--out", required=True, help="output .nam path")
    p.add_argument("--report", help="JSON report path (default: next to --out)")
    p.add_argument(
        "--renders",
        help="directory for held-out input/target/prediction WAVs (for A/B)",
    )
    m = p.add_argument_group("metadata")
    m.add_argument("--name", default="")
    m.add_argument("--modeled-by", default="")
    m.add_argument(
        "--gear-type",
        default="amp",
        choices=[
            "amp",
            "pedal",
            "pedal_amp",
            "amp_cab",
            "amp_pedal_cab",
            "preamp",
            "studio",
        ],
    )
    m.add_argument("--gear-make", default="")
    m.add_argument("--gear-model", default="")
    m.add_argument(
        "--tone-type",
        default="",
        choices=["", "clean", "overdrive", "crunch", "hi_gain", "fuzz"],
    )
    args = p.parse_args(argv)

    if not args.capture_meta and not (args.train_input and args.train_target):
        p.error("give --train-input/--train-target or --capture-meta/--capture-output")
    if args.capture_meta and not args.capture_output:
        p.error("--capture-meta needs --capture-output")

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device(args.device)

    data = _load_pairs(args)
    pairs = data["pairs"]
    for k, (a, _) in pairs.items():
        print(f"{k:>5}: {len(a) / args.sample_rate:7.1f} s")

    model = WaveNet(ARCHITECTURES[args.arch]()).to(device)
    rf = model.receptive_field
    n_params = sum(t.numel() for t in model.parameters())
    print(f"WaveNet '{args.arch}': {n_params} parameters, receptive field {rf} samples")

    to_t = lambda a: torch.tensor(a, dtype=torch.float32, device=device)  # noqa: E731
    x_train, y_train = map(to_t, pairs["train"])
    x_val, y_val = to_t(pairs["val"][0]), pairs["val"][1]
    x_test, y_test = to_t(pairs["test"][0]), pairs["test"][1]

    window = args.segment + rf - 1
    max_start = len(x_train) - window
    if max_start <= 0:
        raise SystemExit("Training audio is shorter than one example")
    steps_per_epoch = max(1, math.ceil(len(x_train) / (args.segment * args.batch_size)))
    offsets = torch.arange(window, device=device)

    optimiser = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = torch.optim.lr_scheduler.ExponentialLR(
        optimiser, gamma=1.0 - args.lr_decay
    )

    best_esr, best_state, best_epoch = math.inf, None, -1
    history = []
    t0 = time.time()
    for epoch in range(1, args.epochs + 1):
        running = 0.0
        for _ in range(steps_per_epoch):
            starts = torch.randint(0, max_start + 1, (args.batch_size,), device=device)
            idx = starts[:, None] + offsets
            xb = x_train[idx]
            yb = y_train[idx][:, rf - 1 :]
            pred = model(xb)
            loss = F.mse_loss(pred, yb)
            if args.mrstft_weight > 0:
                loss = loss + args.mrstft_weight * _mrstft(
                    _pre_emphasis(pred, 0.85), _pre_emphasis(yb, 0.85)
                )
            optimiser.zero_grad(set_to_none=True)
            loss.backward()
            optimiser.step()
            running += loss.item()
        scheduler.step()
        val_esr, _ = _evaluate(model, x_val, y_val)
        history.append(
            {
                "epoch": epoch,
                "train_loss": running / steps_per_epoch,
                "val_esr": val_esr,
            }
        )
        marker = ""
        if val_esr < best_esr:
            best_esr, best_epoch = val_esr, epoch
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
            marker = "  *"
        print(
            f"epoch {epoch:4d}  loss {running / steps_per_epoch:.6f}  val ESR {val_esr:.5f}{marker}",
            flush=True,
        )

    model.load_state_dict(best_state)
    val_esr, _ = _evaluate(model, x_val, y_val)
    test_esr, test_pred = _evaluate(model, x_test, y_test)
    loudness = audio.rms_db(test_pred)
    elapsed = time.time() - t0

    report = {
        "architecture": args.arch,
        "parameters": n_params,
        "receptive_field": rf,
        "sample_rate": args.sample_rate,
        "delay_samples": data["delay"],
        "epochs": args.epochs,
        "best_epoch": best_epoch,
        "validation_esr": val_esr,
        "test_esr": test_esr,
        "seconds": {k: len(a) / args.sample_rate for k, (a, _) in pairs.items()},
        "loudness_db": loudness,
        "training_time_s": elapsed,
        "history": history,
    }
    meta = NamMetadata(
        name=args.name,
        modeled_by=args.modeled_by,
        gear_type=args.gear_type,
        gear_make=args.gear_make,
        gear_model=args.gear_model,
        tone_type=args.tone_type,
        loudness=loudness,
        training={
            "trainer": "evil-amplifier evilamp.train",
            "validation_esr": val_esr,
            "test_esr": test_esr,
            "best_epoch": best_epoch,
            "delay_samples": data["delay"],
        },
    )
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    export_nam(model.cpu(), out, args.sample_rate, meta)
    report_path = Path(args.report) if args.report else out.with_suffix(".report.json")
    report_path.write_text(json.dumps(report, indent=2))

    if args.renders:
        d = Path(args.renders)
        d.mkdir(parents=True, exist_ok=True)
        audio.write_wav(d / "test-input.wav", pairs["test"][0], args.sample_rate)
        audio.write_wav(d / "test-target.wav", y_test, args.sample_rate)
        audio.write_wav(d / "test-prediction.wav", test_pred, args.sample_rate)

    print(
        f"\nbest epoch {best_epoch}: validation ESR {val_esr:.5f}, held-out test ESR {test_esr:.5f}"
    )
    print(f"wrote {out} and {report_path} ({elapsed / 60:.1f} min)")


if __name__ == "__main__":
    main()
