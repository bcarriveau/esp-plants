#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "sensor_route.h"
#include "sensor_route_view.h"

struct Node {
  bool used = true, online = true;
  uint8_t ieee[8]{1};
  uint32_t lastSeenMs = 100;
  char name[24] = "KITCHEN REPEATER";
};
struct Tables {
  bool child = false, router = true, active = true;
  uint16_t destination = 0x1234, hop = 0x5678;
  unsigned neighbors = 0, routes = 0;
  uint8_t routerIeee = 1;
  bool identityMatches = true;
  bool matchesSensor(const uint8_t *, uint16_t) { return identityMatches; }
  void restartNeighbors() { neighbors = 0; }
  void restartRoutes() { routes = 0; }
  bool nextNeighbor() { return neighbors++ == 0; }
  bool nextRoute() { return routes++ == 0; }
  bool directChild(const uint8_t ieee[8], uint16_t address) {
    return child && ieee[0] == 9 && address == destination;
  }
  uint16_t activeNextHop(uint16_t address) {
    return active && address == destination ? hop : 0xffff;
  }
  bool liveRouter(uint16_t address, uint8_t ieee[8]) {
    if (!router || address != hop) return false;
    memset(ieee, 0, 8); ieee[0] = routerIeee;
    return true;
  }
};

int main() {
  Tables tables;
  plantlink::SensorReportData report;
  report.ieee[0] = 9;
  report.shortAddress = 0x1234;
  sensor_route_view::Route view;
  Node registry[2]; registry[1].ieee[0] = 2;
  strcpy(registry[1].name, "HALL REPEATER");
  char text[64];
  auto show = [&](uint32_t now = 100, bool ready = true) {
    sensor_route_view::format(view, registry, now, ready, text, sizeof(text));
  };
  auto resolve = [&]() {
    sensor_route::resolve(tables, report.ieee, report.shortAddress, report);
    // Exercise the real v2 wire parser between H2 policy and Waveshare view.
    uint8_t payload[30];
    assert(plantlink::serializeSensorReport(report, payload, sizeof(payload)) == 30);
    plantlink::SensorReportData parsed;
    assert(plantlink::parseSensorReport(payload, sizeof(payload), parsed));
    view.update(parsed, 100);
    show();
  };
  show(); assert(!text[0]);  // reboot starts unknown
  tables.child = true; resolve();
  assert(!strcmp(text, "DIRECT TO HUB"));
  tables.child = false; resolve();
  assert(!strcmp(text, "VIA: KITCHEN REPEATER"));
  strcpy(registry[0].name, "DEN REPEATER"); show();
  assert(!strcmp(text, "VIA: DEN REPEATER")); // no new report required
  registry[0].online = false; show(); assert(!text[0]);
  registry[0].online = true;
  tables.router = false; resolve(); assert(!text[0]); // disappeared next hop
  tables.router = true; tables.active = false; resolve(); assert(!text[0]);
  tables.active = true; tables.routerIeee = 2; resolve();
  assert(!strcmp(text, "VIA: HALL REPEATER"));
  tables.identityMatches = false; resolve(); assert(!text[0]);
  tables.identityMatches = true;
  tables.routerIeee = 3; resolve(); assert(!text[0]); // unregistered/reused short address
  tables.child = true; resolve(); assert(!strcmp(text, "DIRECT TO HUB"));
  show(10101); assert(!text[0]); // topology stream lost, heartbeat may still run
  show(100, false); assert(!text[0]);
  view = {}; show(); assert(!text[0]); // H2/link reset
  tables.child = false; tables.hop = report.shortAddress; resolve(); assert(!text[0]);
  tables.hop = 0; resolve(); assert(!text[0]);
  report.shortAddress = 0xffff; resolve(); assert(!text[0]);
  report.shortAddress = 0x1234; tables.hop = 0x5678; tables.routerIeee = 1;
  resolve(); registry[0].lastSeenMs = 0;
  view.learnedMs = 11000; show(11000); assert(!text[0]); // registry freshness
  view.learnedMs = 0xfffffff0u; registry[0].lastSeenMs = 0xfffffff0u;
  show(16); assert(!strcmp(text, "VIA: DEN REPEATER")); // millis wrap
  std::cout << "sensor route scenarios: PASS\n";
}
