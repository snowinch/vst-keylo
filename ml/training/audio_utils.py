"""Shared audio utilities: CQT extraction, label parsing, pitch shift."""

import re
import numpy as np
import librosa
from pathlib import Path

SR = 22050
HOP_LENGTH = 512
N_BINS = 84          # 7 octaves × 12 bins, C1–B7
BINS_PER_OCTAVE = 12
FMIN = librosa.note_to_hz("C1")
CLIP_DURATION = 12.0  # seconds to analyse per file


NOTE_ALIASES: dict[str, int] = {
    "c": 0, "c#": 1, "db": 1,
    "d": 2, "d#": 3, "eb": 3,
    "e": 4,
    "f": 5, "f#": 6, "gb": 6,
    "g": 7, "g#": 8, "ab": 8,
    "a": 9, "a#": 10, "bb": 10,
    "b": 11,
}

MODE_ALIASES: dict[str, int] = {
    "m": 12, "min": 12, "minor": 12,   # minor → offset 12
    "maj": 0, "major": 0, "": 0,       # major → offset 0
}


def parse_key_from_filename(path: str | Path) -> int | None:
    """
    Extract key class (0–23) from filename.
    Supports patterns like:
      - "Dm", "D#m", "A#m", "Cm", "C#m"  (minor, offset 12)
      - "D", "F#", "Bb"                   (major, offset 0)
      - "d-m", "a-m", "f-m", "c-m"       (hyphenated minor)
      - "dm", "d-m", "dbm", "a#m"        (case insensitive)
    Returns None if pattern not found.
    """
    stem = Path(path).stem.lower()

    # Pattern: optional dash, note name, optional # or b, optional dash, m/min/maj
    pattern = r"[-_ ]([a-g][#b]?)-?(m(?:in(?:or)?)?|maj(?:or)?)(?=[-_ \.]|$)"
    matches = re.findall(pattern, stem)
    if not matches:
        return None

    # Take last match (usually the key info is near the end of the filename)
    note_str, mode_str = matches[-1]
    root = NOTE_ALIASES.get(note_str)
    if root is None:
        return None

    mode_offset = MODE_ALIASES.get(mode_str, 0)
    return mode_offset + root


def load_cqt(path: str | Path, start: float = 0.0) -> np.ndarray | None:
    """
    Load audio file, extract CQT.
    Returns array of shape [1, N_BINS, T] (float32, magnitude in dB).
    Returns None on load failure.
    """
    try:
        y, _ = librosa.load(str(path), sr=SR, mono=True,
                            offset=start, duration=CLIP_DURATION)
    except Exception:
        return None

    if len(y) < SR * 1.0:
        return None

    cqt = librosa.cqt(y, sr=SR, hop_length=HOP_LENGTH,
                      fmin=FMIN, n_bins=N_BINS, bins_per_octave=BINS_PER_OCTAVE)
    cqt_db = librosa.amplitude_to_db(np.abs(cqt), ref=np.max)
    cqt_norm = (cqt_db - cqt_db.mean()) / (cqt_db.std() + 1e-6)
    return cqt_norm[np.newaxis].astype(np.float32)   # [1, 84, T]


def pitch_shift_cqt(cqt: np.ndarray, semitones: int) -> np.ndarray:
    """
    Shift CQT by N semitones by rolling the frequency axis.
    Positive = up, negative = down.
    Works because CQT bins are equally spaced in semitones.
    """
    return np.roll(cqt, shift=semitones, axis=1)
