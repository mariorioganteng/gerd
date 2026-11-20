// MODEL SEMENTARA. File ini akan ditimpa otomatis oleh python/train_model.py.
// Selama masih berisi model sementara, sarung tangan selalu mengirim gesture "open".
#pragma once
#include <stdint.h>

#define GESTURE_MODEL_PLACEHOLDER 1
#define WINDOW_SIZE 50
#define N_CHANNELS 8
#define N_FEATURES 32
#define N_CLASSES 5
#define N_TREES 1
#define N_NODES 1

static const char* const GESTURE_NAMES[N_CLASSES] = {"open", "fist", "point", "pinch", "thumbsup"};
static const int16_t TREE_ROOT[N_TREES] = {0};
static const int8_t NODE_FEATURE[N_NODES] = {-1};
static const float NODE_THRESHOLD[N_NODES] = {0.0f};
static const int16_t NODE_LEFT[N_NODES] = {-1};
static const int16_t NODE_RIGHT[N_NODES] = {-1};
static const uint8_t NODE_CLASS[N_NODES] = {0};
