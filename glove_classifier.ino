// Langkah 3 (sarung tangan): baca sensor, klasifikasi gesture dengan model hasil train_model.py,
// lalu kirim ID gesture ke robot lewat ESP-NOW (broadcast, tanpa perlu alamat MAC).
// Kirim 'c' lewat Serial Monitor (115200 baud) untuk kalibrasi ulang flex sensor.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "glove_common.h"
#include "gesture_model.h"
#include "gesture_classifier.h"
#include "gesture_packet.h"

// ---------------- Pengaturan ----------------
#define CLASSIFY_EVERY 5     // klasifikasi tiap 5 sampel (100 ms pada 50 Hz)
#define VOTE_N 5             // keputusan akhir = suara terbanyak dari 5 prediksi terakhir
#define PRINT_DEBUG 1        // 1 = tampilkan hasil di Serial Monitor

static_assert(N_CH == N_CHANNELS, "N_CH di glove_common.h harus sama dengan N_CHANNELS di gesture_model.h");

static const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static float window[WINDOW_SIZE][N_CHANNELS];
static int winPos = 0;
static int winCount = 0;
static int sinceClassify = 0;

static int history[VOTE_N];
static int histPos = 0;
static int histCount = 0;

static uint8_t seqNo = 0;
static uint32_t nextTick = 0;
static bool mpuOk = false;

static void setupEspNow() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW gagal dimulai");
    return;
  }
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: gagal menambah peer broadcast");
  }
}

static void sendGesture(int gesture, uint8_t confidence) {
  GesturePacket pkt;
  pkt.magic = PACKET_MAGIC;
  pkt.gesture = (uint8_t)gesture;
  pkt.confidence = confidence;
  pkt.seq = seqNo++;
  esp_now_send(BROADCAST_ADDR, (const uint8_t *)&pkt, sizeof(pkt));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  mpuOk = gloveBegin();
  if (!mpuOk) Serial.println("ERROR: MPU6050 tidak terdeteksi. Cek kabel SDA(21), SCL(22), VCC, GND.");
#if GESTURE_MODEL_PLACEHOLDER
  Serial.println("PERINGATAN: gesture_model.h masih model sementara. Jalankan python/train_model.py dulu.");
#endif
  setupEspNow();
  Serial.println("glove_classifier siap. Kirim 'c' untuk kalibrasi flex sensor.");
  nextTick = micros();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') {
      runCalibration();
      nextTick = micros();
      winCount = 0;
      winPos = 0;
      histCount = 0;
    }
  }

  if ((int32_t)(micros() - nextTick) < 0) return;
  nextTick += 1000000UL / SAMPLE_HZ;
  if (!mpuOk) return;

  float s[N_CH];
  if (!readSample(s)) return;

  for (int c = 0; c < N_CHANNELS; c++) window[winPos][c] = s[c];
  winPos = (winPos + 1) % WINDOW_SIZE;
  if (winCount < WINDOW_SIZE) winCount++;
  sinceClassify++;

  if (winCount < WINDOW_SIZE || sinceClassify < CLASSIFY_EVERY) return;
  sinceClassify = 0;

  float f[N_FEATURES];
  computeFeatures(window, WINDOW_SIZE, f);
  uint8_t treeConf = 0;
  int g = predictGesture(f, &treeConf);

  history[histPos] = g;
  histPos = (histPos + 1) % VOTE_N;
  if (histCount < VOTE_N) histCount++;

  int counts[N_CLASSES] = {0};
  for (int i = 0; i < histCount; i++) counts[history[i]]++;
  int best = g;  // kalau seri, pilih prediksi terbaru
  for (int k = 0; k < N_CLASSES; k++) {
    if (counts[k] > counts[best]) best = k;
  }
  uint8_t conf = (uint8_t)((100 * counts[best]) / histCount);

  sendGesture(best, conf);
#if PRINT_DEBUG
  Serial.printf("gesture=%s  suara=%d%%  pohon=%d%%\n", GESTURE_NAMES[best], conf, treeConf);
#endif
}
