#!/usr/bin/env python3
"""
Fine-tune ChromaNet on the labeled trap dataset.

Two phases:
  Phase A — Self-supervised pre-training on ALL audio (no labels needed).
             Uses pitch-shift equivariance loss. Runs if --pretrain-epochs > 0.
  Phase B — Supervised fine-tune on files with verified labels (source=filename
             or source=model with confidence ≥ MIN_CONF). Uses cross-entropy.

Checkpoints saved to ml/models/:
  chromanet_pretrain.pt   — after phase A
  chromanet_best.pt       — best val loss during phase B
  chromanet_final.pt      — after all epochs

Usage:
    python 03_finetune.py [options]

Options:
    --labels        PATH    CSV from 02_prelabel.py (default: ml/dataset/labels.csv)
    --pretrain-epochs N     Phase A epochs (default: 20; set 0 to skip)
    --finetune-epochs N     Phase B epochs (default: 50)
    --batch-size N          (default: 32)
    --lr FLOAT              Learning rate (default: 1e-3)
    --min-conf FLOAT        Min confidence to include in supervised set (default: 0.6)
    --val-split FLOAT       Fraction held out for validation (default: 0.15)
    --resume PATH           Resume from checkpoint
"""

import argparse
import csv
import random
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, Dataset

from audio_utils import load_cqt, pitch_shift_cqt
from chromanet import ChromaNet, equivariance_loss

REPO_ROOT = Path(__file__).resolve().parents[2]
DATASET_DIR = REPO_ROOT / "ml" / "dataset"
MODELS_DIR = REPO_ROOT / "ml" / "models"
MODELS_DIR.mkdir(parents=True, exist_ok=True)

DEVICE = "mps" if torch.backends.mps.is_available() else "cuda" if torch.cuda.is_available() else "cpu"


class AudioDataset(Dataset):
    def __init__(self, rows: list[dict], augment: bool = False):
        self.rows = rows
        self.augment = augment

    def __len__(self):
        return len(self.rows)

    def __getitem__(self, idx):
        row = self.rows[idx]
        cqt = load_cqt(row["path"])
        if cqt is None:
            # Return a zero tensor — will be filtered by collate_fn
            cqt = np.zeros((1, 84, 512), dtype=np.float32)

        if self.augment:
            shift = random.randint(-5, 5)
            if shift != 0:
                cqt = pitch_shift_cqt(cqt, shift)
                label = ChromaNet.shift_label(int(row["key_idx"]), shift)
                return torch.tensor(cqt), label
        return torch.tensor(cqt), int(row["key_idx"])


class UnlabeledDataset(Dataset):
    """All audio, for self-supervised pre-training."""
    def __init__(self, paths: list[Path]):
        self.paths = paths

    def __len__(self):
        return len(self.paths)

    def __getitem__(self, idx):
        cqt = load_cqt(self.paths[idx])
        if cqt is None:
            cqt = np.zeros((1, 84, 512), dtype=np.float32)
        return torch.tensor(cqt)


def pad_collate(batch):
    """Pad CQTs to same time length in batch."""
    if isinstance(batch[0], tuple):
        cqts, labels = zip(*batch)
    else:
        cqts = batch
        labels = None

    max_t = max(c.shape[-1] for c in cqts)
    padded = [torch.nn.functional.pad(c, (0, max_t - c.shape[-1])) for c in cqts]
    x = torch.stack(padded)
    if labels is not None:
        return x, torch.tensor(labels, dtype=torch.long)
    return x


def load_labels(csv_path: Path, min_conf: float) -> list[dict]:
    rows = []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            conf = float(row["confidence"])
            if conf >= min_conf or row["source"] == "filename":
                if Path(row["path"]).exists():
                    rows.append(row)
    return rows


def pretrain(model: ChromaNet, paths: list[Path], epochs: int, batch_size: int, lr: float):
    print(f"\n=== Phase A: Self-supervised pre-training ({len(paths)} files, {epochs} epochs) ===")
    dataset = UnlabeledDataset(paths)
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=True,
                        collate_fn=pad_collate, num_workers=0)
    opt = optim.Adam(model.parameters(), lr=lr)
    model.to(DEVICE)

    for epoch in range(epochs):
        model.train()
        total_loss = 0.0
        for x in loader:
            x = x.to(DEVICE)
            shift = random.choice([-7, -5, -4, -3, 3, 4, 5, 7])
            x_shifted = torch.tensor(
                pitch_shift_cqt(x.cpu().numpy(), shift)
            ).to(DEVICE)
            loss = equivariance_loss(model, x, shift, x_shifted)
            opt.zero_grad()
            loss.backward()
            opt.step()
            total_loss += loss.item()

        print(f"  Epoch {epoch+1}/{epochs} — loss: {total_loss/len(loader):.4f}")

    checkpoint = MODELS_DIR / "chromanet_pretrain.pt"
    torch.save(model.state_dict(), checkpoint)
    print(f"Pretrain checkpoint: {checkpoint}")


