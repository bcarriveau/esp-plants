#include <cassert>
#include <cstring>
#include <iostream>
#include "sensor_route.h"
#include "sensor_route_view.h"

struct Node {
  bool used = true, online = true;
  uint8_t ieee[8]{1};
  uint32_t lastSeenMs = 100;
  char name[24] = "KITCHEN REPEATER";
};

struct Tables {
  enum Kind : uint8_t { SENSOR_DIRECT, ROUTER_A, ROUTER_B, OTHER };
  struct Entry {
    Kind kind = OTHER;
    uint8_t lqi = 0;
  } entries[4]{};
  size_t count = 0;
  size_t index = 0;
  bool identityMatches = true;

  bool matchesSensor(const uint8_t *, uint16_t) { return identityMatches; }
  void restartNeighbors() { index = 0; }
  bool nextNeighbor() { return index < count ? (++index, true) : false; }
  const Entry &current() const { return entries[index - 1]; }
  bool directSensor(const uint8_t ieee[8], uint16_t address) const {
    return current().kind == SENSOR_DIRECT && ieee[0] == 9 && address == 0x1234;
  }
  bool routerForPacket(uint8_t packetLqi, uint8_t ieee[8]) const {
    if (current().lqi != packetLqi) return false;
    if (current().kind == ROUTER_A) {
      memset(ieee, 0, 8); ieee[0] = 1; return true;
    }
    if (current().kind == ROUTER_B) {
      memset(ieee, 0, 8); ieee[0] = 2; return true;
    }
    return false;
  }
};

int main() {
  Tables tables;
  uint8_t sensorIeee[8]{9};
  uint8_t repeater[8]{};

  // A coordinator child is intentionally silent even if a router happens to
  // have the same LQI.
  tables.entries[0] = {Tables::SENSOR_DIRECT, 77};
  tables.entries[1] = {Tables::ROUTER_A, 77};
  tables.count = 2;
  assert(!sensor_route::resolveRepeater(tables, sensorIeee, 0x1234, 77, repeater));
  assert(repeater[0] == 0);

  // One live router matching the forwarded packet LQI is positive last-hop evidence.
  tables.entries[0] = {Tables::ROUTER_A, 61};
  tables.entries[1] = {Tables::OTHER, 61};
  tables.count = 2;
  assert(sensor_route::resolveRepeater(tables, sensorIeee, 0x1234, 61, repeater));
  assert(repeater[0] == 1);

  // No matching router remains unknown.
  tables.entries[0] = {Tables::ROUTER_A, 45};
  tables.count = 1;
  assert(!sensor_route::resolveRepeater(tables, sensorIeee, 0x1234, 61, repeater));
  assert(repeater[0] == 0);

  // Two matching routers are ambiguous: never guess.
  tables.entries[0] = {Tables::ROUTER_A, 61};
  tables.entries[1] = {Tables::ROUTER_B, 61};
  tables.count = 2;
  assert(!sensor_route::resolveRepeater(tables, sensorIeee, 0x1234, 61, repeater));
  assert(repeater[0] == 0);

  tables.identityMatches = false;
  assert(!sensor_route::resolveRepeater(tables, sensorIeee, 0x1234, 61, repeater));
  tables.identityMatches = true;
  assert(!sensor_route::resolveRepeater(tables, sensorIeee, 0xffff, 61, repeater));

  // Wire compatibility keeps DIRECT reserved, but the UI intentionally renders
  // only repeater routes.
  sensor_route_view::Route view;
  Node registry[2];
  registry[1].ieee[0] = 2;
  strcpy(registry[1].name, "HALL REPEATER");
  char text[64];
  auto show = [&](uint32_t now = 100, bool ready = true) {
    sensor_route_view::format(view, registry, now, ready, text, sizeof(text));
  };

  plantlink::SensorReportData report;
  report.routeState = plantlink::RouteState::DIRECT;
  assert(view.update(report));
  show(); assert(!text[0]);

  report.routeState = plantlink::RouteState::ROUTED;
  report.repeaterIeee[0] = 1;
  assert(view.update(report));
  show(); assert(!strcmp(text, "VIA: KITCHEN REPEATER"));
  assert(!view.update(report));  // same route identity, no visible redraw needed
  show(101); assert(!strcmp(text, "VIA: KITCHEN REPEATER"));

  report.repeaterIeee[0] = 2;
  assert(view.update(report));
  show(102); assert(!strcmp(text, "VIA: HALL REPEATER"));
  registry[1].online = false; show(102); assert(!text[0]);
  registry[1].online = true;
  show(102, false); assert(!text[0]);

  std::cout << "sensor repeater route scenarios: PASS\n";
}
