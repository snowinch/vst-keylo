#!/usr/bin/env python3
"""
Supervised fine-tune of S-KEY (Deezer ChromaNet) on labeled trap dataset.

Loads skey.pt pre-trained checkpoint, freezes VQT (feature extractor),
fine-tunes only ChromaNet with cross-entropy on our edma labels.

Key index mapping:
  Our system  (chromanet.py): C=0..B=11 major, C=12..B=23 minor
  S-KEY system (key_map):     A=0..G#=11 major, B=12..Bb=23 minor

Usage:
    python 04_finetune_skey.py [--epochs N] [--lr FLOAT] [--batch-size N]
"""

import argparse
import csv
import random
import sys
from pathlib import Path

import torch
import torch.nn as nn
import torch.optim as optim
import torchaudio
from tqdm import tqdm

REPO_ROOT = Path(__file__).resolve().parents[2]
SKEY_DIR = REPO_ROOT / "ml" / "skey"
MODELS_DIR = REPO_ROOT / "ml" / "models"
DATASET_DIR = REPO_ROOT / "ml" / "dataset"
DEFAULT_LABELS = DATASET_DIR / "typebeats" / "labels.csv"
DEFAULT_CKPT = SKEY_DIR / "skey" / "models" / "skey.pt"

sys.path.insert(0, str(SKEY_DIR))
from skey.chromanet import ChromaNet
from skey.hcqt import VQT, CropCQT

DEVICE = "mps" if torch.backends.mps.is_available() else "cuda" if torch.cuda.is_available() else "cpu"

# S-KEY key_map (from key_detection.py)
SKEY_MAP = {
    0: "A Major", 1: "Bb Major", 2: "B Major", 3: "C Major",
    4: "C# Major", 5: "D Major", 6: "D# Major", 7: "E Major",
    8: "F Major", 9: "F# Major", 10: "G Major", 11: "G# Major",
    12: "B minor", 13: "C minor", 14: "C# minor", 15: "D minor",
    16: "D# minor", 17: "E minor", 18: "F minor", 19: "F# minor",
    20: "G minor", 21: "G# minor", 22: "A minor", 23: "Bb minor",
}
SKEY_MAP_INV = {v.lower(): k for k, v in SKEY_MAP.items()}


def our_key_to_skey(key_idx: int) -> int:
    """Convert our key_idx (C=0) to S-KEY key_idx (A=0)."""
    root = key_idx % 12
    is_minor = key_idx >= 12
    if not is_minor:
        return (root - 9) % 12
    else:
        return 12 + (root - 11) % 12


def load_skey_components(ckpt_path: Path, device: str):
    ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    hcqt = VQT(harmonics=[1], fmin=27.5, n_bins=99).to(device)
    chromanet = ChromaNet(
        n_bins=84, n_harmonics=1,
        out_channels=[2, 3, 40, 40, 30, 10, 3],
        kernels=[7, 7, 7, 7, 7, 5, 5],
        temperature=1,
    ).to(device)
    hcqt.load_state_dict({k.replace("hcqt.", ""): v for k, v in ckpt["stone"].items() if "hcqt" in k})
    chromanet.load_state_dict({k.replace("chromanet.", ""): v for k, v in ckpt["stone"].items() if "chromanet" in k})
    return hcqt, chromanet, CropCQT(84), ckpt


def load_audio(path: Path, sr: int = 22050) -> torch.Tensor | None:
    try:
        wav, orig_sr = torchaudio.load(str(path), backend="soundfile")
        if orig_sr != sr:
            wav = torchaudio.transforms.Resample(orig_sr, sr)(wav)
        if wav.shape[0] > 1:
            wav = wav.mean(dim=0, keepdim=True)
        mx = wav.abs().max()
        if mx > 0:
            wav = wav / mx
        return wav
    except Exception:
        return None


