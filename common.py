"""Konfigurasi dan fungsi fitur yang dipakai bersama oleh collect_data.py dan train_model.py.

PENTING: urutan GESTURES menentukan ID gesture di firmware (0 = open, 1 = fist, dst.).
Tabel pemetaan servo di firmware/robot_receiver/robot_receiver.ino memakai urutan yang sama.
"""
import numpy as np

GESTURES = ["open", "fist", "point", "pinch", "thumbsup"]

# Urutan channel harus sama dengan urutan baris "S," dari glove_data_logger.ino
CHANNELS = ["flex_thumb", "flex_index", "ax", "ay", "az", "gx", "gy", "gz"]

SAMPLE_HZ = 50      # harus sama dengan SAMPLE_HZ di glove_common.h
WINDOW_SIZE = 50    # 50 sampel = 1 detik
STRIDE = 10         # jarak antar window saat membuat dataset

STAT_NAMES = ["mean", "std", "min", "max"]
FEATURE_NAMES = [f"{c}_{s}" for c in CHANNELS for s in STAT_NAMES]
N_FEATURES = len(FEATURE_NAMES)  # 8 channel x 4 statistik = 32


def window_features(win):
    """win: array (WINDOW_SIZE, jumlah channel). Mengembalikan vektor fitur 1D.

    Urutan: untuk tiap channel -> mean, std (populasi), min, max.
    Fungsi yang sama ada di firmware/glove_classifier/gesture_classifier.h.
    """
    win = np.asarray(win, dtype=np.float64)
    stats = np.stack([win.mean(axis=0), win.std(axis=0), win.min(axis=0), win.max(axis=0)], axis=1)
    return stats.reshape(-1)
