// Bagian bersama untuk sarung tangan: pin, pembacaan sensor, dan kalibrasi flex sensor.
// File ini ada di dua folder (glove_data_logger dan glove_classifier). Isinya harus sama.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

// ---------------- Pin (ESP32 DevKit V1, 30 pin) ----------------
#define PIN_FLEX_THUMB 34   // flex jempol  (ADC1, input only)
#define PIN_FLEX_INDEX 35   // flex telunjuk (ADC1, input only)
#define PIN_SDA 21          // MPU6050 SDA
#define PIN_SCL 22          // MPU6050 SCL
#define MPU_ADDR 0x68       // pin AD0 MPU6050 disambung ke GND

// ---------------- Pengaturan sampling ----------------
#define SAMPLE_HZ 50        // harus sama dengan SAMPLE_HZ di python/common.py
#define N_CH 8              // 2 flex + 3 akselerometer + 3 gyro
#define FLEX_OVERSAMPLE 4   // rata-rata beberapa pembacaan ADC untuk mengurangi noise

static const uint8_t FLEX_PINS[2] = {PIN_FLEX_THUMB, PIN_FLEX_INDEX};
static uint16_t flexMin[2] = {0, 0};
static uint16_t flexMax[2] = {4095, 4095};
static Preferences prefs;

static void loadCalibration() {
  prefs.begin("flexcal", false);
  flexMin[0] = prefs.getUShort("min0", 0);
  flexMax[0] = prefs.getUShort("max0", 4095);
  flexMin[1] = prefs.getUShort("min1", 0);
  flexMax[1] = prefs.getUShort("max1", 4095);
  prefs.end();
}

static void saveCalibration() {
  prefs.begin("flexcal", false);
  prefs.putUShort("min0", flexMin[0]);
  prefs.putUShort("max0", flexMax[0]);
  prefs.putUShort("min1", flexMin[1]);
  prefs.putUShort("max1", flexMax[1]);
  prefs.end();
}

static float readFlexRaw(int i) {
  uint32_t sum = 0;
  for (int k = 0; k < FLEX_OVERSAMPLE; k++) sum += analogRead(FLEX_PINS[i]);
  return (float)sum / FLEX_OVERSAMPLE;
}

static int16_t read16() {
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}

static bool mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Mengembalikan false kalau MPU6050 tidak terdeteksi.
static bool gloveBegin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  loadCalibration();
  if (!mpuWrite(0x6B, 0x00)) return false;  // bangunkan sensor
  mpuWrite(0x1A, 0x03);                     // low-pass filter sekitar 44 Hz
  mpuWrite(0x1B, 0x00);                     // gyro +-250 derajat/detik
  mpuWrite(0x1C, 0x00);                     // akselerometer +-2 g
  return true;
}

// Mengisi out[N_CH]: flex jempol, flex telunjuk (0..1), ax ay az (g), gx gy gz (derajat/detik).
static bool readSample(float *out) {
  for (int i = 0; i < 2; i++) {
    float raw = readFlexRaw(i);
    float lo = flexMin[i];
    float hi = flexMax[i];
    if (hi - lo < 50) { lo = 0; hi = 4095; }  // kalibrasi belum ada atau tidak valid
    float n = (raw - lo) / (hi - lo);
    out[i] = n < 0 ? 0 : (n > 1 ? 1 : n);
  }
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14, (uint8_t)true) != 14) return false;
  int16_t ax = read16();
  int16_t ay = read16();
  int16_t az = read16();
  read16();  // suhu, tidak dipakai
  int16_t gx = read16();
  int16_t gy = read16();
  int16_t gz = read16();
  out[2] = ax / 16384.0f;
  out[3] = ay / 16384.0f;
  out[4] = az / 16384.0f;
  out[5] = gx / 131.0f;
  out[6] = gy / 131.0f;
  out[7] = gz / 131.0f;
  return true;
}

// Kalibrasi 8 detik: buka dan tutup jempol dan telunjuk penuh berulang kali.
// Hasil disimpan di memori ESP32 (NVS), jadi tidak perlu diedit di kode.
static void runCalibration() {
  Serial.println("# KALIBRASI: buka dan tutup jempol dan telunjuk penuh, berulang kali, selama 8 detik");
  uint16_t mn[2] = {4095, 4095};
  uint16_t mx[2] = {0, 0};
  unsigned long t0 = millis();
  unsigned long lastPrint = t0;
  while (millis() - t0 < 8000) {
    for (int i = 0; i < 2; i++) {
      uint16_t v = (uint16_t)readFlexRaw(i);
      if (v < mn[i]) mn[i] = v;
      if (v > mx[i]) mx[i] = v;
    }
    if (millis() - lastPrint >= 1000) {
      lastPrint += 1000;
      Serial.printf("# sisa %lu detik | jempol %u-%u | telunjuk %u-%u\n",
                    (unsigned long)((8000 - (millis() - t0)) / 1000),
                    (unsigned)mn[0], (unsigned)mx[0], (unsigned)mn[1], (unsigned)mx[1]);
    }
    delay(10);
  }
  if ((mx[0] - mn[0] < 200) || (mx[1] - mn[1] < 200)) {
    Serial.println("# GAGAL: rentang sensor terlalu kecil. Cek kabel flex sensor dan resistor 10k.");
  } else {
    flexMin[0] = mn[0]; flexMax[0] = mx[0];
    flexMin[1] = mn[1]; flexMax[1] = mx[1];
    saveCalibration();
    Serial.println("# Kalibrasi tersimpan di memori ESP32.");
  }
  Serial.println("# CAL DONE");
}
