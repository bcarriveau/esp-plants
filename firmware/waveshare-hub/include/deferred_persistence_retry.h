#pragma once

#include <stdint.h>

// Small, platform-independent helpers for retrying only failed deferred NVS
// writes. No record layout, NVS key, or flash behavior is defined here.
namespace espplants_deferred_persistence {

struct Masks {
  uint32_t settings = 0;
  uint32_t plants = 0;
  uint32_t infrastructure = 0;
  uint32_t moisture = 0;

  bool any() const {
    return settings || plants || infrastructure || moisture;
  }

  void merge(const Masks &other) {
    settings |= other.settings;
    plants |= other.plants;
    infrastructure |= other.infrastructure;
    moisture |= other.moisture;
  }
};

constexpr uint32_t kInitialRetryDelayMs = 5000;
constexpr uint32_t kMaximumRetryDelayMs = 60000;

// The first failed batch retries after 5 seconds; persistent failures back
// off (10, 20, 40, then 60 seconds) without blocking UI or writing to flash
// on every main-loop iteration.
inline uint32_t retryDelayMs(uint8_t failedBatches) {
  uint32_t delay = kInitialRetryDelayMs;
  for (uint8_t i = 1; i < failedBatches && delay < kMaximumRetryDelayMs; ++i) {
    delay = delay > (kMaximumRetryDelayMs / 2)
                ? kMaximumRetryDelayMs
                : delay * 2;
  }
  return delay;
}

}  // namespace espplants_deferred_persistence
