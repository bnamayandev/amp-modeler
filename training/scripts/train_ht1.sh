#!/usr/bin/env bash
# Reproduces resources/models/BlackstarHT1.nam: downloads the Blackstar HT-1 reamp dataset
# (Wright et al., Aalto University, GPL-3.0) and trains the standard NAM WaveNet on it.
# ~20 minutes on an RTX 3070; hours on CPU.
set -euo pipefail
cd "$(dirname "$0")/.."

data=data/ht1
base=https://raw.githubusercontent.com/Alec-Wright/Automated-GuitarAmpModelling/HEAD/Data
mkdir -p "$data"
for split in train val test; do
  for kind in input target; do
    [ -f "$data/$split-$kind.wav" ] || curl -sSfL -o "$data/$split-$kind.wav" "$base/$split/ht1-$kind.wav"
  done
done

python -m evilamp.train \
  --train-input "$data/train-input.wav" --train-target "$data/train-target.wav" \
  --val-input "$data/val-input.wav" --val-target "$data/val-target.wav" \
  --test-input "$data/test-input.wav" --test-target "$data/test-target.wav" \
  --arch standard --epochs 200 \
  --out runs/ht1/BlackstarHT1.nam --renders runs/ht1/renders \
  --name "Blackstar HT-1" --modeled-by "Evil Amplifier (Aalto HT-1 dataset)" \
  --gear-type amp --gear-make Blackstar --gear-model "HT-1" "$@"
