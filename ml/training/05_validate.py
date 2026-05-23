#!/usr/bin/env python3
"""
Validate ChromaNet against the test-loops and an optional validation set.
Compares model accuracy vs TSA baseline (reads KeyloTest output if available).

Prints per-file prediction, ground truth, correctness, and aggregate stats.
Root-only accuracy (ignores mode) is also reported — useful early when mode
detection is still weak.

Usage:
    python 05_validate.py [--checkpoint PATH] [--audio-dir PATH]
"""

import argparse
import subprocess
import re
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

from audio_utils import load_cqt, parse_key_from_filename, KEY_NAMES
from chromanet import ChromaNet

REPO_ROOT = Path(__file__).resolve().parents[2]
TEST_LOOPS_DIR = REPO_ROOT / "Assets" / "test-loops"
MODELS_DIR = REPO_ROOT / "ml" / "models"
KEYLO_TEST = REPO_ROOT / "build" / "Release" / "KeyloTest"

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def key_root(key_idx: int) -> int:
    return key_idx % 12


def key_mode(key_idx: int) -> str:
    return "minor" if key_idx >= 12 else "major"


def run_tsa_baseline() -> dict[str, str]:
    """Run KeyloTest binary and parse its output. Returns {filename: predicted_key}."""
    if not KEYLO_TEST.exists():
        return {}
    try:
        loops = sorted(TEST_LOOPS_DIR.glob("*.wav"))
        result = subprocess.run(
            [str(KEYLO_TEST)] + [str(p) for p in loops],
            capture_output=True, text=True, timeout=60,
        )
        predictions = {}
        for line in result.stdout.splitlines():
            # Parse lines like: "BNB - Dm - 100bpm - @disa_rnk.wav → D min"
            m = re.match(r"(.+?\.wav)\s+.*?→\s+(.+)", line)
            if m:
                predictions[m.group(1).strip()] = m.group(2).strip()
        return predictions
    except Exception as e:
        print(f"[warn] TSA baseline failed: {e}")
        return {}


def evaluate(model: ChromaNet | None, audio_dir: Path) -> dict:
    files = sorted(audio_dir.glob("*.wav")) + sorted(audio_dir.glob("*.mp3"))
    files = [f for f in files if parse_key_from_filename(f) is not None]
    if not files:
        print(f"No labeled files found in {audio_dir}")
        return {}

    correct_full, correct_root = 0, 0
    rows = []

    for path in files:
        gt_idx = parse_key_from_filename(path)
        if gt_idx is None:
            continue

        if model is not None:
            cqt = load_cqt(path)
            if cqt is None:
                continue
            x = torch.tensor(cqt[np.newaxis])
            pred_idx, conf = model.predict(x)
        else:
            pred_idx, conf = 0, 0.0

        full_match = pred_idx == gt_idx
        root_match = key_root(pred_idx) == key_root(gt_idx)
        correct_full += full_match
        correct_root += root_match

        rows.append({
            "file": path.name,
            "gt": KEY_NAMES[gt_idx],
            "pred": KEY_NAMES[pred_idx],
            "conf": conf,
            "full_match": full_match,
            "root_match": root_match,
        })

    return {"rows": rows, "correct_full": correct_full,
            "correct_root": correct_root, "total": len(rows)}


def print_results(label: str, stats: dict, tsa: dict[str, str] | None = None):
    if not stats:
        return
    rows = stats["rows"]
    n = stats["total"]
    print(f"\n{'='*70}")
    print(f"{label}  ({n} files)")
    print(f"{'='*70}")
    header = f"{'File':<52} {'GT':<14} {'Pred':<14} {'Conf':>6} {'✓'}"
    if tsa:
        header += f"  {'TSA':<14}"
    print(header)
    print("-" * 70)

    for r in rows:
        tick = "✓" if r["full_match"] else ("~" if r["root_match"] else "✗")
        line = (f"{r['file']:<52} {r['gt']:<14} {r['pred']:<14} "
                f"{r['conf']:>5.1%} {tick}")
        if tsa:
            tsa_pred = tsa.get(r["file"], "—")
            line += f"  {tsa_pred:<14}"
        print(line)

    acc_full = stats["correct_full"] / max(n, 1)
    acc_root = stats["correct_root"] / max(n, 1)
    print(f"\nAccuracy (full): {stats['correct_full']}/{n} = {acc_full:.1%}")
    print(f"Accuracy (root): {stats['correct_root']}/{n} = {acc_root:.1%}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path,
                        default=MODELS_DIR / "chromanet_best.pt")
    parser.add_argument("--audio-dir", type=Path, default=TEST_LOOPS_DIR)
    args = parser.parse_args()

    model = None
    if args.checkpoint.exists():
        model = ChromaNet()
        model.load_state_dict(torch.load(args.checkpoint, map_location="cpu"))
        model.eval()
        print(f"Model: {args.checkpoint}")
    else:
        print(f"[warn] Checkpoint not found: {args.checkpoint}")
        print("Running KK baseline only.")

    tsa = run_tsa_baseline()
    if tsa:
        print(f"TSA baseline loaded ({len(tsa)} predictions)")

    stats = evaluate(model, args.audio_dir)
    print_results("ChromaNet evaluation", stats, tsa if tsa else None)

    if tsa and stats.get("rows"):
        # Count TSA correct vs model correct
        tsa_correct = 0
        for r in stats["rows"]:
            gt_root = NOTE_NAMES[int(r["gt"].split()[0] in NOTE_NAMES and NOTE_NAMES.index(r["gt"].split()[0])
                                   if r["gt"].split()[0] in NOTE_NAMES else 0)]
            # Simple heuristic: TSA correct if "✓" comparison
            tsa_key = tsa.get(r["file"], "")
            gt_name = r["gt"]
            # Compare root note
            if gt_name.split()[0].lower() in tsa_key.lower():
                tsa_correct += 1
        print(f"\nTSA root accuracy (approx): {tsa_correct}/{len(stats['rows'])} "
              f"= {tsa_correct/max(len(stats['rows']),1):.1%}")


if __name__ == "__main__":
    main()
