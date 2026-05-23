#!/usr/bin/env python3
"""
Pre-label audio files using Essentia KeyExtractor (edma profile).

Priority:
  1. Parse key from filename (free, confidence=1.0)
  2. Essentia KeyExtractor profileType='edma' (EDM-tuned, ~84% accuracy)

Output: <audio-dir>/labels.csv
  path, key_idx, key_name, source (filename|model), confidence

Usage:
    python 02_prelabel.py [--audio-dir PATH]
"""

import argparse
import csv
import logging
from pathlib import Path

from audio_utils import parse_key_from_filename
from chromanet import KEY_NAMES

logging.disable(logging.CRITICAL)

REPO_ROOT = Path(__file__).resolve().parents[2]

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

ENHARMONIC = {
    "Db": "C#", "Eb": "D#", "Fb": "E", "Gb": "F#",
    "Ab": "G#", "Bb": "A#", "Cb": "B",
}


def essentia_to_idx(key: str, scale: str) -> int:
    note = ENHARMONIC.get(key, key)
    root = NOTE_NAMES.index(note)
    return root + (12 if scale == "minor" else 0)


def build_extractor():
    import essentia.standard as es
    return es.KeyExtractor(profileType="edma")


def label_file(path: Path, extractor) -> dict:
    import essentia.standard as es

    gt = parse_key_from_filename(path)
    if gt is not None:
        return {
            "path": str(path),
            "key_idx": gt,
            "key_name": KEY_NAMES[gt],
            "source": "filename",
            "confidence": 1.0,
        }

    try:
        audio = es.MonoLoader(filename=str(path), sampleRate=44100)()
        key, scale, strength = extractor(audio)
        key_idx = essentia_to_idx(key, scale)
    except Exception:
        return {
            "path": str(path),
            "key_idx": 0,
            "key_name": KEY_NAMES[0],
            "source": "failed",
            "confidence": 0.0,
        }

    return {
        "path": str(path),
        "key_idx": key_idx,
        "key_name": KEY_NAMES[key_idx],
        "source": "model",
        "confidence": float(strength),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--audio-dir", type=Path,
                        default=REPO_ROOT / "ml" / "dataset" / "typebeats")
    parser.add_argument("--checkpoint", type=Path, default=None)  # unused, compat
    args = parser.parse_args()

    print("Loading Essentia KeyExtractor (edma)...")
    extractor = build_extractor()

    audio_files = sorted(
        f for ext in ("*.wav", "*.mp3", "*.flac")
        for f in args.audio_dir.rglob(ext)
    )
    print(f"Labelling {len(audio_files)} files...")

    rows = []
    for i, path in enumerate(audio_files):
        row = label_file(path, extractor)
        rows.append(row)
        if (i + 1) % 100 == 0 or (i + 1) == len(audio_files):
            print(f"  {i+1}/{len(audio_files)}")

    out_csv = args.audio_dir / "labels.csv"
    with open(out_csv, "w", newline="") as f:
        writer = csv.DictWriter(
            f, fieldnames=["path", "key_idx", "key_name", "source", "confidence"]
        )
        writer.writeheader()
        writer.writerows(rows)

    by_source: dict[str, int] = {}
    low_conf = [r for r in rows if float(r["confidence"]) < 0.6]
    for r in rows:
        by_source[r["source"]] = by_source.get(r["source"], 0) + 1

    print(f"\nLabels: {out_csv}")
    print(f"  By source: {by_source}")
    print(f"  Low confidence (<0.6): {len(low_conf)}")

    if low_conf:
        review_csv = args.audio_dir / "labels_review.csv"
        with open(review_csv, "w", newline="") as f:
            writer = csv.DictWriter(
                f, fieldnames=["path", "key_idx", "key_name", "source", "confidence"]
            )
            writer.writeheader()
            writer.writerows(low_conf)
        print(f"  Review list: {review_csv}")


if __name__ == "__main__":
    main()