def extract_features(hcqt: VQT, crop_fn: CropCQT, audio: torch.Tensor, device: str) -> torch.Tensor:
    with torch.no_grad():
        x = audio.unsqueeze(0).to(device)          # [1, 1, T]
        h = hcqt(x)                                 # [1, harmonics, bins, frames]
        c = crop_fn(h, torch.zeros(1).to(device))   # [1, 1, 84, frames]
    return c


def load_labels(csv_path: Path, val_split: float) -> tuple[list, list]:
    rows = list(csv.DictReader(open(csv_path)))
    random.shuffle(rows)
    n_val = max(1, int(len(rows) * val_split))
    return rows[n_val:], rows[:n_val]


def run_epoch(
    hcqt: VQT, chromanet: ChromaNet, crop_fn: CropCQT,
    rows: list, optimizer, criterion, device: str, train: bool
) -> tuple[float, float]:
    chromanet.train(train)
    total_loss, correct, total = 0.0, 0, 0
    random.shuffle(rows)

    for row in tqdm(rows, desc="train" if train else "val ", leave=False):
        path = Path(row["path"])
        if not path.is_absolute():
            path = REPO_ROOT / "ml" / path
        if not path.exists():
            continue

        audio = load_audio(path)
        if audio is None:
            continue

        label_skey = our_key_to_skey(int(row["key_idx"]))
        target = torch.tensor([label_skey], dtype=torch.long).to(device)

        features = extract_features(hcqt, crop_fn, audio, device)  # [1, 1, 84, T]

        if train:
            optimizer.zero_grad()

        logits = chromanet(features)                          # [T, 24]
        pred = logits.mean(dim=0, keepdim=True)              # [1, 24]

        loss = criterion(pred, target)

        if train:
            loss.backward()
            optimizer.step()

        total_loss += loss.item()
        correct += (pred.argmax(dim=-1) == target).sum().item()
        total += 1

    return total_loss / max(total, 1), correct / max(total, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--val-split", type=float, default=0.15)
    parser.add_argument("--labels", type=Path, default=DEFAULT_LABELS)
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CKPT)
    args = parser.parse_args()

    print(f"Device: {DEVICE}")
    print(f"Loading S-KEY from {args.checkpoint}...")
    hcqt, chromanet, crop_fn, ckpt = load_skey_components(args.checkpoint, DEVICE)

    # Freeze VQT — it's a fixed feature extractor
    for p in hcqt.parameters():
        p.requires_grad = False
    hcqt.eval()

    train_rows, val_rows = load_labels(args.labels, args.val_split)
    print(f"Train: {len(train_rows)} | Val: {len(val_rows)}")

    optimizer = optim.AdamW(chromanet.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)
    criterion = nn.CrossEntropyLoss()

    best_val_acc = 0.0
    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    for epoch in range(args.epochs):
        train_loss, train_acc = run_epoch(hcqt, chromanet, crop_fn, train_rows, optimizer, criterion, DEVICE, train=True)
        with torch.no_grad():
            val_loss, val_acc = run_epoch(hcqt, chromanet, crop_fn, val_rows, None, criterion, DEVICE, train=False)
        scheduler.step()

        print(f"Epoch {epoch+1}/{args.epochs} — "
              f"train loss: {train_loss:.4f} acc: {train_acc:.2%} | "
              f"val loss: {val_loss:.4f} acc: {val_acc:.2%}")

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            # Save in same format as skey.pt
            new_ckpt = {k: v for k, v in ckpt.items() if "stone" not in k}
            stone_state = {**{f"hcqt.{k}": v for k, v in hcqt.state_dict().items()},
                           **{f"chromanet.{k}": v for k, v in chromanet.state_dict().items()}}
            new_ckpt["stone"] = stone_state
            torch.save(new_ckpt, MODELS_DIR / "skey_finetuned.pt")

    print(f"\nBest val acc: {best_val_acc:.2%}")
    print(f"Checkpoint: {MODELS_DIR}/skey_finetuned.pt")


if __name__ == "__main__":
    main()
