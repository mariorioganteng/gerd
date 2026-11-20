// Format paket ESP-NOW dari sarung tangan ke robot. Harus sama di kedua sketch.
#pragma once
#include <stdint.h>

#define PACKET_MAGIC 0xA5
#define ESPNOW_CHANNEL 1   // kedua ESP32 harus memakai channel yang sama

struct __attribute__((packed)) GesturePacket {
  uint8_t magic;       // selalu PACKET_MAGIC
  uint8_t gesture;     // ID gesture: 0 open, 1 fist, 2 point, 3 pinch, 4 thumbsup
  uint8_t confidence;  // 0..100, persentase suara mayoritas
  uint8_t seq;         // nomor urut paket
};
