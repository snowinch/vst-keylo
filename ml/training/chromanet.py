"""
ChromaNet — self-supervised key detection model.

Architecture: CQT spectrogram (84 bins × T frames) → CNN → 24 key classes.
Self-supervised training constraint: pitch shift by N semitones → output
label shifts by N (equivariance). No labels required for pre-training.

Key classes (24):
  0–11:  C..B major
  12–23: C..B minor
"""

import torch
import torch.nn as nn
import torch.nn.functional as F


NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
KEY_NAMES = [f"{n} major" for n in NOTE_NAMES] + [f"{n} minor" for n in NOTE_NAMES]


class ChromaNet(nn.Module):
    def __init__(self, n_keys: int = 24):
        super().__init__()
        # Input shape: [B, 1, 84, T]  (84 CQT bins, variable T)
        self.conv = nn.Sequential(
            # Octave-stride conv: 12 bins = 1 octave, stride 1
            nn.Conv2d(1, 32, kernel_size=(12, 3), stride=(1, 1), padding=(0, 1)),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d((2, 2)),                     # → [B, 32, 36, T//2]

            nn.Conv2d(32, 64, kernel_size=(3, 3), padding=(1, 1)),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.MaxPool2d((2, 2)),                     # → [B, 64, 18, T//4]

            nn.Conv2d(64, 128, kernel_size=(3, 3), padding=(1, 1)),
            nn.BatchNorm2d(128),
            nn.ReLU(inplace=True),
        )
        self.pool = nn.AdaptiveAvgPool2d((4, 4))      # → [B, 128, 4, 4]
        self.head = nn.Sequential(
            nn.Flatten(),
            nn.Linear(128 * 4 * 4, 256),
            nn.ReLU(inplace=True),
            nn.Dropout(0.4),
            nn.Linear(256, n_keys),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.conv(x)
        # AdaptiveAvgPool2d not supported on MPS with non-divisible sizes
        if x.device.type == "mps":
            x = self.pool(x.cpu()).to("mps")
        else:
            x = self.pool(x)
        return self.head(x)

    def predict(self, x: torch.Tensor) -> tuple[int, float]:
        """Return (key_idx, confidence) for a single CQT input."""
        with torch.no_grad():
            logits = self.forward(x)
            probs = F.softmax(logits, dim=-1)
            idx = probs.argmax(dim=-1).item()
            conf = probs[0, idx].item()
        return idx, conf

    @staticmethod
    def key_name(idx: int) -> str:
        return KEY_NAMES[idx]

    @staticmethod
    def shift_label(label: int, semitones: int) -> int:
        """Shift key label by N semitones (wraps within major/minor group)."""
        mode_offset = 12 if label >= 12 else 0
        root = label % 12
        shifted_root = (root + semitones) % 12
        return mode_offset + shifted_root


def equivariance_loss(
    model: ChromaNet,
    x: torch.Tensor,
    shift_semitones: int,
    x_shifted: torch.Tensor,
) -> torch.Tensor:
    """
    Self-supervised loss: model(shift(x, N)) should predict shift(model(x), N).
    No ground-truth labels needed.
    """
    logits_orig = model(x)
    logits_shifted = model(x_shifted)

    # Pseudo-label from original prediction, shifted by N semitones
    with torch.no_grad():
        pred_orig = logits_orig.argmax(dim=-1)
        pseudo_label = torch.tensor(
            [ChromaNet.shift_label(p.item(), shift_semitones) for p in pred_orig],
            device=x.device,
            dtype=torch.long,
        )

    return F.cross_entropy(logits_shifted, pseudo_label)
