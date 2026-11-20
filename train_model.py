#!/usr/bin/env python3
"""Melatih model gesture dari data hasil collect_data.py dan mengekspornya ke firmware.

Contoh:
    python train_model.py
    python train_model.py --data-dir data --out ../firmware/glove_classifier/gesture_model.h

Keluaran:
    results/model_comparison.csv      perbandingan akurasi dan F1 empat model
    results/confusion_matrix.png      confusion matrix Random Forest (validasi silang)
    results/classification_report.txt precision, recall, F1 per gesture
    ../firmware/glove_classifier/gesture_model.h  model untuk ESP32
"""
import argparse
import glob
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import ConfusionMatrixDisplay, classification_report, confusion_matrix
from sklearn.model_selection import StratifiedGroupKFold, cross_val_predict, cross_validate
from sklearn.neighbors import KNeighborsClassifier
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.tree import DecisionTreeClassifier

from common import (CHANNELS, FEATURE_NAMES, GESTURES, N_FEATURES, SAMPLE_HZ,
                    STRIDE, WINDOW_SIZE, window_features)

RANDOM_STATE = 42
N_TREES = 10
MAX_DEPTH = 8


# ---------------------------------------------------------------- dataset
def load_dataset(data_dir):
    X, y, groups = [], [], []
    files_per_gesture = {g: 0 for g in GESTURES}
    group_id = 0
    for label, gesture in enumerate(GESTURES):
        for path in sorted(glob.glob(os.path.join(data_dir, gesture, "*.csv"))):
            df = pd.read_csv(path)
            missing = [c for c in CHANNELS if c not in df.columns]
            if missing:
                print(f"Lewati {path}: kolom {missing} tidak ada")
                continue
            data = df[CHANNELS].to_numpy(dtype=float)
            if len(data) < WINDOW_SIZE:
                print(f"Lewati {path}: hanya {len(data)} sampel")
                continue
            if "t_ms" in df.columns and len(df) > 2:
                dt = np.median(np.diff(df["t_ms"].to_numpy(dtype=float)))
                if dt > 0 and abs(1000.0 / dt - SAMPLE_HZ) > 5:
                    print(f"Peringatan {path}: laju sampling {1000.0 / dt:.1f} Hz, seharusnya {SAMPLE_HZ} Hz")
            for start in range(0, len(data) - WINDOW_SIZE + 1, STRIDE):
                X.append(window_features(data[start:start + WINDOW_SIZE]))
                y.append(label)
                groups.append(group_id)
            files_per_gesture[gesture] += 1
            group_id += 1
    return np.array(X), np.array(y), np.array(groups), files_per_gesture


# ------------------------------------------------------------- hard voting
def hard_vote_predict(rf, X):
    """Meniru persis cara ESP32 memprediksi: tiap pohon memilih satu kelas, suara terbanyak menang."""
    X32 = np.asarray(X, dtype=np.float32)
    votes = np.zeros((len(X32), len(GESTURES)), dtype=int)
    rows = np.arange(len(X32))
    for est in rf.estimators_:
        leaf = est.apply(X32)
        cls = est.tree_.value[leaf, 0, :].argmax(axis=1)
        votes[rows, cls] += 1
    return votes.argmax(axis=1)


# ------------------------------------------------------------- export C header
def floor_float32(x):
    """Pembulatan ke float32 ke arah bawah, supaya (f <= ambang) di ESP32 sama dengan scikit-learn."""
    t = np.float32(x)
    if float(t) > x:
        t = np.nextafter(t, np.float32(-np.inf))
    return float(t)


def c_float(v):
    """Menulis angka sebagai literal float C yang valid (selalu ada titik atau eksponen)."""
    t = f"{v:.9g}"
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def export_header(rf, path):
    feature, threshold, left, right, node_class, roots = [], [], [], [], [], []
    for est in rf.estimators_:
        t = est.tree_
        base = len(feature)
        roots.append(base)
        for n in range(t.node_count):
            if t.children_left[n] == -1:  # daun
                feature.append(-1)
                threshold.append(0.0)
                left.append(-1)
                right.append(-1)
                node_class.append(int(t.value[n, 0, :].argmax()))
            else:
                feature.append(int(t.feature[n]))
                threshold.append(floor_float32(float(t.threshold[n])))
                left.append(base + int(t.children_left[n]))
                right.append(base + int(t.children_right[n]))
                node_class.append(0)
    n_nodes = len(feature)
    if n_nodes >= 32767:
        raise SystemExit("Model terlalu besar. Kurangi N_TREES atau MAX_DEPTH di train_model.py.")

    def arr(vals, fmt):
        out, line = [], []
        for v in vals:
            line.append(fmt(v))
            if len(line) == 12:
                out.append("  " + ", ".join(line))
                line = []
        if line:
            out.append("  " + ", ".join(line))
        return ",\n".join(out)

    names = ", ".join(f'"{g}"' for g in GESTURES)
    text = f"""// DIBUAT OTOMATIS oleh python/train_model.py. Jangan diedit manual.
// Model: Random Forest, {len(roots)} pohon, {n_nodes} node, {N_FEATURES} fitur, {len(GESTURES)} kelas.
#pragma once
#include <stdint.h>

#define GESTURE_MODEL_PLACEHOLDER 0
#define WINDOW_SIZE {WINDOW_SIZE}
#define N_CHANNELS {len(CHANNELS)}
#define N_FEATURES {N_FEATURES}
#define N_CLASSES {len(GESTURES)}
#define N_TREES {len(roots)}
#define N_NODES {n_nodes}

static const char* const GESTURE_NAMES[N_CLASSES] = {{{names}}};

static const int16_t TREE_ROOT[N_TREES] = {{
{arr(roots, str)}
}};
static const int8_t NODE_FEATURE[N_NODES] = {{
{arr(feature, str)}
}};
static const float NODE_THRESHOLD[N_NODES] = {{
{arr(threshold, c_float)}
}};
static const int16_t NODE_LEFT[N_NODES] = {{
{arr(left, str)}
}};
static const int16_t NODE_RIGHT[N_NODES] = {{
{arr(right, str)}
}};
static const uint8_t NODE_CLASS[N_NODES] = {{
{arr(node_class, str)}
}};
"""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return n_nodes


