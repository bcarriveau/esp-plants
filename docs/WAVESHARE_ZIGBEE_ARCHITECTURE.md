# Waveshare + ESP32-H2 Zigbee architecture

## Goal

Build the Waveshare ESP32-S3 7-inch version of ESP PLANTS as a standalone,
local-first plant hub using HOBEIAN ZG-303Z Zigbee plant sensors directly, with
no Home Assistant requirement.

## System boundary

```text
HOBEIAN ZG-303Z sensors
        |
        | Zigbee
        v
M5Stack ESP32-H2
        |
        | PlantLink UART
        v
Waveshare ESP32-S3
        |
        +-- 7-inch touch UI
        +-- plant configuration/history
        +-- Wi-Fi/time/update services
```

## H2 ownership

The H2 is the Zigbee appliance. It owns network formation/restoration,
permit-join, pairing/removal, sleepy-device handling, raw Zigbee reports,
ZG-303Z/Tuya decoding, supported Zigbee configuration commands, and persistent
Zigbee network state.

The H2 reports normalized plant telemetry over PlantLink and exposes unknown
vendor datapoints through diagnostics rather than silently discarding them.

## Waveshare ownership

The Waveshare is the product/application controller. It owns plant slots,
names, thresholds, display behavior, local history, Wi-Fi, time, setup, and
firmware-update UX.

Plant names remain hub-only. The permanent key for a Zigbee sensor is its
64-bit IEEE address. The Zigbee 16-bit network address is transient and must not
be used as permanent identity.

## Persistence

Normal firmware updates must preserve user state.

Waveshare persistent state includes at minimum:

- Wi-Fi/user setup
- plant names and slots
- IEEE-address-to-plant assignments
- user thresholds/preferences
- history/config schema version

H2 persistent state includes at minimum:

- Zigbee network identity/keys required to restore the coordinator network
- paired-device metadata needed by the H2 implementation
- H2 configuration schema version

A factory-reset operation is distinct from a normal firmware update.

## Replacement behavior

A plant slot belongs to the user's plant, not permanently to one sensor.
Replacing a failed sensor should pair a new IEEE address into the existing plant
slot while retaining the plant name, thresholds, and history.

## Debugging

The H2 USB-C connection should provide human-readable diagnostics. PlantLink
UART stays machine-framed so debug text cannot corrupt inter-processor traffic.

The Waveshare native USB connection should be used for its own upload/debug
workflow, leaving the H2 UART link dedicated to PlantLink.

## Hardware verification rule

Do not freeze connector pin order, voltage selection, UART GPIO mapping, or
baud rate as hardware-proven until checked against current documentation and
then physically verified on the actual units.
