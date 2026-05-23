# Keylo ML — S-KEY Fine-tuning Pipeline

Key detection model for trap/typebeat audio. Fine-tunes Deezer S-KEY (ChromaNet pre-trained on EDM) on a labeled trap dataset, exports to ONNX for integration in the Keylo VST plugin.

---

## Pipeline

### 1. Dataset download

Search YouTube for typebeat audio via yt-dlp. Filters: duration 90–210s, title must contain "type beat", excludes tutorials.

```bash
cd ml && source .venv/bin/activate
python training/01_download.py --per-query 100
```

Output: `ml/dataset/typebeats/<video-id>-<title>.wav`
Dedup: `ml/dataset/downloaded.txt` — re-run anytime, already-downloaded files are skipped.

Result: ~1200 WAV files across 35 search queries (trap, drill, rnb, afrobeat, artist type beats, ecc.)

---

### 2. Labeling with Essentia edma

Key labels generated with `KeyExtractor(profileType="edma")` — Essentia's EDM-tuned profile, ~84% accuracy. No manual labeling needed.

Priority per file:
1. Key parsed from filename (e.g. `"Dm trap type beat"` → D minor), confidence = 1.0
2. Essentia edma prediction, confidence = strength value (0–1)

Files with confidence < 0.6 removed from dataset (audio + CSV).

```bash
python training/02_prelabel.py 2>/dev/null
```

Output: `ml/dataset/typebeats/labels.csv`
Result: 1182 labeled files (39 from filename, 1143 from edma model)

---

### 3. Fine-tune S-KEY

S-KEY (Deezer, MIT license) is a ConvNeXt-based ChromaNet pre-trained self-supervised on EDM. Checkpoint: `ml/skey/skey/models/skey.pt`.

Fine-tuning strategy:
- VQT (feature extractor) frozen
- ChromaNet only trained, supervised cross-entropy on edma labels
- Val split 15%, CosineAnnealingLR, AdamW lr=1e-4

```bash
python training/04_finetune_skey.py --epochs 30
```

Output: `ml/models/skey_finetuned.pt` (same format as skey.pt)

---

### 4. Validate

```bash
# Via S-KEY CLI on test loops
cd ml/skey
poetry run skey /path/to/Assets/test-loops --device mps \
    --checkpoint ../models/skey_finetuned.pt

# Or via script
python training/05_validate.py
```

**Gate:** only proceed to ONNX export if fine-tuned model beats base S-KEY on test loops.

---

### 5. Export ONNX

```bash
python training/05_export_onnx.py
```

Output: `ml/models/skey_int8.onnx` — target < 8MB, < 200ms on modern CPU for 12s audio.

---

## Setup

```bash
cd ml
python3.12 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
pip install torchcodec

# Clone S-KEY
git clone https://github.com/deezer/skey
cd skey && pip install poetry && poetry lock && poetry install && cd ..
```

---

## Key class mapping

| System | C | C# | D | ... | A | A# | B |
|--------|---|----|---|-----|---|----|---|
| Ours (major) | 0 | 1 | 2 | ... | 9 | 10 | 11 |
| Ours (minor) | 12 | 13 | 14 | ... | 21 | 22 | 23 |
| S-KEY (major) | 3 | 4 | 5 | ... | 0 | 1 | 2 |
| S-KEY (minor) | 13 | 14 | 15 | ... | 22 | 23 | 12 |

---

## File layout

```
ml/
├── requirements.txt
├── dataset/
│   ├── typebeats/          # gitignored — 1182 WAV files
│   │   ├── labels.csv      # edma labels
│   │   └── labels_review.csv
│   └── downloaded.txt      # yt-dlp dedup archive
├── models/                 # gitignored
│   └── skey_finetuned.pt
├── skey/                   # gitignored — Deezer S-KEY repo
└── training/
    ├── audio_utils.py
    ├── chromanet.py        # our custom ChromaNet (unused after adopting S-KEY)
    ├── 01_download.py
    ├── 02_prelabel.py      # Essentia edma labeling
    ├── 03_finetune.py      # our ChromaNet fine-tune (superseded)
    ├── 04_finetune_skey.py # S-KEY fine-tune ← use this
    └── 05_validate.py
```
