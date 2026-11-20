// Ekstraksi fitur dan inferensi Random Forest. Tanpa dependensi Arduino.
// Harus di-include setelah gesture_model.h.
// Urutan fitur sama dengan window_features() di python/common.py:
// untuk tiap channel -> mean, std (populasi), min, max.
#pragma once
#include <stdint.h>
#include <math.h>

static inline void computeFeatures(const float win[][N_CHANNELS], int n, float *f) {
  for (int c = 0; c < N_CHANNELS; c++) {
    float sum = 0.0f;
    float mn = win[0][c];
    float mx = win[0][c];
    for (int i = 0; i < n; i++) {
      float v = win[i][c];
      sum += v;
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    float mean = sum / n;
    float var = 0.0f;
    for (int i = 0; i < n; i++) {
      float d = win[i][c] - mean;
      var += d * d;
    }
    var /= n;
    f[c * 4 + 0] = mean;
    f[c * 4 + 1] = sqrtf(var);
    f[c * 4 + 2] = mn;
    f[c * 4 + 3] = mx;
  }
}

// Mengembalikan ID gesture. *confidence diisi 0..100 (persentase pohon yang setuju).
static inline int predictGesture(const float *f, uint8_t *confidence) {
  uint8_t votes[N_CLASSES] = {0};
  for (int t = 0; t < N_TREES; t++) {
    int n = TREE_ROOT[t];
    while (NODE_FEATURE[n] >= 0) {
      n = (f[NODE_FEATURE[n]] <= NODE_THRESHOLD[n]) ? NODE_LEFT[n] : NODE_RIGHT[n];
    }
    votes[NODE_CLASS[n]]++;
  }
  int best = 0;
  for (int k = 1; k < N_CLASSES; k++) {
    if (votes[k] > votes[best]) best = k;
  }
  if (confidence) *confidence = (uint8_t)((100 * votes[best]) / N_TREES);
  return best;
}
