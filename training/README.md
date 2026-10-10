# Training: capture an amp or pedal and train a model

Everything here runs offline in Python. The output is a standard `.nam` file that the plugin
(and the official NAM plugin) can load.

```bash
cd training
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt   # install the CUDA build of torch from pytorch.org if you have an NVIDIA GPU
```

## 1. Make the reamp signal

```bash
python -m evilamp.capture_signal capture/ --di my_guitar_di.wav
```

This writes `capture/capture_input.wav` (48 kHz, 24-bit) and `capture/capture.json`. The signal
holds four calibration blips, then train, validation and held-out test regions made of sweeps,
noise bursts and synthetic plucked strings, plus your guitar DI if you pass `--di`. Real DI
makes a much better capture: record 3+ minutes of varied playing (chords, single notes, palm
mutes, soft and hard picking) with the guitar straight into the interface, no effects. The
DI is split 80/10/10 across the regions so the test score covers real playing the model
never trained on.

## 2. Reamp the device

```
interface out ──► reamp box ──► amp/pedal input
amp line out / load box / mic'd cab ──► interface in
```

- Set the interface to **48 kHz** and keep it there for playback and recording.
- Play `capture_input.wav` and record the device's output **at the same time, in the same
  session**, starting before the file starts and stopping after it ends. Export the recording
  as WAV, trimmed to start exactly where playback started (most DAWs: record onto a track
  aligned with the playback track, then export that track over the same range).
- Levels: the device's output should peak around -6 dBFS and never clip. The reamp box
  should present the amp with roughly guitar level; the signal peaks at -0.2 dBFS.
- Each capture is one fixed setting of the amp's knobs. Write the settings down.
- A load box or the amp's line out captures the amp alone. Keep the plugin's cab IR on for
  that. If you mic a cabinet, the capture already includes the cab: turn the plugin's cab off.

## 3. Train

```bash
python -m evilamp.train --capture-meta capture/capture.json --capture-output recorded.wav \
    --delay auto --arch standard --epochs 200 \
    --out my_amp.nam --renders renders/ \
    --name "My amp, crunch channel" --modeled-by "me" --gear-type amp --gear-make Fender --gear-model "Blues Jr"
```

- `--delay auto` measures the round-trip latency from the blips (it prints all four; they
  should agree to within a sample). The trainer keeps one sample of extra delay, so the
  model never has to predict the future.
- Architectures are NAM's sizes: `standard` (13.8k weights, ~8% of a 128-sample deadline at
  48 kHz on a desktop CPU), `lite`, `feather`, `nano` (smaller, cheaper, less accurate).
- The loss is MSE plus a pre-emphasised multi-resolution STFT term. The best epoch by
  validation ESR is kept.
- At the end it prints **validation ESR** and **held-out test ESR**, and writes both into
  the model's metadata (the plugin shows the held-out ESR) and into `my_amp.report.json`
  along with per-epoch history.
- `--renders` writes `test-input.wav`, `test-target.wav` and `test-prediction.wav` for the
  held-out region: use the target and prediction for a blind A/B (ABX) listening test.

ESR (error-to-signal ratio) is `sum((target - prediction)^2) / sum(target^2)`. Roughly:
< 0.01 is very close, 0.01 to 0.05 is good, > 0.1 is audibly different. High-gain amps are
harder to model than clean ones.

## 4. Check it in C++ and load it

```bash
../build/evil-cli_artefacts/Release/evil-cli info my_amp.nam
../build/evil-cli_artefacts/Release/evil-cli esr renders/test-input.wav renders/test-target.wav --model my_amp.nam
```

`esr` runs the model through the plugin's exact C++ path. It should match the trainer's
held-out ESR to about four decimal places. Then, in the plugin: **Amp > Neural > Load .nam...**

## Already-aligned data

If you have input/target pairs that are already time-aligned (like a published dataset):

```bash
python -m evilamp.train --train-input in.wav --train-target out.wav \
    [--val-input ... --val-target ...] [--test-input ... --test-target ...] --out model.nam
```

Missing validation/test sets are carved off the end of the training pair, so they are never
trained on. Files at other sample rates are resampled to 48 kHz first.
`scripts/train_ht1.sh` reproduces the bundled Blackstar HT-1 model this way.

## Other tools

- `python -m evilamp.make_ir out.wav`: regenerates the synthetic default cab IR.
- `python -m evilamp.golden ../tests/data`: regenerates the PyTorch/NAM Core parity test data.

## Comparing against the official NAM trainer

The brief names the NAM trainer as the baseline. `pip install neural-amp-modeler` and train
the same capture with its `nam-full` command or GUI. Both produce `.nam` files, so you can
compare held-out ESR with `evil-cli esr` on identical test audio.
