#!/usr/bin/env python3
"""Merekam data gesture dari ESP32 yang menjalankan glove_data_logger.ino.

Contoh:
    python collect_data.py --port COM5 --person budi
    python collect_data.py --port /dev/ttyUSB0 --person ani --seconds 10

Hasil: data/<gesture>/<person>_<waktu>.csv
"""
import argparse
import datetime
import os
import sys
import time

import serial
import serial.tools.list_ports

from common import CHANNELS, GESTURES, SAMPLE_HZ

TIPS = {
    "open": "Buka tangan lebar, semua jari lurus.",
    "fist": "Kepalkan tangan, semua jari menekuk.",
    "point": "Telunjuk lurus, jari lain menekuk.",
    "pinch": "Ujung jempol dan telunjuk saling menjepit, jari lain bebas.",
    "thumbsup": "Jempol lurus ke atas, jari lain menekuk.",
}

HEADER = ["t_ms"] + CHANNELS


def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("Tidak ada port serial. Colokkan ESP32 lalu coba lagi.")
    for p in ports:
        print(f"  {p.device}  ({p.description})")


def parse_line(line):
    """Baris data: S,<ms>,<8 nilai>. Baris lain (diawali '#') diabaikan."""
    if not line.startswith("S,"):
        return None
    parts = line.strip().split(",")
    if len(parts) != 2 + len(CHANNELS):
        return None
    try:
        return [float(x) for x in parts[1:]]
    except ValueError:
        return None


def read_text(ser):
    return ser.readline().decode("utf-8", errors="ignore")


def calibrate(ser):
    print("Kalibrasi flex sensor dimulai. Buka dan tutup jempol dan telunjuk penuh, berulang kali, selama 8 detik.")
    ser.reset_input_buffer()
    ser.write(b"c")
    deadline = time.time() + 20
    while time.time() < deadline:
        line = read_text(ser).strip()
        if line.startswith("#"):
            print(line)
            if "CAL DONE" in line:
                return
    print("Tidak ada respon kalibrasi. Pastikan sketch glove_data_logger sedang berjalan.")


def record(ser, seconds):
    n_target = int(seconds * SAMPLE_HZ)
    ser.reset_input_buffer()
    rows = []
    deadline = time.time() + seconds + 5
    last_print = time.time()
    while len(rows) < n_target and time.time() < deadline:
        row = parse_line(read_text(ser))
        if row is not None:
            rows.append(row)
        if time.time() - last_print >= 1:
            last_print = time.time()
            print(f"  merekam... {len(rows) // SAMPLE_HZ}/{int(seconds)} detik", end="\r")
    print(" " * 40, end="\r")
    return rows


def count_files(data_dir, gesture):
    d = os.path.join(data_dir, gesture)
    return len([f for f in os.listdir(d) if f.endswith(".csv")]) if os.path.isdir(d) else 0


def main():
    ap = argparse.ArgumentParser(description="Rekam data gesture dari sarung tangan")
    ap.add_argument("--port", help="Port serial ESP32, misalnya COM5 atau /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--person", default="p1", help="Nama/ID orang yang direkam (dipakai di nama file)")
    ap.add_argument("--seconds", type=float, default=10, help="Durasi tiap rekaman")
    ap.add_argument("--data-dir", default="data")
    args = ap.parse_args()

    if not args.port:
        print("Tentukan port dengan --port. Port yang terdeteksi:")
        list_ports()
        sys.exit(1)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(2)  # ESP32 reset saat port dibuka

    while True:
        print("\n=== Rekam data gesture ===")
        for i, g in enumerate(GESTURES, 1):
            print(f"  {i}. {g:<9} ({count_files(args.data_dir, g)} file)  {TIPS[g]}")
        print("  c. kalibrasi flex sensor    q. keluar")
        choice = input("Pilih nomor gesture: ").strip().lower()
        if choice == "q":
            break
        if choice == "c":
            calibrate(ser)
            continue
        if not choice.isdigit() or not 1 <= int(choice) <= len(GESTURES):
            print("Pilihan tidak valid.")
            continue

        gesture = GESTURES[int(choice) - 1]
        print(f"\nGesture: {gesture}. {TIPS[gesture]}")
        print("Tahan gesture, boleh sedikit menggeser dan memiringkan tangan selama merekam.")
        for s in (3, 2, 1):
            print(f"  mulai dalam {s}...", end="\r")
            time.sleep(1)
        print("  REKAM!            ")

        rows = record(ser, args.seconds)
        min_rows = int(args.seconds * SAMPLE_HZ * 0.8)
        if len(rows) < min_rows:
            print(f"Data kurang ({len(rows)} sampel, butuh minimal {min_rows}). Cek kabel dan port, lalu ulangi.")
            continue

        out_dir = os.path.join(args.data_dir, gesture)
        os.makedirs(out_dir, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(out_dir, f"{args.person}_{stamp}.csv")
        with open(path, "w", encoding="utf-8") as f:
            f.write(",".join(HEADER) + "\n")
            for r in rows:
                f.write(",".join(f"{v:.4f}" if i else f"{int(v)}" for i, v in enumerate(r)) + "\n")
        print(f"Tersimpan: {path} ({len(rows)} sampel)")

    ser.close()


if __name__ == "__main__":
    main()
