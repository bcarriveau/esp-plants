#pragma once

#include <stdint.h>

namespace espplants_moisture {

constexpr int8_t kPreferenceMin = -2;
constexpr int8_t kPreferenceMax = 2;

enum class CareState : uint8_t {
  CRITICAL = 0,
  VERY_DRY = 1,
  DRY = 2,
  GOOD = 3,
  WET = 4,
  VERY_WET = 5,
};

struct Thresholds {
  uint8_t critical;
  uint8_t veryDry;
  uint8_t dry;
  uint8_t good;
  uint8_t wet;
};

constexpr Thresholds kThresholds[5] = {
    {4, 8, 15, 35, 55},
    {6, 12, 25, 50, 70},
    {10, 20, 40, 70, 85},
    {18, 32, 50, 78, 90},
    {25, 40, 55, 82, 92},
};

constexpr int8_t clampPreference(int8_t preference) {
  return preference < kPreferenceMin ? kPreferenceMin
       : preference > kPreferenceMax ? kPreferenceMax
                                     : preference;
}

constexpr uint8_t preferenceIndex(int8_t preference) {
  return static_cast<uint8_t>(clampPreference(preference) - kPreferenceMin);
}

constexpr const Thresholds &thresholdsFor(int8_t preference) {
  return kThresholds[preferenceIndex(preference)];
}

constexpr const char *preferenceLabel(int8_t preference) {
  return clampPreference(preference) == -2 ? "MUCH DRIER"
       : clampPreference(preference) == -1 ? "DRIER"
       : clampPreference(preference) == 0  ? "NORMAL"
       : clampPreference(preference) == 1  ? "WETTER"
                                           : "MUCH WETTER";
}

constexpr CareState careState(uint8_t moisturePct, int8_t preference) {
  const Thresholds &t = thresholdsFor(preference);
  return moisturePct <= t.critical ? CareState::CRITICAL
       : moisturePct <= t.veryDry ? CareState::VERY_DRY
       : moisturePct <= t.dry ? CareState::DRY
       : moisturePct <= t.good ? CareState::GOOD
       : moisturePct <= t.wet ? CareState::WET
                              : CareState::VERY_WET;
}

constexpr bool needsWater(CareState state) {
  return state == CareState::CRITICAL || state == CareState::VERY_DRY;
}

struct RankingKey {
  uint8_t band;
  uint16_t positionPermille;
};

constexpr uint8_t bandLower(CareState state, const Thresholds &t) {
  return state == CareState::CRITICAL ? 0
       : state == CareState::VERY_DRY ? static_cast<uint8_t>(t.critical + 1)
       : state == CareState::DRY ? static_cast<uint8_t>(t.veryDry + 1)
       : state == CareState::GOOD ? static_cast<uint8_t>(t.dry + 1)
       : state == CareState::WET ? static_cast<uint8_t>(t.good + 1)
                                : static_cast<uint8_t>(t.wet + 1);
}

constexpr uint8_t bandUpper(CareState state, const Thresholds &t) {
  return state == CareState::CRITICAL ? t.critical
       : state == CareState::VERY_DRY ? t.veryDry
       : state == CareState::DRY ? t.dry
       : state == CareState::GOOD ? t.good
       : state == CareState::WET ? t.wet
                                : 100;
}

constexpr RankingKey rankingKey(uint8_t moisturePct, int8_t preference) {
  const Thresholds &t = thresholdsFor(preference);
  const CareState state = careState(moisturePct, preference);
  const uint8_t lower = bandLower(state, t);
  const uint8_t upper = bandUpper(state, t);
  const uint16_t span = static_cast<uint16_t>(upper - lower);
  const uint16_t position = span == 0
      ? 0
      : static_cast<uint16_t>(
            (static_cast<uint32_t>(moisturePct - lower) * 1000u) / span);
  return {static_cast<uint8_t>(state), position};
}

constexpr bool validThresholds(const Thresholds &t) {
  return t.critical < t.veryDry && t.veryDry < t.dry && t.dry < t.good &&
         t.good < t.wet && t.wet < 100;
}

static_assert(validThresholds(kThresholds[0]), "preference -2 thresholds invalid");
static_assert(validThresholds(kThresholds[1]), "preference -1 thresholds invalid");
static_assert(validThresholds(kThresholds[2]), "preference 0 thresholds invalid");
static_assert(validThresholds(kThresholds[3]), "preference +1 thresholds invalid");
static_assert(validThresholds(kThresholds[4]), "preference +2 thresholds invalid");

static_assert(careState(0, -2) == CareState::CRITICAL, "0 must be critical");
static_assert(careState(0, 2) == CareState::CRITICAL, "0 must be critical");
static_assert(careState(100, -2) == CareState::VERY_WET, "100 must be very wet");
static_assert(careState(100, 2) == CareState::VERY_WET, "100 must be very wet");

}  // namespace espplants_moisture
