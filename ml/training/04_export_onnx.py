#!/usr/bin/env python3
"""
Export ChromaNet checkpoint to ONNX with int8 static quantization.
Target: < 8MB, inference < 200ms for 12s audio on modern CPU.

Output files:
  ml/models/chromanet.onnx           — FP32 baseline
  ml/models/chromanet_int8.onnx      — int8 quantized (deploy this one)

Usage:
    python 04_export_onnx.py [--checkpoint PATH] [--validate]
"""

import argparse
import time
from pathlib import Path

import numpy as np
import torch
import onnx
import onnxruntime as ort
from onnxruntime.quantization import quantize_dynamic, QuantType

from audio_utils import load_cqt
from chromanet import ChromaNet, KEY_NAMES

REPO_ROOT = Path(__file__).resolve().parents[2]
MODELS_DIR = REPO_ROOT / "ml" / "models"
TEST_LOOPS_DIR = REPO_ROOT / "Assets" / "test-loops"


def export_onnx(model: ChromaNet, out_path: Path):
    model.eval()
    # Dynamic time axis — T can vary
    dummy = torch.zeros(1, 1, 84, 512)
    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        opset_version=17,
        input_names=["cqt"],
        output_names=["logits"],
        dynamic_axes={"cqt": {3: "time_frames"}, "logits": {0: "batch"}},
        do_constant_folding=True,
    )
    onnx.checker.check_model(str(out_path))
    size_mb = out_path.stat().st_size / 1e6
    print(f"Exported: {out_path} ({size_mb:.1f} MB)")
    return out_path


def quantize(fp32_path: Path, int8_path: Path):
    quantize_dynamic(
        str(fp32_path),
        str(int8_path),
        weight_type=QuantType.QUInt8,
    )
    size_mb = int8_path.stat().st_size / 1e6
    print(f"Quantized: {int8_path} ({size_mb:.1f} MB)")


def validate_onnx(onnx_path: Path):
    """Run on test-loops and report accuracy + latency."""
    test_files = list(TEST_LOOPS_DIR.glob("*.wav"))
    if not test_files:
        print("No test files found — skipping validation")
        return

    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])

    results = []
    latencies = []
    for path in test_files:
        cqt = load_cqt(path)
        if cqt is None:
            continue
        x = cqt[np.newaxis].astype(np.float32)  # [1, 1, 84, T]

        t0 = time.perf_counter()
        logits = sess.run(["logits"], {"cqt": x})[0]
        latencies.append((time.perf_counter() - t0) * 1000)

        probs = np.exp(logits) / np.exp(logits).sum(axis=-1, keepdims=True)
        pred_idx = int(probs.argmax())
        pred_name = KEY_NAMES[pred_idx]
        conf = float(probs[0, pred_idx])

        results.append((path.name, pred_name, conf))

    print(f"\nValidation on {len(results)} test loops:")
    for fname, pred, conf in results:
        print(f"  {fname:<60} → {pred:<16} ({conf:.2%})")
    print(f"\nAvg latency: {np.mean(latencies):.1f}ms | Max: {np.max(latencies):.1f}ms")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path,
                        default=MODELS_DIR / "chromanet_best.pt")
    parser.add_argument("--validate", action="store_true", default=True)
    args = parser.parse_args()

    if not args.checkpoint.exists():
        print(f"Checkpoint not found: {args.checkpoint}")
        print("Run 03_finetune.py first.")
        return

    model = ChromaNet()
    model.load_state_dict(torch.load(args.checkpoint, map_location="cpu"))
    print(f"Loaded: {args.checkpoint}")

    fp32_path = MODELS_DIR / "chromanet.onnx"
    int8_path = MODELS_DIR / "chromanet_int8.onnx"

    export_onnx(model, fp32_path)
    quantize(fp32_path, int8_path)

    if args.validate:
        print("\n--- FP32 ---")
        validate_onnx(fp32_path)
        print("\n--- INT8 ---")
        validate_onnx(int8_path)


if __name__ == "__main__":
    main()
