#pragma once
#include <stdio.h>
#include "plantlink.h"

namespace sensor_route_view {
constexpr uint32_t kInfrastructureLifetimeMs = 10000;
struct Route {
  plantlink::RouteState state = plantlink::RouteState::UNKNOWN;
  uint8_t repeaterIeee[8]{};
  bool update(const plantlink::SensorReportData &report) {
    const bool changed = state != report.routeState ||
                         memcmp(repeaterIeee, report.repeaterIeee, 8) != 0;
    state = report.routeState;
    memcpy(repeaterIeee, report.repeaterIeee, 8);
    return changed;
  }
};
template <class Registry>
void format(const Route &route, const Registry &registry, uint32_t now,
            bool linkReady, char *out, size_t size) {
  if (!size) return;
  out[0] = 0;
  if (!linkReady || route.state != plantlink::RouteState::ROUTED) return;
  for (const auto &node : registry) {
    if (node.used && node.online && now - node.lastSeenMs <= kInfrastructureLifetimeMs &&
        memcmp(node.ieee, route.repeaterIeee, 8) == 0) {
      snprintf(out, size, "VIA: %s", node.name);
      return;
    }
  }
}
}  // namespace sensor_route_view
