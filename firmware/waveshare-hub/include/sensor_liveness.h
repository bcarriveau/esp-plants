#pragma once

#include <stdint.h>

namespace espplants_sensor_liveness {

constexpr uint32_t kBootstrapTimeoutMs = 2UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kMinimumCadenceSampleGapMs = 60UL * 1000UL;
constexpr uint32_t kMinimumTimeoutMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kMaximumCadenceMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kMaximumTimeoutMs = 3UL * 60UL * 60UL * 1000UL;
constexpr uint8_t kMissedPeriodsBeforeStale = 3;
constexpr uint8_t kCadenceSamplesRequired = 3;

struct Tracker {
  uint32_t learnedCadenceMs = 0;
  uint8_t learnedSamples = 0;
  bool stale = false;
};

inline uint32_t timeoutMs(const Tracker &tracker) {
  if (tracker.learnedSamples < kCadenceSamplesRequired || tracker.learnedCadenceMs == 0) {
    return kBootstrapTimeoutMs;
  }

  uint32_t timeout = tracker.learnedCadenceMs * kMissedPeriodsBeforeStale;
  if (timeout < kMinimumTimeoutMs) timeout = kMinimumTimeoutMs;
  if (timeout > kMaximumTimeoutMs) timeout = kMaximumTimeoutMs;
  return timeout;
}

inline bool shouldBeStale(const Tracker &tracker, bool seenThisBoot,
                          uint32_t lastSeenMs, uint32_t nowMs) {
  if (!seenThisBoot || lastSeenMs == 0) return false;
  return static_cast<uint32_t>(nowMs - lastSeenMs) >= timeoutMs(tracker);
}

inline void noteReport(Tracker &tracker, uint32_t previousLastSeenMs,
                       uint32_t nowMs) {
  const bool wasStale = tracker.stale;

  if (!wasStale && previousLastSeenMs != 0) {
    const uint32_t gapMs = static_cast<uint32_t>(nowMs - previousLastSeenMs);
    if (gapMs >= kMinimumCadenceSampleGapMs) {
      const uint32_t boundedGap =
          gapMs > kMaximumCadenceMs ? kMaximumCadenceMs : gapMs;
      if (tracker.learnedSamples == 0 || boundedGap > tracker.learnedCadenceMs) {
        tracker.learnedCadenceMs = boundedGap;
      }
      if (tracker.learnedSamples < UINT8_MAX) ++tracker.learnedSamples;
    }
  }

  tracker.stale = false;
}

}  // namespace espplants_sensor_liveness
