// Langkah 1: kirim data sensor ke komputer lewat USB untuk direkam dengan python/collect_data.py.
// Format tiap baris: S,<ms>,<flex_jempol>,<flex_telunjuk>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>
// Kirim huruf 'c' lewat Serial Monitor (115200 baud) atau lewat collect_data.py untuk kalibrasi.
#include "glove_common.h"

static bool mpuOk = false;
static uint32_t nextTick = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  mpuOk = gloveBegin();
  Serial.println(mpuOk ? "# glove_data_logger siap. Kirim 'c' untuk kalibrasi flex sensor."
                       : "# ERROR: MPU6050 tidak terdeteksi. Cek kabel SDA(21), SCL(22), VCC, GND.");
  nextTick = micros();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') {
      runCalibration();
      nextTick = micros();
    }
  }

  if ((int32_t)(micros() - nextTick) < 0) return;
  nextTick += 1000000UL / SAMPLE_HZ;

  if (!mpuOk) {
    static unsigned long lastWarn = 0;
    if (millis() - lastWarn > 2000) {
      lastWarn = millis();
      Serial.println("# ERROR: MPU6050 tidak terdeteksi. Reset ESP32 setelah memperbaiki kabel.");
    }
    return;
  }

  float s[N_CH];
  if (!readSample(s)) return;
  Serial.printf("S,%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\n",
                (unsigned long)millis(), s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
}
