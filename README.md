# DejaView

**Finds the photos you've seen before.** A C++20 engine that detects exact and
near-duplicate images, combining perceptual hashing with a learned classifier
that judges pairs on nine features rather than a single hand-tuned threshold.

**[▶ Live demo](https://shreyashgaurav.github.io/deja-view/)** — browse real
results and drag the confidence slider to watch groups appear and dissolve.

DejaView is **report-only**: it never modifies, moves, or deletes a file.

---

## Why another duplicate finder

Existing tools (czkawka, dupeGuru, imagededup) all use the same pipeline:
decode → perceptual hash → compare by Hamming distance → threshold → group.
That pipeline is correct and DejaView reimplements it from scratch. Where it
diverges is the final decision.

Every one of those tools asks the user to set **one threshold on one hash**.
That single number cannot express the case where evidence conflicts — where the
structural hashes say "different" but the colour distribution and aspect ratio
say "same photo, transformed."

DejaView replaces the threshold with a calibrated logistic classifier over nine
features. On a held-out test split it catches **100% of rotated duplicates
against 79.8% for a threshold tuned on the same data**, while raising fewer
false positives.

---

## Results

Trained on 5,000 COCO base images with a scripted transformation ladder;
evaluated on a held-out split with base-image-level separation (no image
appears in both train and test).

| | Classifier | Best single hash + threshold |
|---|---|---|
| Precision | 0.987 | 0.978 |
| Recall | 1.000 | 0.977 |
| F1 | **0.993** | 0.978 |
| Missed duplicates | **0** | ~86 |
| False positives | **51** | ~83 |

**Recall by transformation** (test split), classifier vs. the tuned
`dHash ≤ 13` baseline:

| Transformation | Classifier | Baseline |
|---|---|---|
| rotate (±3–6°) | **100%** | 79.8% |
| resize 25% | **100%** | 92.7% |
| large overlay | **100%** | 98.7% |
| resize 50% | 100% | 99.7% |
| quality 40/70, blur, brightness, contrast, saturation, PNG conversion, watermark | 100% | 100% |

The baseline only fails where hashes degrade but colour and geometry survive —
exactly the conflict a single number cannot resolve.

**Cross-domain generalisation.** Trained on photographs, evaluated on 1,200
pairs of transformed *screenshots* (a domain absent from training):
**98.8% precision, 99.7% recall.**

---

## What the model learned

Standardised logistic-regression coefficients, ordered by magnitude:

| Feature | Weight |
|---|---|
| `d_dhash` | −4.67 |
| `d_phash` | −4.41 |
| `hist_distance` | −3.88 |
| `aspect_delta` | −1.41 |
| `dim_ratio` | +1.33 |
| `size_ratio` | −0.64 |
| `d_ahash` | −0.48 |
| `brightness_delta` | +0.31 |
| `contrast_delta` | +0.15 |

Training on easy pairs alone produced a model that leaned almost entirely on
hash distances (`hist_distance` weight −1.19). Once rotations, overlays, and
hard negatives entered the training set, the model **reweighted toward colour
and geometry** — the features that stay reliable when hashes fail. aHash fell
to near-irrelevance, which the model determined on its own.

A worked example from the demo — a horizontally mirrored image:

```
p=0.808   aHash=9   dHash=17   pHash=28   hist_distance=0.018   dim_ratio=1.00
```

pHash at 28 is close to the ~32-bit distance of two *unrelated* hashes: the
structural features carry essentially no signal. But the colour distributions
are a 98% match and the dimensions are identical. The classifier resolves the
conflict correctly. No threshold on any single hash can.

---

## Architecture

```
scan → exact dedup → decode → perceptual hash → candidate match
     → pair features → classifier → cluster → JSON report
```

A funnel: each stage is far more expensive per pair than the last, so each one
discards as much as it cheaply can. On a 633-image library that means 200,028
possible pairs → 10,108 candidates (hash radius) → 1,653 accepted (classifier)
→ 116 groups (clustering).

| Stage | What it does | Notes |
|---|---|---|
| **Scan** | Recursive walk, magic-byte format detection | Extension mismatches flagged; errors are data, never crashes |
| **Exact dedup** | Size buckets → FNV-1a → **byte verification** | Verification makes hash collisions structurally harmless, which is why a fast/simple hash suffices |
| **Decode** | libjpeg-turbo + libpng → grayscale + RGB thumbnails | JPEG scaled DCT decode (1/8 where possible); `setjmp` recovery for corrupt files |
| **Perceptual hash** | aHash, dHash, pHash (hand-written DCT) | Three hashes because each is blind to different edits — they become classifier *features*, not competing verdicts |
| **Candidate match** | Brute-force SIMD `popcount`, union of three radii | Deliberately over-inclusive: a false candidate costs microseconds, a missed one is unrecoverable |
| **Features** | 9 pair features from per-image data computed once | One C++ implementation feeds both training export and runtime — no train/serve skew |
| **Classifier** | `sigmoid(w · standardise(x) + b)` | Weights loaded from JSON with feature-name validation; falls back to a pHash threshold if absent |
| **Cluster** | Union-find with confidence-tiered merging | Weak edges may attach a singleton but never bridge two groups |
| **Report** | Versioned JSON with per-pair evidence | Every verdict carries the features that produced it |

---

## Measured findings

**Perceptual hashing cannot see crops.** Median minimum hash distance by
transformation, against a baseline of ~32 bits for two *random* 64-bit hashes:

| Transformation | 90th-pct min hash distance |
|---|---|
| quality 40/70, blur, saturation | 0 |
| resize 50%, PNG conversion, watermark | 2–3 |
| **rotate, large overlay** | **10** |
| crop 15% | 24 |
| crop 25% | **30** |

A 25% crop is, to a perceptual hash, statistically indistinguishable from an
unrelated photograph. This is not a tuning problem — candidate generation
cannot surface these pairs at any usable radius, because a radius wide enough
to include them stops filtering at all. Crops are therefore **out of scope for
v1**, and this measurement is the concrete case for the planned embedding tier.

**The first classifier failed to beat the baseline.** Trained on synthetic
positives and *random* negatives, it scored F1 0.9938 against the baseline's
0.9984 — worse. The cause: two random COCO photographs share nothing, so
rejecting them requires no intelligence and leaves no headroom. Hard-negative
mining and harder positives (rotation, large overlays) were what made the
comparison meaningful.

**Filesystem choice dominated everything.** Identical code, identical build:
**187 images/s** on native ext4 versus **12 images/s** across the WSL→Windows
filesystem boundary. A release build over `/mnt/c` bought only ~1.3×, which
correctly indicated the workload was I/O-bound rather than CPU-bound.

**Flat images produce degenerate hashes.** A uniform image has no left-right
brightness transitions, so its dHash is all zeros — and so is that of any
sufficiently low-contrast image, causing unrelated images to collide. Surfaced
by a unit test whose *assumption* was wrong while the implementation was
correct.

---

## Limitations

**UI screenshots over-merge.** Screenshots of the same application share window
chrome, fonts, and palette by construction. The current feature set cannot
separate that shared framing from genuine duplication, so screenshot-heavy
libraries produce over-large clusters. Photographic libraries are unaffected
(98.8% precision measured). Fixing this needs content-region features or
learned embeddings.

**No rotation or flip invariance.** Both are caught opportunistically — via
colour and dimensional evidence when hashes fail — not by design. The heuristic
generalises imperfectly to visually homogeneous domains.

**No incremental cache.** Specified (FR-C1–C4) but unimplemented: every scan
recomputes all hashes. A persistent cache would make re-scans near-instant.

**Single-threaded.** Decoding is embarrassingly parallel and currently uses one
core.

**JPEG and PNG only.** No HEIC, WebP, or RAW.

---

## Build

Requires a C++20 compiler, CMake ≥ 3.22, libjpeg-turbo, and libpng.
GoogleTest and nlohmann/json are fetched automatically.

```bash
sudo apt install build-essential cmake ninja-build libjpeg-dev libpng-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # 50 tests
```

## Usage

```bash
# Scan a directory
./build/dejaview ~/Pictures

# Writes dejaview-report.json alongside terminal output
```

The report is versioned JSON: scan statistics, errors, groups with members and
keep-recommendations, and per-pair classifier probabilities with the full
feature vector behind each one.

## Retraining

```bash
cd training
python3 make_pairs.py --base data/base --out data --count 5000 --per-image 5
../build/dejaview mine-negatives data/base data/mined.csv 16
python3 add_hard_negatives.py --pairs data/pairs.csv --mined data/mined.csv \
    --out data/pairs_hard.csv --max-per-split 12000
../build/dejaview export-features data/pairs_hard.csv data/train.csv
python3 train.py --csv data/train.csv --out model/weights.json
```

Features are computed by the C++ binary in both paths. Splits are assigned at
the base-image level before any pair is generated, so no image's derivatives
can straddle train and test.

## Roadmap

- **v2 — embedding tier.** A contrastively-trained embedding for the cases
  hashing provably cannot reach (crops, rotations, screenshots).
- Incremental hash cache; multithreaded decoding; HEIC support.

## License

MIT