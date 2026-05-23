#!/usr/bin/env python3
"""
Download typebeat audio from YouTube via search queries.
Deduplication: yt-dlp --download-archive tracks video IDs across runs.

Usage:
    python 01_download.py [--per-query N] [--dry-run]

Options:
    --per-query N   Max downloads per search query (default: 30)
    --dry-run       Print commands without downloading
"""

import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DATASET_DIR = REPO_ROOT / "ml" / "dataset"
ARCHIVE_FILE = DATASET_DIR / "downloaded.txt"   # dedup index — commit this

SEARCH_QUERIES = [
    # Genre-based
    "trap type beat free instrumental",
    "dark trap type beat free instrumental",
    "melodic trap type beat free instrumental",
    "hoodtrap type beat free instrumental",
    "rnb type beat free instrumental",
    "hip hop type beat free instrumental",
    "drill type beat free instrumental",
    "afrobeat type beat free instrumental",
    "reggaeton type beat free instrumental",
    "jerk type beat free instrumental",
    "pop type beat free instrumental",
    "indie type beat free instrumental",
    "rage type beat free instrumental",
    "pluggnb type beat free instrumental",
    "jersey club type beat free instrumental",
    "phonk type beat free instrumental",
    # Artist-based (high search volume)
    "drake type beat free",
    "rod wave type beat free",
    "lil baby type beat free",
    "nba youngboy type beat free",
    "polo g type beat free",
    "juice wrld type beat free",
    "central cee type beat free",
    "ken carson type beat free",
    "carti type beat free",
    "travis scott type beat free",
    "future type beat free",
    "gunna type beat free",
    "lil uzi vert type beat free",
    "summer walker type beat free",
    "sza type beat free",
]


MUST_CONTAIN = r"(?i)(type.?beat|typebeat)"
MUST_NOT_CONTAIN = r"(?i)(tutorial|how.?to|tips|grow|react|review|vlog|interview|podcast|lesson|course|explained|critique|feedback|rank|tier)"
MATCH_FILTER = (
    f"duration>=90 & duration<=210 "
    f"& title~={MUST_CONTAIN} "
    f"& title!~={MUST_NOT_CONTAIN}"
)


def build_cmd(query: str, out_dir: Path, per_query: int, archive: Path) -> list[str]:
    return [
        "yt-dlp",
        f"ytsearch{per_query}:{query}",
        "--extract-audio",
        "--audio-format", "wav",
        "--audio-quality", "0",
        "--postprocessor-args", "ffmpeg:-ar 22050 -ac 1",
        "--output", str(out_dir / "%(id)s-%(title)s.%(ext)s"),
        "--restrict-filenames",
        "--download-archive", str(archive),
        "--match-filter", MATCH_FILTER,
        "--ignore-errors",
        "--quiet",
        "--progress",
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--per-query", type=int, default=100)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    out_dir = DATASET_DIR / "typebeats"
    out_dir.mkdir(parents=True, exist_ok=True)
    ARCHIVE_FILE.parent.mkdir(parents=True, exist_ok=True)

    print(f"Output: {out_dir}")
    print(f"Archive: {ARCHIVE_FILE}")
    print(f"Queries: {len(SEARCH_QUERIES)} × {args.per_query} = up to {len(SEARCH_QUERIES) * args.per_query} files\n")

    for query in SEARCH_QUERIES:
        cmd = build_cmd(query, out_dir, args.per_query, ARCHIVE_FILE)
        print(f"→ \"{query}\"")
        if args.dry_run:
            print("  " + " ".join(cmd[:6]) + " ...")
            continue
        result = subprocess.run(cmd)
        if result.returncode not in (0, 1):  # 1 = partial errors, ok
            print(f"  [warn] exit {result.returncode}")

    if not args.dry_run:
        total = sum(1 for _ in out_dir.glob("*.wav"))
        print(f"\nDone. {total} WAV files in {out_dir}")


if __name__ == "__main__":
    main()
