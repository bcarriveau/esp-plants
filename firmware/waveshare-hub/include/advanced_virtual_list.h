#pragma once

#include <stddef.h>
#include <stdint.h>

namespace espplants_advanced_virtual_list {

constexpr size_t kPoolSize = 5;
#ifdef ESP_PLANTS_WAVESHARE_7B
constexpr int32_t kViewportHeight = 188;
constexpr int32_t kRowHeight = 63;
constexpr int32_t kRowGap = 8;
#else
constexpr int32_t kViewportHeight = 150;
constexpr int32_t kRowHeight = 50;
constexpr int32_t kRowGap = 6;
#endif
constexpr int32_t kStride = kRowHeight + kRowGap;

inline int32_t contentHeight(size_t logicalCount) {
  if (logicalCount == 0) return kViewportHeight;
  const int32_t logicalHeight =
      static_cast<int32_t>(logicalCount * static_cast<size_t>(kStride)) - kRowGap;
  return logicalHeight > kViewportHeight ? logicalHeight : kViewportHeight;
}

inline int32_t maxScrollY(size_t logicalCount) {
  const int32_t maximum = contentHeight(logicalCount) - kViewportHeight;
  return maximum > 0 ? maximum : 0;
}

inline int32_t clampScrollY(size_t logicalCount, int32_t scrollY) {
  if (scrollY < 0) return 0;
  const int32_t maximum = maxScrollY(logicalCount);
  return scrollY > maximum ? maximum : scrollY;
}

inline size_t firstPoolLogicalIndex(size_t logicalCount, int32_t scrollY) {
  if (logicalCount <= kPoolSize) return 0;

  const int32_t clamped = clampScrollY(logicalCount, scrollY);
  const size_t firstVisible = static_cast<size_t>(clamped / kStride);
  size_t first = firstVisible > 0 ? firstVisible - 1 : 0;  // one row of overscan above
  const size_t maximumFirst = logicalCount - kPoolSize;
  if (first > maximumFirst) first = maximumFirst;
  return first;
}

}  // namespace espplants_advanced_virtual_list
