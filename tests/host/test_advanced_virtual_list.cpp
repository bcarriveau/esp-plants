#include <assert.h>
#include <stddef.h>
#include <initializer_list>

#include "../../firmware/waveshare-hub/include/advanced_virtual_list.h"

using namespace espplants_advanced_virtual_list;

static void check_population(size_t count) {
  assert(contentHeight(count) >= kViewportHeight);
  assert(maxScrollY(count) >= 0);
  assert(clampScrollY(count, -500) == 0);
  assert(clampScrollY(count, 100000) == maxScrollY(count));
  const size_t first = firstPoolLogicalIndex(count, maxScrollY(count));
  if (count <= kPoolSize) assert(first == 0);
  else assert(first <= count - kPoolSize);
}

int main() {
  static_assert(kPoolSize == 5, "Stage B row pool must stay fixed at five rows");
  static_assert(kRowHeight == 50, "Advanced row geometry changed");
  static_assert(kRowGap == 6, "Advanced row gap changed");
  static_assert(kStride == 56, "Advanced row stride changed");
  static_assert(kViewportHeight == 150, "Advanced viewport geometry changed");

  assert(contentHeight(0) == 150);
  assert(contentHeight(1) == 150);
  assert(contentHeight(3) == 162);
  assert(contentHeight(10) == 554);
  assert(contentHeight(25) == 1394);
  assert(contentHeight(32) == 1786);

  assert(firstPoolLogicalIndex(32, 0) == 0);
  assert(firstPoolLogicalIndex(32, 55) == 0);
  assert(firstPoolLogicalIndex(32, 56) == 0);
  assert(firstPoolLogicalIndex(32, 112) == 1);
  assert(firstPoolLogicalIndex(32, maxScrollY(32)) == 27);

  for (size_t count : {size_t(0), size_t(1), size_t(3), size_t(10), size_t(25), size_t(32)}) {
    check_population(count);
  }

  return 0;
}
