# Design decisions

This file records architectural decisions so future changes do not accidentally
undo the reasons behind the current design.

## D001 - Separate T5 and XIAO PlatformIO projects

**Decision:** Keep them as separate build roots.

**Reason:** They require different ESP32 Arduino/toolchain generations. A
combined project previously caused package/toolchain collisions.

**Consequence:** Shared protocol code is duplicated and must be synchronization
checked.

## D002 - Plant names live only on the T5

**Decision:** Sensors send stable IDs, not human plant names.

**Reason:** A rename should happen once. The user should not have to edit names
in sensor firmware, hub code, and external entities.

**Consequence:** T5 NVS is authoritative for `sensor_id -> plant_name`.

## D003 - Normal telemetry uses home Wi-Fi / UDP

**Decision:** Use the household router/mesh for normal sensor range.

**Reason:** Direct ESP-NOW worked for the proof of concept but fringe placement
made reliable bidirectional communication too dependent on hub location.

**Consequence:** The product remains local but benefits from existing whole-home
network coverage.

## D004 - ESP-NOW remains for nearby provisioning

**Decision:** Keep ESP-NOW for short-range local setup.

**Reason:** It allows a sensor with no Wi-Fi configuration to receive credentials
without adding a separate screen or keyboard to the sensor.

**Consequence:** Provisioning security must be hardened before production.

## D005 - Sensor decides whether Wi-Fi is worth turning on

**Decision:** Local measurement occurs before Wi-Fi startup.

**Reason:** Wi-Fi dominates the cost of a routine wake. Stable plants should not
pay that cost every local check.

**Consequence:** Sensor retains enough adaptive state across deep sleep to make
the report decision.

## D006 - Watering causes temporary higher sampling density

**Decision:** After detecting a watering rise, follow up in 5 minutes and then
10-minute intervals until settled.

**Reason:** A probe can become immediately wet when water reaches it, then settle
to a lower representative value. A normal 30-minute WET interval would hide that
important settling behavior.

## D007 - Calibration lives on the physical sensor

**Decision:** Store dry/wet endpoints on the XIAO.

**Reason:** Calibration describes the individual probe/electronics, not the
plant's human name.

**Consequence:** Calibration survives plant renames and T5 display changes.

## D008 - T5 ACK scheduling field remains compatibility-only for now

**Decision:** Keep the v3 ACK field but do not let it override Phase 3D+ sensor
adaptive scheduling.

**Reason:** Avoid a gratuitous protocol change while the smarter sensor-side
scheduler is being proven.

**Future:** If T5-configurable timing is added, introduce an explicit
configuration design rather than relying on ambiguous legacy behavior.