# ------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Latih dan ekspor model gesture")
    ap.add_argument("--data-dir", default="data")
    ap.add_argument("--out", default=os.path.join("..", "firmware", "glove_classifier", "gesture_model.h"))
    ap.add_argument("--results-dir", default="results")
    args = ap.parse_args()

    X, y, groups, files = load_dataset(args.data_dir)
    print("Jumlah file rekaman per gesture:", files)
    if len(X) == 0:
        sys.exit("Tidak ada data. Jalankan collect_data.py dulu.")
    empty = [g for g, n in files.items() if n == 0]
    if empty:
        sys.exit(f"Gesture belum punya data: {empty}")
    min_files = min(files.values())
    if min_files < 2:
        sys.exit("Setiap gesture butuh minimal 2 file rekaman (disarankan 5 atau lebih, dari beberapa orang).")

    n_splits = min(5, min_files)
    cv = StratifiedGroupKFold(n_splits=n_splits, shuffle=True, random_state=RANDOM_STATE)
    print(f"{len(X)} window, {N_FEATURES} fitur, validasi silang {n_splits}-fold (dikelompokkan per file rekaman)\n")

    models = {
        "KNN (k=5)": make_pipeline(StandardScaler(), KNeighborsClassifier(n_neighbors=5)),
        "Decision Tree": DecisionTreeClassifier(max_depth=MAX_DEPTH, random_state=RANDOM_STATE),
        "Random Forest": RandomForestClassifier(n_estimators=N_TREES, max_depth=MAX_DEPTH, random_state=RANDOM_STATE),
        "MLP mini": make_pipeline(StandardScaler(), MLPClassifier(hidden_layer_sizes=(32, 16), max_iter=2000, random_state=RANDOM_STATE)),
    }

    os.makedirs(args.results_dir, exist_ok=True)
    rows = []
    for name, model in models.items():
        res = cross_validate(model, X, y, cv=cv, groups=groups, scoring=["accuracy", "f1_macro"])
        rows.append({
            "model": name,
            "accuracy_mean": res["test_accuracy"].mean(),
            "accuracy_std": res["test_accuracy"].std(),
            "f1_macro_mean": res["test_f1_macro"].mean(),
        })
    table = pd.DataFrame(rows)
    table.to_csv(os.path.join(args.results_dir, "model_comparison.csv"), index=False)
    print("Perbandingan model (validasi silang):")
    print(table.to_string(index=False, float_format=lambda v: f"{v:.3f}"))

    # Evaluasi detail untuk Random Forest (model yang diekspor ke ESP32)
    rf = models["Random Forest"]
    pred = cross_val_predict(rf, X, y, cv=cv, groups=groups)
    report = classification_report(y, pred, target_names=GESTURES, digits=3)
    print("\nRandom Forest, laporan per gesture:\n" + report)
    with open(os.path.join(args.results_dir, "classification_report.txt"), "w", encoding="utf-8") as f:
        f.write(report)
    cm = confusion_matrix(y, pred)
    fig, ax = plt.subplots(figsize=(6, 5))
    ConfusionMatrixDisplay(cm, display_labels=GESTURES).plot(ax=ax, cmap="Blues", colorbar=False)
    ax.set_title("Confusion matrix, Random Forest")
    fig.tight_layout()
    fig.savefig(os.path.join(args.results_dir, "confusion_matrix.png"), dpi=150)

    # Latih ulang dengan semua data, lalu ekspor
    rf.fit(X, y)
    parity = (hard_vote_predict(rf, X) == rf.predict(X)).mean()
    n_nodes = export_header(rf, args.out)
    print(f"Model diekspor ke {args.out}")
    print(f"  {N_TREES} pohon, {n_nodes} node, kira-kira {n_nodes * 11} byte flash")
    print(f"  Kesesuaian voting ESP32 dengan scikit-learn pada data latih: {parity * 100:.1f}%")


if __name__ == "__main__":
    main()
