#pragma once
#include <stdio.h>
#include "plantlink.h"

namespace sensor_route_view {
constexpr uint32_t kLifetimeMs = 10000;
struct Route {
  plantlink::RouteState state = plantlink::RouteState::UNKNOWN;
  uint8_t repeaterIeee[8]{};
  uint32_t learnedMs = 0;
  void update(const plantlink::SensorReportData &report, uint32_t now) {
    state = report.routeState;
    memcpy(repeaterIeee, report.repeaterIeee, 8);
    learnedMs = now;
  }
};
template <class Registry>
void format(const Route &route, const Registry &registry, uint32_t now,
            bool linkReady, char *out, size_t size) {
  if (!size) return;
  out[0] = 0;
  if (!linkReady || now - route.learnedMs > kLifetimeMs) return;
  if (route.state == plantlink::RouteState::DIRECT) {
    snprintf(out, size, "DIRECT TO HUB");
  } else if (route.state == plantlink::RouteState::ROUTED) {
    for (const auto &node : registry) {
      if (node.used && node.online && now - node.lastSeenMs <= kLifetimeMs &&
          memcmp(node.ieee, route.repeaterIeee, 8) == 0) {
        snprintf(out, size, "VIA: %s", node.name);
        return;
      }
    }
  }
}
}  // namespace sensor_route_view
