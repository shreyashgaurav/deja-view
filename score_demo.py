#!/usr/bin/env python3
"""
Score a DejaView report against ground truth derived from filenames.

The demo library encodes truth in its names: `photo.jpg` is an original and
`photo_whatsapp.jpg`, `photo_rotated.jpg` etc. are its duplicates. Files whose
stem carries no known suffix and has no siblings are singletons that should
match nothing.

This gives real precision/recall on realistic duplicates - including WhatsApp
and Windows-resize copies, which were produced by pipelines the model never
trained on.

Usage:
    python3 score_demo.py --report dejaview-report.json --dir demo-library
"""

import argparse
import json
from collections import defaultdict
from itertools import combinations
from pathlib import Path

# Longest first, so `_win_resize` is stripped before any shorter prefix of it.
SUFFIXES = [
    "_win_resize", "_exact_dup", "_captioned", "_whatsapp",
    "_cropped", "_flipped", "_rotated", "_resized",
]


def split_name(stem):
    """-> (base, transform). Transform is 'original' when no suffix matches."""
    for suf in SUFFIXES:
        if stem.endswith(suf):
            return stem[: -len(suf)], suf[1:]
    return stem, "original"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", default="dejaview-report.json")
    ap.add_argument("--dir", default="demo-library")
    args = ap.parse_args()

    # ---- Ground truth from filenames --------------------------------------
    files = sorted(p for p in Path(args.dir).iterdir()
                   if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
    base_of, transform_of = {}, {}
    groups = defaultdict(list)
    for p in files:
        base, transform = split_name(p.stem)
        base_of[p.name] = base
        transform_of[p.name] = transform
        groups[base].append(p.name)

    true_pairs = set()
    for base, members in groups.items():
        for a, b in combinations(sorted(members), 2):
            true_pairs.add((a, b))

    singletons = [m[0] for m in groups.values() if len(m) == 1]
    print(f"library: {len(files)} files, {len(groups)} distinct images, "
          f"{len(singletons)} singletons")
    print(f"ground truth: {len(true_pairs)} duplicate pairs\n")

    # ---- What DejaView reported -------------------------------------------
    report = json.load(open(args.report))
    found_pairs = set()
    for g in report["groups"]:
        names = [Path(m["path"]).name for m in g["members"]]
        for a, b in combinations(sorted(names), 2):
            found_pairs.add((a, b))

    tp = true_pairs & found_pairs
    fp = found_pairs - true_pairs
    fn = true_pairs - found_pairs

    precision = len(tp) / len(found_pairs) if found_pairs else 0.0
    recall = len(tp) / len(true_pairs) if true_pairs else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0

    print("=== Pair-level results ===")
    print(f"  precision  {precision:.3f}   ({len(tp)} correct of {len(found_pairs)} reported)")
    print(f"  recall     {recall:.3f}   ({len(tp)} found of {len(true_pairs)} real)")
    print(f"  F1         {f1:.3f}")

    # ---- Recall by transformation -----------------------------------------
    # Credit a transform when its file is correctly paired with ANY sibling.
    by_transform = defaultdict(lambda: [0, 0])   # [found, total]
    for a, b in true_pairs:
        for name, other in ((a, b), (b, a)):
            t = transform_of[name]
            if t == "original":
                continue
            by_transform[t][1] += 1
            if (a, b) in found_pairs:
                by_transform[t][0] += 1

    if by_transform:
        print("\n=== Recall by transformation ===")
        for t, (found, total) in sorted(by_transform.items(),
                                        key=lambda kv: kv[1][0] / max(1, kv[1][1])):
            rate = found / total if total else 0
            flag = "   <-- missed" if rate < 0.5 else ""
            print(f"  {t:<12} {rate:.2f}  ({found}/{total}){flag}")

    # ---- Mistakes worth eyeballing ----------------------------------------
    if fp:
        print(f"\n=== False positives ({len(fp)}) - different images merged ===")
        for a, b in sorted(fp)[:15]:
            print(f"  {a}  <->  {b}")
        if len(fp) > 15:
            print(f"  ... and {len(fp) - 15} more")

    if fn:
        print(f"\n=== Missed duplicates ({len(fn)}) ===")
        for a, b in sorted(fn)[:15]:
            print(f"  {a}  <->  {b}")
        if len(fn) > 15:
            print(f"  ... and {len(fn) - 15} more")


if __name__ == "__main__":
    main()