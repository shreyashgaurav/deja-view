#!/usr/bin/env python3
"""
Train DejaView's pair classifier.

Reads the feature CSV produced by `dejaview export-features`, trains a logistic
regression, evaluates it honestly on the held-out test split, and exports the
learned weights as JSON for the C++ runtime to load.

Splits come from the CSV's `split` column, which was assigned at pair-generation
time by base image (SRS ML-D3) - no pair can straddle train/test.

Usage:
    python3 train.py --csv data/train.csv --out model/weights.json
"""

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import (average_precision_score, brier_score_loss,
                             confusion_matrix, precision_recall_curve,
                             precision_score, recall_score, f1_score)
from sklearn.preprocessing import StandardScaler

FEATURES = ["d_ahash", "d_dhash", "d_phash", "dim_ratio", "aspect_delta",
            "size_ratio", "hist_distance", "brightness_delta", "contrast_delta"]


def single_hash_baseline(df_test, df_val):
    """The approach every existing tool uses: one hash, one threshold.

    We give the baseline every advantage - the threshold is tuned on the
    validation split, and we try all three hashes and keep the best."""
    best = None
    for feat in ("d_ahash", "d_dhash", "d_phash"):
        for thresh in range(0, 33):
            pred = (df_val[feat] <= thresh).astype(int)
            f1 = f1_score(df_val["label"], pred, zero_division=0)
            if best is None or f1 > best[2]:
                best = (feat, thresh, f1)
    feat, thresh, _ = best
    pred = (df_test[feat] <= thresh).astype(int)
    return {
        "feature": feat,
        "threshold": thresh,
        "precision": precision_score(df_test["label"], pred, zero_division=0),
        "recall": recall_score(df_test["label"], pred, zero_division=0),
        "f1": f1_score(df_test["label"], pred, zero_division=0),
    }


def pick_threshold(y_true, probs, min_precision):
    """Lowest threshold that still meets the precision target (SRS ML-E2)."""
    precision, recall, thresholds = precision_recall_curve(y_true, probs)
    best_t, best_r = 0.5, 0.0
    for p, r, t in zip(precision[:-1], recall[:-1], thresholds):
        if p >= min_precision and r > best_r:
            best_t, best_r = float(t), float(r)
    return best_t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="data/train.csv")
    ap.add_argument("--out", default="model/weights.json")
    ap.add_argument("--min-precision", type=float, default=0.98)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    print(f"loaded {len(df)} pairs from {args.csv}")

    missing = [f for f in FEATURES if f not in df.columns]
    if missing:
        raise SystemExit(f"CSV is missing feature columns: {missing}")

    train = df[df["split"] == "train"]
    val = df[df["split"] == "val"]
    test = df[df["split"] == "test"]
    for name, part in (("train", train), ("val", val), ("test", test)):
        pos = int((part["label"] == 1).sum())
        print(f"  {name}: {len(part)} pairs ({pos} positive, {len(part)-pos} negative)")

    X_train = train[FEATURES].to_numpy(dtype=np.float64)
    y_train = train["label"].to_numpy()
    X_val = val[FEATURES].to_numpy(dtype=np.float64)
    y_val = val["label"].to_numpy()
    X_test = test[FEATURES].to_numpy(dtype=np.float64)
    y_test = test["label"].to_numpy()

    # Standardise: hash distances live in 0..64, ratios in 0..1. Without this
    # the optimiser struggles and the coefficients aren't comparable.
    scaler = StandardScaler().fit(X_train)
    model = LogisticRegression(max_iter=2000, random_state=args.seed)
    model.fit(scaler.transform(X_train), y_train)

    #Threshold chosen on VALIDATION, never on test
    val_probs = model.predict_proba(scaler.transform(X_val))[:, 1]
    threshold = pick_threshold(y_val, val_probs, args.min_precision)
    print(f"\nchosen threshold (>= {args.min_precision:.2f} precision on val): "
          f"{threshold:.4f}")

    # Test-set evaluation
    test_probs = model.predict_proba(scaler.transform(X_test))[:, 1]
    test_pred = (test_probs >= threshold).astype(int)

    precision = precision_score(y_test, test_pred, zero_division=0)
    recall = recall_score(y_test, test_pred, zero_division=0)
    f1 = f1_score(y_test, test_pred, zero_division=0)
    ap_score = average_precision_score(y_test, test_probs)
    brier = brier_score_loss(y_test, test_probs)
    tn, fp, fn, tp = confusion_matrix(y_test, test_pred).ravel()

    print("\n Test set ")
    print(f"  precision {precision:.4f}")
    print(f"  recall {recall:.4f}")
    print(f"  F1 {f1:.4f}")
    print(f"  average precision {ap_score:.4f}")
    print(f"  Brier score: {brier:.4f}  (lower = better calibrated)")
    print(f"  confusion: TP={tp} FP={fp} FN={fn} TN={tn}")

    # Per-transformation recall
    print("\n=== Recall by transformation ===")
    test_with_pred = test.copy()
    test_with_pred["pred"] = test_pred
    pos = test_with_pred[test_with_pred["label"] == 1]
    for tname, group in pos.groupby("transform"):
        r = group["pred"].mean()
        flag = "  <-- weak" if r < 0.90 else ""
        print(f"  {tname:<12} {r:.4f}  (n={len(group)}){flag}")

    # Baseline comparison
    base = single_hash_baseline(test, val)
    print("\n=== Baseline: best single hash + threshold ===")
    print(f"  {base['feature']} <= {base['threshold']}")
    print(f"  precision {base['precision']:.4f}  recall {base['recall']:.4f}  "
          f"F1 {base['f1']:.4f}")
    print(f"\n  classifier improves F1 by {f1 - base['f1']:+.4f}")

    #What the model learned
    print("\n=== Feature weights (standardised; sign shows direction) ===")
    for name, coef in sorted(zip(FEATURES, model.coef_[0]),
                             key=lambda kv: -abs(kv[1])):
        print(f"  {name:<18} {coef:+.4f}")

    #Export for the C++ runtime
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    weights = {
        "schema_version": 1,
        "model": "logistic_regression",
        "feature_names": FEATURES,
        "mean": scaler.mean_.tolist(),
        "scale": scaler.scale_.tolist(),
        "coef": model.coef_[0].tolist(),
        "intercept": float(model.intercept_[0]),
        "threshold": float(threshold),
        "test_metrics": {
            "precision": float(precision),
            "recall": float(recall),
            "f1": float(f1),
            "average_precision": float(ap_score),
        },
    }
    out_path.write_text(json.dumps(weights, indent=2))
    print(f"\nwrote weights to {out_path}")


if __name__ == "__main__":
    main()