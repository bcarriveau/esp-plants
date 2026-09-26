#pragma once
#include "plantlink.h"

namespace sensor_route {
// The coordinator's route table is not populated for these sleepy end devices.
// Hardware diagnostics showed that a forwarded APS packet updates the immediate
// router neighbor's LQI to the packet LQI. Treat that as last-hop evidence only
// when exactly one live router matches. Direct children and ambiguous matches
// intentionally produce no route metadata.
template <class Tables>
bool resolveRepeater(Tables &tables, const uint8_t ieee[8], uint16_t address,
                     uint8_t packetLqi, uint8_t repeaterIeee[8]) {
  memset(repeaterIeee, 0, 8);
  if (address == 0 || address >= 0xfff8) return false;
  if (!tables.matchesSensor(ieee, address)) return false;

  tables.restartNeighbors();
  while (tables.nextNeighbor()) {
    if (tables.directSensor(ieee, address)) return false;
  }

  bool found = false;
  uint8_t candidate[8]{};
  tables.restartNeighbors();
  while (tables.nextNeighbor()) {
    uint8_t routerIeee[8]{};
    if (!tables.routerForPacket(packetLqi, routerIeee)) continue;
    if (found) {
      // Two routers with the same current LQI are ambiguous. Never guess.
      memset(repeaterIeee, 0, 8);
      return false;
    }
    memcpy(candidate, routerIeee, 8);
    found = true;
  }

  if (!found) return false;
  memcpy(repeaterIeee, candidate, 8);
  return true;
}
}  // namespace sensor_route
