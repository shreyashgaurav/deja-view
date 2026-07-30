#!/usr/bin/env python3
"""
Merge mined hard negatives into the pair manifest.

`dejaview mine-negatives` finds pairs of DIFFERENT base images whose hashes are
close - two beach scenes, two plates of food. These are genuine negatives that
a single hash threshold gets wrong, which is exactly what the classifier needs
in order to have something worth learning.

Split safety: each mined pair is kept only if BOTH of its images already belong
to the same split in the existing manifest. A pair spanning train and test would
leak, and a pair whose images aren't in the manifest at all has no split.

Usage:
    python3 add_hard_negatives.py --pairs data/pairs.csv --mined data/mined.csv \
        --out data/pairs_hard.csv
"""

import argparse
import csv
from collections import Counter
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", default="data/pairs.csv")
    ap.add_argument("--mined", default="data/mined.csv")
    ap.add_argument("--out", default="data/pairs_hard.csv")
    ap.add_argument("--min-distance", type=int, default=4,
                    help="drop pairs closer than this: COCO may contain a few "
                         "genuine near-duplicates, and mislabelling those as "
                         "negatives would teach the model something false")
    ap.add_argument("--max-per-split", type=int, default=0,
                    help="0 = keep all")
    args = ap.parse_args()

    # --- Learn each base image's split from the existing manifest ----------
    split_of = {}
    original_rows = []
    with open(args.pairs, newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames
        for row in reader:
            original_rows.append(row)
            split_of[row["base_a"]] = row["split"]
            split_of[row["base_b"]] = row["split"]
    print(f"manifest: {len(original_rows)} pairs, {len(split_of)} known base images")

    # --- Read mined candidates ---------------------------------------------
    mined = []
    with open(args.mined, newline="") as f:
        for row in csv.DictReader(f):
            mined.append(row)
    print(f"mined:    {len(mined)} candidate hard negatives")

    kept, dropped_split, dropped_close, dropped_unknown = [], 0, 0, 0
    per_split = Counter()
    dist_buckets = Counter()

    for row in mined:
        stem_a = Path(row["path_a"]).stem
        stem_b = Path(row["path_b"]).stem

        if stem_a not in split_of or stem_b not in split_of:
            dropped_unknown += 1
            continue
        if split_of[stem_a] != split_of[stem_b]:
            dropped_split += 1
            continue

        d_min = min(int(row["d_ahash"]), int(row["d_dhash"]), int(row["d_phash"]))
        if d_min < args.min_distance:
            dropped_close += 1
            continue

        split = split_of[stem_a]
        if args.max_per_split and per_split[split] >= args.max_per_split:
            continue

        per_split[split] += 1
        dist_buckets[d_min // 4 * 4] += 1
        kept.append({
            "path_a": row["path_a"],
            "path_b": row["path_b"],
            "label": 0,
            "transform": "hard_negative",
            "split": split,
            "base_a": stem_a,
            "base_b": stem_b,
        })

    print(f"\nkept {len(kept)} hard negatives")
    print(f"  dropped (different splits):  {dropped_split}")
    print(f"  dropped (too close, < {args.min_distance}): {dropped_close}")
    print(f"  dropped (not in manifest):   {dropped_unknown}")
    print("\n  by split: " + ", ".join(f"{s}={per_split[s]}" for s in
                                       ("train", "val", "test")))
    print("\n  closest-hash-distance distribution:")
    for lo in sorted(dist_buckets):
        print(f"    {lo:>2}-{lo+3:<2} : {dist_buckets[lo]}")

    out_path = Path(args.out)
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(original_rows)
        writer.writerows(kept)

    total = len(original_rows) + len(kept)
    print(f"\nwrote {total} pairs to {out_path}")


if __name__ == "__main__":
    main()