def finetune(
    model: ChromaNet,
    rows: list[dict],
    epochs: int,
    batch_size: int,
    lr: float,
    val_split: float,
):
    print(f"\n=== Phase B: Supervised fine-tune ({len(rows)} files, {epochs} epochs) ===")
    random.shuffle(rows)
    n_val = max(1, int(len(rows) * val_split))
    val_rows, train_rows = rows[:n_val], rows[n_val:]
    print(f"  Train: {len(train_rows)} | Val: {len(val_rows)}")

    train_ds = AudioDataset(train_rows, augment=True)
    val_ds = AudioDataset(val_rows, augment=False)
    train_loader = DataLoader(train_ds, batch_size=batch_size, shuffle=True,
                              collate_fn=pad_collate, num_workers=0)
    val_loader = DataLoader(val_ds, batch_size=batch_size, shuffle=False,
                            collate_fn=pad_collate, num_workers=0)

    opt = optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(opt, T_max=epochs)
    criterion = nn.CrossEntropyLoss()
    model.to(DEVICE)
    best_val_loss = float("inf")

    for epoch in range(epochs):
        model.train()
        train_loss = 0.0
        for x, y in train_loader:
            x, y = x.to(DEVICE), y.to(DEVICE)
            loss = criterion(model(x), y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            train_loss += loss.item()
        scheduler.step()

        model.eval()
        val_loss, correct, total = 0.0, 0, 0
        with torch.no_grad():
            for x, y in val_loader:
                x, y = x.to(DEVICE), y.to(DEVICE)
                logits = model(x)
                val_loss += criterion(logits, y).item()
                correct += (logits.argmax(1) == y).sum().item()
                total += len(y)

        val_acc = correct / max(total, 1)
        avg_val = val_loss / max(len(val_loader), 1)
        print(f"  Epoch {epoch+1}/{epochs} — train: {train_loss/len(train_loader):.4f} "
              f"val: {avg_val:.4f} acc: {val_acc:.2%}")

        if avg_val < best_val_loss:
            best_val_loss = avg_val
            torch.save(model.state_dict(), MODELS_DIR / "chromanet_best.pt")

    torch.save(model.state_dict(), MODELS_DIR / "chromanet_final.pt")
    print(f"Best val loss: {best_val_loss:.4f}")
    print(f"Checkpoints: {MODELS_DIR}/chromanet_best.pt, chromanet_final.pt")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--labels", type=Path, default=DATASET_DIR / "labels.csv")
    parser.add_argument("--pretrain-epochs", type=int, default=20)
    parser.add_argument("--finetune-epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--min-conf", type=float, default=0.6)
    parser.add_argument("--val-split", type=float, default=0.15)
    parser.add_argument("--resume", type=Path, default=None)
    args = parser.parse_args()

    print(f"Device: {DEVICE}")
    model = ChromaNet()

    if args.resume and args.resume.exists():
        model.load_state_dict(torch.load(args.resume, map_location="cpu"))
        print(f"Resumed from {args.resume}")

    # Phase A — self-supervised on all audio
    if args.pretrain_epochs > 0:
        all_audio: list[Path] = []
        for ext in ("*.wav", "*.mp3", "*.flac"):
            all_audio.extend(DATASET_DIR.rglob(ext))
        if all_audio:
            pretrain(model, all_audio, args.pretrain_epochs, args.batch_size, args.lr * 0.5)
        else:
            print("No audio found for pre-training — skipping phase A")

    # Phase B — supervised on labeled subset
    if not args.labels.exists():
        print(f"Labels not found: {args.labels}\nRun 02_prelabel.py first.")
        return

    rows = load_labels(args.labels, args.min_conf)
    if len(rows) < 10:
        print(f"Only {len(rows)} labeled files with conf≥{args.min_conf} — need more data.")
        return

    finetune(model, rows, args.finetune_epochs, args.batch_size, args.lr, args.val_split)


if __name__ == "__main__":
    main()
