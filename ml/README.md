# Keylo ML — S-KEY / ChromaNet Pipeline

Key detection model for trap / typebeat audio. Fine-tunes a ChromaNet-style
self-supervised CNN on trap instrumentals, exports to ONNX for C++ integration.

## Setup

```bash
cd ml
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

## Pipeline (run in order)

### 1. Download dataset
```bash
python training/01_download.py --per-query 30
# Dry-run preview:
python training/01_download.py --dry-run
# Re-run anytime — already-downloaded videos are skipped via archive index
```
Output: `ml/dataset/typebeats/*.wav`. Dedup index: `ml/dataset/downloaded.txt`.

### 2. Pre-label
```bash
python training/02_prelabel.py
```
Priority: filename key info → existing model → KK chroma baseline.
Output: `ml/dataset/labels.csv` + `labels_review.csv` (low-confidence files to verify manually).

**Manual review step**: open `labels_review.csv`, listen to each file, correct key column.

### 3. Fine-tune
```bash
python training/03_finetune.py \
    --pretrain-epochs 20 \
    --finetune-epochs 50 \
    --min-conf 0.6
```
Phase A: self-supervised (pitch-shift equivariance, no labels required).
Phase B: supervised on verified labels.
Output: `ml/models/chromanet_best.pt`

### 4. Export ONNX
```bash
python training/04_export_onnx.py --validate
```
Output: `ml/models/chromanet_int8.onnx` (deploy this in the plugin).

### 5. Validate
```bash
python training/05_validate.py
```
Compares model vs TSA baseline on the 9 test loops. **Do not proceed to C++
integration until model beats TSA accuracy.**

## Decision gate

| Model accuracy (9 loops) | Action |
|---|---|
| < 7/9 | More data, longer training, review labels |
| = 7/9 | Parity — add more test loops before deciding |
| > 7/9 | Proceed to ONNX integration in Keylo |

## Architecture

- Input: CQT spectrogram [1, 84, T] — 84 bins (C1–B7), hop 512 @ 22050Hz
- Model: 3-layer CNN + FC head → 24-class softmax (12 major + 12 minor)
- Training: pitch-shift equivariance pre-train → supervised fine-tune
- Export: ONNX opset 17 → int8 dynamic quantization

## Key classes

```
0=C maj, 1=C# maj, ..., 11=B maj
12=C min, 13=C# min, ..., 23=B min
```

## File layout

```
ml/
├── requirements.txt
├── dataset/               # gitignored — audio files
│   ├── labels.csv         # auto-generated labels
│   └── labels_review.csv  # low-confidence → review manually
├── models/                # gitignored — checkpoints + ONNX
│   ├── chromanet_best.pt
│   └── chromanet_int8.onnx
└── training/
    ├── channels.yaml      # YouTube sources config
    ├── audio_utils.py     # CQT extraction, label parsing
    ├── chromanet.py       # Model architecture + losses
    ├── 01_download.py
    ├── 02_prelabel.py
    ├── 03_finetune.py
    ├── 04_export_onnx.py
    └── 05_validate.py
```
