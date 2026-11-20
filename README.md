# Gesture-Controlled Robot Hand

Sarung tangan (ESP32 + 2 flex sensor + MPU6050) mengenali 5 gesture dengan Random Forest,
lalu mengirimnya lewat ESP-NOW ke robot (ESP32 + 3 servo).

```
gesture-robot-hand/
  python/
    common.py          konfigurasi gesture, channel, dan fungsi fitur
    collect_data.py    merekam data dari sarung tangan ke CSV
    train_model.py     melatih 4 model, evaluasi, ekspor Random Forest ke ESP32
    requirements.txt
  firmware/
    glove_data_logger/ Langkah 1: kirim data sensor ke komputer
    glove_classifier/  Langkah 3: klasifikasi di sarung tangan + kirim ke robot
    robot_receiver/    Langkah 3: terima gesture + gerakkan servo
```

## Persiapan

1. Arduino IDE 2.x, lalu tambahkan board **esp32 by Espressif** (Boards Manager). Board: *ESP32 Dev Module*.
2. Library Manager: pasang **ESP32Servo**. Library lain sudah bawaan.
3. Python 3.9 atau lebih baru: `pip install -r python/requirements.txt`

## Pengkabelan

**Sarung tangan (ESP32 #1)**

| Dari | Ke |
|---|---|
| Flex jempol, kaki 1 | 3V3 |
| Flex jempol, kaki 2 | GPIO34 dan resistor 10k ke GND |
| Flex telunjuk, kaki 1 | 3V3 |
| Flex telunjuk, kaki 2 | GPIO35 dan resistor 10k ke GND |
| MPU6050 VCC / GND | 3V3 / GND |
| MPU6050 SDA / SCL | GPIO21 / GPIO22 |
| MPU6050 AD0 | GND (alamat 0x68) |

Flex sensor dipasang sebagai pembagi tegangan: 3V3 - flex - titik ke GPIO - resistor 10k - GND.
Pakai GPIO34/35 (ADC1) karena ADC2 tidak stabil saat WiFi/ESP-NOW aktif.

**Robot (ESP32 #2)**

| Dari | Ke |
|---|---|
| Servo 1 (jempol), sinyal | GPIO18 |
| Servo 2 (telunjuk), sinyal | GPIO19 |
| Servo 3 (jari lain), sinyal | GPIO23 |
| Semua servo, V+ | 5V dari powerbank (JANGAN dari pin ESP32) |
| Semua servo, GND | GND powerbank dan GND ESP32 (harus disambung bersama) |
| Kapasitor 470-1000 uF | di antara 5V dan GND servo, dekat servo |

## Langkah pakai

**1. Kumpulkan data**
1. Upload `firmware/glove_data_logger` ke ESP32 sarung tangan.
2. Jalankan: `python python/collect_data.py --port COM5 --person budi` (di Linux/Mac pakai `/dev/ttyUSB0`).
3. Pilih `c` untuk kalibrasi flex sensor (8 detik, buka-tutup jempol dan telunjuk penuh). Hasilnya tersimpan di memori ESP32.
4. Rekam tiap gesture (pilih nomor 1 sampai 5), 10 detik per rekaman.
   Target minimal: **5 rekaman per gesture**, idealnya dari 3 orang atau lebih. Variasikan posisi dan kemiringan tangan.
   Skrip membuat folder `data/<gesture>/`.

**2. Latih model**
```
cd python
python train_model.py
```
Hasilnya:
- `results/model_comparison.csv`: akurasi dan F1 empat model (bahan slide PPT)
- `results/confusion_matrix.png` dan `results/classification_report.txt`
- `firmware/glove_classifier/gesture_model.h`: model untuk ESP32 (menimpa file sementara)

Validasi silang dikelompokkan per file rekaman, jadi angkanya tidak bocor dari window yang tumpang tindih.

**3. Jalankan**
1. Upload `firmware/glove_classifier` ke ESP32 sarung tangan (setelah `gesture_model.h` terbentuk).
2. Upload `firmware/robot_receiver` ke ESP32 robot.
3. Buka Serial Monitor (115200) di kedua ESP32 untuk melihat gesture yang terdeteksi dan diterima.
4. Kalibrasi servo: ubah `ANGLE_OPEN`, `ANGLE_HALF`, `ANGLE_CLOSED`, dan `SERVO_INVERT` di `robot_receiver.ino` sampai jari bergerak sesuai tanpa memaksa.

## Catatan penting

- **Tabel gesture ke servo** ada di `GESTURE_ANGLES` (robot_receiver.ino). Urutan baris sama dengan `GESTURES` di `python/common.py`
  (0 open, 1 fist, 2 point, 3 pinch, 4 thumbsup). Kalau menambah atau mengubah gesture, ubah di kedua tempat dan latih ulang.
- **Latensi:** window 1 detik membuat respon terhadap gesture baru terasa sekitar 0,3 sampai 0,6 detik. Untuk lebih cepat,
  kecilkan `WINDOW_SIZE` di `python/common.py` (misalnya 25), data tidak perlu direkam ulang, cukup jalankan `train_model.py` lagi.
- **ESP-NOW memakai broadcast,** jadi tidak perlu mengisi alamat MAC. Kedua ESP32 memakai channel 1 (`gesture_packet.h`).
- **Failsafe:** tanpa paket selama 1,5 detik, jari robot kembali terbuka.
- `glove_common.h` dan `gesture_packet.h` ada di lebih dari satu folder sketch (syarat Arduino IDE). Kalau mengubahnya, salin ke folder lain.
- Beberapa powerbank mati otomatis kalau arus yang diambil kecil. Kalau ESP32 sering mati sendiri, coba powerbank lain.

## Pemecahan masalah

| Gejala | Kemungkinan penyebab |
|---|---|
| `ERROR: MPU6050 tidak terdeteksi` | SDA/SCL tertukar, VCC belum tersambung, atau AD0 tidak ke GND |
| Kalibrasi `GAGAL: rentang sensor terlalu kecil` | Resistor 10k belum terpasang, atau flex sensor belum ditekuk penuh |
| `collect_data.py` tidak menerima data | Port salah, Serial Monitor Arduino masih terbuka, atau sketch yang diupload bukan `glove_data_logger` |
| Robot tidak bergerak | Servo tidak punya daya 5V sendiri, GND tidak disambung, atau channel/ESP-NOW belum jalan (lihat Serial Monitor) |
| Servo bergetar atau panas | Sudut melebihi batas mekanik jari. Kecilkan `ANGLE_CLOSED`, tambah kapasitor |
| Gesture sering tertukar | Data kurang atau kurang variasi. Tambah rekaman dari lebih banyak orang, lihat confusion matrix |
