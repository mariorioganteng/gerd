// Langkah 3 (robot): terima ID gesture lewat ESP-NOW lalu gerakkan 3 servo.
// Servo 1 = jempol, servo 2 = telunjuk, servo 3 = jari tengah + manis + kelingking.
// Library yang dibutuhkan: "ESP32Servo" (Library Manager Arduino IDE).
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include "gesture_packet.h"

// ---------------- Pengaturan servo ----------------
static const uint8_t SERVO_PINS[3] = {18, 19, 23};
static const bool SERVO_INVERT[3] = {false, false, false};  // true kalau arah servo terbalik

// Batasi sudut sesuai mekanik jari agar servo tidak memaksa dan panas. Atur setelah uji coba.
#define ANGLE_OPEN 0       // jari lurus
#define ANGLE_HALF 60      // setengah menekuk
#define ANGLE_CLOSED 120   // jari menekuk penuh

#define SERVO_STEP_DEG 4       // langkah per update, makin kecil makin halus
#define SERVO_UPDATE_MS 15     // jeda antar update
#define FAILSAFE_MS 1500       // tanpa paket selama ini, jari kembali ke posisi terbuka

#define O ANGLE_OPEN
#define H ANGLE_HALF
#define C ANGLE_CLOSED
// Baris = ID gesture (urutan sama dengan GESTURES di python/common.py).
// Kolom = servo jempol, telunjuk, jari lain.
static const uint8_t GESTURE_ANGLES[5][3] = {
  {O, O, O},  // 0 open
  {C, C, C},  // 1 fist
  {C, O, C},  // 2 point
  {H, H, O},  // 3 pinch
  {O, C, C},  // 4 thumbsup
};
#undef O
#undef H
#undef C

static Servo servos[3];
static int currentAngle[3];
static int targetAngle[3];

static volatile uint8_t lastGesture = 0;
static volatile uint8_t lastConfidence = 0;
static volatile uint32_t lastRxMs = 0;
static volatile bool newPacket = false;

static int toServo(int i, int logicalAngle) {
  return SERVO_INVERT[i] ? (ANGLE_OPEN + ANGLE_CLOSED - logicalAngle) : logicalAngle;
}

static void handlePacket(const uint8_t *data, int len) {
  if (len != (int)sizeof(GesturePacket)) return;
  GesturePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  if (pkt.magic != PACKET_MAGIC || pkt.gesture >= 5) return;
  lastGesture = pkt.gesture;
  lastConfidence = pkt.confidence;
  lastRxMs = millis();
  newPacket = true;
}

// Penanda callback berbeda antara ESP32 Arduino core 2.x dan 3.x.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  handlePacket(data, len);
}
#else
static void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  handlePacket(data, len);
}
#endif

static void setTarget(int gesture) {
  for (int i = 0; i < 3; i++) targetAngle[i] = toServo(i, GESTURE_ANGLES[gesture][i]);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  for (int i = 0; i < 3; i++) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(SERVO_PINS[i], 500, 2400);
    currentAngle[i] = toServo(i, ANGLE_OPEN);
    targetAngle[i] = currentAngle[i];
    servos[i].write(currentAngle[i]);
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW gagal dimulai");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  Serial.println("robot_receiver siap. Menunggu paket dari sarung tangan...");
}

void loop() {
  static uint32_t lastUpdate = 0;
  static bool failsafeActive = false;

  if (newPacket) {
    newPacket = false;
    setTarget(lastGesture);
    failsafeActive = false;
    Serial.printf("gesture=%d  keyakinan=%d%%\n", (int)lastGesture, (int)lastConfidence);
  }

  if (!failsafeActive && lastRxMs != 0 && millis() - lastRxMs > FAILSAFE_MS) {
    failsafeActive = true;
    setTarget(0);
    Serial.println("Tidak ada paket, jari kembali terbuka.");
  }

  if (millis() - lastUpdate >= SERVO_UPDATE_MS) {
    lastUpdate = millis();
    for (int i = 0; i < 3; i++) {
      if (currentAngle[i] == targetAngle[i]) continue;
      int diff = targetAngle[i] - currentAngle[i];
      int step = diff > 0 ? min(diff, SERVO_STEP_DEG) : -min(-diff, SERVO_STEP_DEG);
      currentAngle[i] += step;
      servos[i].write(currentAngle[i]);
    }
  }
}
