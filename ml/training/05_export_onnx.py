#!/usr/bin/env python3
"""
Export fine-tuned S-KEY to ONNX.

Pipeline wrapped in a single module:
  audio [1, T] float32 (mono, normalized -1..1, sr=22050)
  → VQT  [1, 1, 99, frames]
  → crop [1, 1, 84, frames]   (CropCQT with offset=0)
  → ChromaNet → [1, 24]       (softmax probabilities)

Output: ml/models/skey_finetuned.onnx (fp32)
        ml/models/skey_finetuned_int8.onnx (quantized, optional)

Usage:
    python training/05_export_onnx.py [--checkpoint PATH] [--quantize]
"""

import argparse
import sys
from pathlib import Path

import torch
import torch.nn as nn

REPO_ROOT = Path(__file__).resolve().parents[2]
SKEY_DIR = REPO_ROOT / "ml" / "skey"
MODELS_DIR = REPO_ROOT / "ml" / "models"
DEFAULT_CKPT = MODELS_DIR / "skey_finetuned.pt"

sys.path.insert(0, str(SKEY_DIR))
from skey.chromanet import ChromaNet
from skey.hcqt import VQT, CropCQT

# S-KEY key_map (from key_detection.py)
SKEY_MAP = {
    0: "A Major", 1: "Bb Major", 2: "B Major", 3: "C Major",
    4: "C# Major", 5: "D Major", 6: "D# Major", 7: "E Major",
    8: "F Major", 9: "F# Major", 10: "G Major", 11: "G# Major",
    12: "B minor", 13: "C minor", 14: "C# minor", 15: "D minor",
    16: "D# minor", 17: "E minor", 18: "F minor", 19: "F# minor",
    20: "G minor", 21: "G# minor", 22: "A minor", 23: "Bb minor",
}


class SkeyONNXPipeline(nn.Module):
    """Full inference pipeline: raw audio → key probabilities."""

    def __init__(self, hcqt: VQT, chromanet: ChromaNet):
        super().__init__()
        self.hcqt = hcqt
        self.chromanet = chromanet

    def forward(self, audio: torch.Tensor) -> torch.Tensor:
        """
        Args:
            audio: [1, T] float32, mono, normalized -1..1, sr=22050
        Returns:
            probs: [24] float32, softmax probabilities over 24 keys
        """
        x = audio.unsqueeze(0)        # [1, 1, T]
        h = self.hcqt(x)              # [1, 1, 99, frames]
        c = h[:, :, :84, :]           # [1, 1, 84, frames]  (CropCQT offset=0)
        probs = self.chromanet(c)     # [1, 24]
        return probs[0]               # [24]


def load_pipeline(ckpt_path: Path) -> SkeyONNXPipeline:
    ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)

    hcqt = VQT(harmonics=[1], fmin=27.5, n_bins=99)
    chromanet = ChromaNet(
        n_bins=84, n_harmonics=1,
        out_channels=[2, 3, 40, 40, 30, 10, 3],
        kernels=[7, 7, 7, 7, 7, 5, 5],
        temperature=1,
    )
    hcqt.load_state_dict({k.replace("hcqt.", ""): v for k, v in ckpt["stone"].items() if "hcqt" in k})
    chromanet.load_state_dict({k.replace("chromanet.", ""): v for k, v in ckpt["stone"].items() if "chromanet" in k})

    pipeline = SkeyONNXPipeline(hcqt, chromanet)
    pipeline.eval()
    return pipeline


def verify_onnx(onnx_path: Path, pipeline: SkeyONNXPipeline, dummy: torch.Tensor):
    """Run both PyTorch and ONNX, check outputs match."""
    import numpy as np
    import onnxruntime as ort

    with torch.no_grad():
        pt_out = pipeline(dummy).numpy()

    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    ort_out = sess.run(None, {"audio": dummy.numpy()})[0]

    max_diff = np.abs(pt_out - ort_out).max()
    print(f"  Max diff PT vs ORT: {max_diff:.2e}", "✓" if max_diff < 1e-4 else "✗ WARN")

    # Show top prediction
    top = int(np.argmax(ort_out))
    print(f"  Top key: {SKEY_MAP[top]} ({ort_out[top]:.3f})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CKPT)
    parser.add_argument("--quantize", action="store_true", help="Also export int8 quantized")
    parser.add_argument("--audio-seconds", type=float, default=10.0,
                        help="Duration of dummy audio for tracing (default: 10s)")
    args = parser.parse_args()

    if not args.checkpoint.exists():
        print(f"Checkpoint not found: {args.checkpoint}")
        sys.exit(1)

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Loading pipeline from {args.checkpoint}...")
    pipeline = load_pipeline(args.checkpoint)

    sr = 22050
    T = int(args.audio_seconds * sr)
    dummy = torch.randn(1, T)  # [1, T]

    out_path = MODELS_DIR / "skey_finetuned.onnx"
    print(f"Exporting to {out_path}...")

    with torch.no_grad():
        torch.onnx.export(
            pipeline,
            dummy,
            str(out_path),
            input_names=["audio"],
            output_names=["key_probs"],
            dynamic_axes={
                "audio": {1: "num_samples"},       # variable length audio
                "key_probs": {},                    # always [24]
            },
            opset_version=17,
            do_constant_folding=True,
        )

    size_mb = out_path.stat().st_size / 1024 / 1024
    print(f"Exported: {out_path} ({size_mb:.1f} MB)")

    print("Verifying...")
    verify_onnx(out_path, pipeline, dummy)

    if args.quantize:
        from onnxruntime.quantization import quantize_dynamic, QuantType
        q_path = MODELS_DIR / "skey_finetuned_int8.onnx"
        print(f"\nQuantizing to {q_path}...")
        quantize_dynamic(str(out_path), str(q_path), weight_type=QuantType.QInt8)
        q_size_mb = q_path.stat().st_size / 1024 / 1024
        print(f"Quantized: {q_path} ({q_size_mb:.1f} MB)")
        print("Verifying quantized...")
        verify_onnx(q_path, pipeline, dummy)

    print("\nDone.")


if __name__ == "__main__":
    main()
