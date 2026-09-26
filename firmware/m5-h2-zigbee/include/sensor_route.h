#pragma once
#include "plantlink.h"

namespace sensor_route {
// Adapter supplies SDK table entries, restarting each traversal. No retained
// topology or short-address identity: every resolution uses the current tables.
template <class Tables>
void resolve(Tables &tables, const uint8_t ieee[8], uint16_t address,
             plantlink::SensorReportData &out) {
  out.routeState = plantlink::RouteState::UNKNOWN;
  memset(out.repeaterIeee, 0, 8);
  if (address == 0 || address >= 0xfff8) return;
  if (!tables.matchesSensor(ieee, address)) return;
  tables.restartNeighbors();
  while (tables.nextNeighbor()) {
    if (tables.directChild(ieee, address)) {
      out.routeState = plantlink::RouteState::DIRECT;
      return;
    }
  }
  tables.restartRoutes();
  while (tables.nextRoute()) {
    const uint16_t hop = tables.activeNextHop(address);
    if (hop == 0 || hop >= 0xfff8 || hop == address) continue;
    tables.restartNeighbors();
    while (tables.nextNeighbor()) {
      if (tables.liveRouter(hop, out.repeaterIeee)) {
        out.routeState = plantlink::RouteState::ROUTED;
        return;
      }
    }
  }
}
}  // namespace sensor_route
