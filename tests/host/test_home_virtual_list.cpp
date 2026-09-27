#include <assert.h>
#include <stddef.h>
#include <initializer_list>

#include "../../firmware/waveshare-hub/include/home_virtual_list.h"

using namespace espplants_home_virtual_list;

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
  static_assert(kPoolSize == 7, "Stage C Home row pool must stay fixed at seven rows");
  static_assert(kRowHeight == 56, "Home row geometry changed");
  static_assert(kRowGap == 7, "Home row gap changed");
  static_assert(kStride == 63, "Home row stride changed");
  static_assert(kViewportHeight == 266, "Home viewport geometry changed");

  assert(contentHeight(0) == 266);
  assert(contentHeight(1) == 266);
  assert(contentHeight(3) == 266);
  assert(contentHeight(10) == 623);
  assert(contentHeight(25) == 1568);
  assert(contentHeight(32) == 2009);

  assert(firstPoolLogicalIndex(32, 0) == 0);
  assert(firstPoolLogicalIndex(32, 62) == 0);
  assert(firstPoolLogicalIndex(32, 63) == 0);
  assert(firstPoolLogicalIndex(32, 126) == 1);
  assert(firstPoolLogicalIndex(32, maxScrollY(32)) == 25);

  for (size_t count : {size_t(0), size_t(1), size_t(3), size_t(10), size_t(25), size_t(32)}) {
    check_population(count);
  }

  return 0;
}
