#!/usr/bin/env python3

# Generate labeled near-duplicate pairs for DejaView's pair classifier.

# Positives are created by transforming a base image using - . Negatives are
# pairs of different base images (automatic).
# TODO: Add hard negatives

# Note: Base images are split into train/val/test first, and pairs are only ever generated within a split.
# A base image and all of its derivatives therefore live in exactly one split and no pair can leake into othr split
#
# To Run: python3 make_pairs.py --base data/base --out data --count 5000


import argparse
import csv
import random
from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


#Transformation seq
# Each function takes (image, rng) and returns (new_image, extension, save_kwargs).
# The name of each transform is recorded in the manifest so evaluation can report per-transformation recall.

def t_resize50(img, rng):
    w, h = img.size
    return img.resize((max(1, w // 2), max(1, h // 2)), Image.LANCZOS), "jpg", {"quality": 92}


def t_resize25(img, rng):
    w, h = img.size
    return img.resize((max(1, w // 4), max(1, h // 4)), Image.LANCZOS), "jpg", {"quality": 92}


def t_quality70(img, rng):
    return img, "jpg", {"quality": 70}


def t_quality40(img, rng):
    return img, "jpg", {"quality": 40}


def t_brightness(img, rng):
    factor = rng.choice([0.75, 0.85, 1.15, 1.3])
    return ImageEnhance.Brightness(img).enhance(factor), "jpg", {"quality": 92}


def t_contrast(img, rng):
    factor = rng.choice([0.75, 0.85, 1.2, 1.4])
    return ImageEnhance.Contrast(img).enhance(factor), "jpg", {"quality": 92}


def t_saturation(img, rng):
    factor = rng.choice([0.5, 0.7, 1.3, 1.6])
    return ImageEnhance.Color(img).enhance(factor), "jpg", {"quality": 92}


def t_topng(img, rng):
    return img, "png", {}


# --- Harder transforms -----------------------------------------------------
# These genuinely damage the perceptual hashes while preserving identity.
# They are the cases where evidence CONFLICTS: hashes say "far apart", but
# aspect ratio and colour histogram still say "same image". A single threshold
# cannot resolve that conflict; a multi-feature classifier can.

def t_crop15(img, rng):
    w, h = img.size
    dx, dy = int(w * 0.15), int(h * 0.15)
    return img.crop((dx, dy, w - dx, h - dy)), "jpg", {"quality": 92}


def t_crop25(img, rng):
    w, h = img.size
    dx, dy = int(w * 0.25), int(h * 0.25)
    return img.crop((dx, dy, w - dx, h - dy)), "jpg", {"quality": 92}


def t_rotate(img, rng):
    angle = rng.choice([-6, -3, 3, 6])
    return img.rotate(angle, expand=False, fillcolor=(0, 0, 0)), "jpg", {"quality": 92}


def t_blur(img, rng):
    return img.filter(ImageFilter.GaussianBlur(radius=2.0)), "jpg", {"quality": 92}


def t_overlay_large(img, rng):
    out = img.copy()
    draw = ImageDraw.Draw(out)
    w, h = out.size
    # Covers roughly a quarter of the frame - a meme caption, a sticker, a logo.
    draw.rectangle([0, 0, w, h // 4], fill=(240, 240, 240))
    draw.text((8, 8), "BREAKING NEWS", fill=(0, 0, 0))
    return out, "jpg", {"quality": 92}


def t_watermark(img, rng):
    out = img.copy()
    draw = ImageDraw.Draw(out)
    w, h = out.size
    # Small caption bar in a corner - the kind of thing a share/export adds.
    bar_h = max(8, h // 12)
    draw.rectangle([0, h - bar_h, w // 2, h], fill=(20, 20, 20))
    draw.text((4, h - bar_h + 2), "shared", fill=(255, 255, 255))
    return out, "jpg", {"quality": 92}


TRANSFORMS = {
    # easy: hashes barely move
    "quality70": t_quality70,
    "quality40": t_quality40,
    "resize50": t_resize50,
    "brightness": t_brightness,
    "contrast": t_contrast,
    "saturation": t_saturation,
    "topng": t_topng,
    # medium
    "resize25": t_resize25,
    "watermark": t_watermark,
    "blur": t_blur,
    # hard but reachable: hashes degrade to ~10, other features must decide
    "rotate": t_rotate,
    "overlay_large": t_overlay_large,
}
# t_crop15 / t_crop25 deliberately excluded from v1: measured min-hash-distance
# of 24-30 against a random-pair baseline of 32, i.e. perceptual hashing carries
# essentially no signal for crops. Candidate generation cannot surface them at
# any usable radius. Retained here as the motivating evidence for the v2
# embedding tier.


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="data/base", help="directory of base images")
    ap.add_argument("--out", default="data", help="output root")
    ap.add_argument("--count", type=int, default=5000, help="base images to use")
    ap.add_argument("--per-image", type=int, default=3,
                    help="transforms applied per base image")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)

    base_dir = Path(args.base).resolve()
    out_dir = Path(args.out).resolve()
    derived_dir = out_dir / "derived"
    derived_dir.mkdir(parents=True, exist_ok=True)

    bases = sorted(p for p in base_dir.iterdir()
                   if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
    if not bases:
        raise SystemExit(f"no images found in {base_dir}")
    rng.shuffle(bases)
    bases = bases[:args.count]
    print(f"using {len(bases)} base images from {base_dir}")

    #Split base images before generating any pairs
    n = len(bases)
    n_train = int(n * 0.70)
    n_val = int(n * 0.15)
    splits = {}
    for i, p in enumerate(bases):
        if i < n_train:
            splits[p] = "train"
        elif i < n_train + n_val:
            splits[p] = "val"
        else:
            splits[p] = "test"

    rows = []
    transform_names = list(TRANSFORMS.keys())

    # Positive images/set:  base vs. its own transformed copies
    for i, path in enumerate(bases):
        if i % 500 == 0:
            print(f"  transforming {i}/{len(bases)}...")
        try:
            img = Image.open(path).convert("RGB")
        except Exception as e:
            print(f"  skipping {path.name}: {e}")
            continue

        chosen = rng.sample(transform_names, k=min(args.per_image, len(transform_names)))
        for tname in chosen:
            try:
                new_img, ext, kwargs = TRANSFORMS[tname](img, rng)
                dst = derived_dir / f"{path.stem}__{tname}.{ext}"
                new_img.save(dst, **kwargs)
            except Exception as e:
                print(f"  transform {tname} failed on {path.name}: {e}")
                continue

            rows.append({
                "path_a": str(path),
                "path_b": str(dst),
                "label": 1,
                "transform": tname,
                "split": splits[path],
                "base_a": path.stem,
                "base_b": path.stem,
            })

    n_positives = len(rows)
    print(f"generated {n_positives} positive pairs")

    # Negative images/ set: two different base images, same split
    by_split = {"train": [], "val": [], "test": []}
    for p in bases:
        by_split[splits[p]].append(p)

    target_per_split = {s: sum(1 for r in rows if r["split"] == s) for s in by_split}
    for split, pool in by_split.items():
        if len(pool) < 2:
            continue
        made, attempts = 0, 0
        seen = set()
        while made < target_per_split[split] and attempts < target_per_split[split] * 10:
            attempts += 1
            a, b = rng.sample(pool, 2)
            key = tuple(sorted((a.stem, b.stem)))
            if key in seen:
                continue
            seen.add(key)
            rows.append({
                "path_a": str(a),
                "path_b": str(b),
                "label": 0,
                "transform": "none",
                "split": split,
                "base_a": a.stem,
                "base_b": b.stem,
            })
            made += 1

    print(f"generated {len(rows) - n_positives} negative pairs")

    #Writing the  manifest
    manifest = out_dir / "pairs.csv"
    with open(manifest, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "path_a", "path_b", "label", "transform", "split", "base_a", "base_b"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"\nwrote {len(rows)} pairs to {manifest}")
    for split in ("train", "val", "test"):
        pos = sum(1 for r in rows if r["split"] == split and r["label"] == 1)
        neg = sum(1 for r in rows if r["split"] == split and r["label"] == 0)
        print(f"  {split}: {pos} positive, {neg} negative")


if __name__ == "__main__":
    main()