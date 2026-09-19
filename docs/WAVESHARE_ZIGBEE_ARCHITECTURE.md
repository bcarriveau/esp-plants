# Waveshare + ESP32-H2 Zigbee architecture

## Goal

Build the Waveshare ESP32-S3 7-inch version of ESP PLANTS as a standalone,
local-first plant hub using HOBEIAN ZG-303Z Zigbee plant sensors directly, with
no Home Assistant, MQTT, Zigbee2MQTT, or cloud dependency in the normal runtime
path.

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

## Why Zigbee lives on the H2

The Waveshare display stack in Bill's ESP Aircraft Radar is already physically
proven on Arduino-ESP32 3.0.7. Moving the Zigbee host stack onto the S3 would
force a display-side framework jump just to gain newer generic Zigbee gateway
APIs.

Instead, ESP PLANTS keeps the Waveshare on the proven display generation and
runs the H2 as a native Zigbee coordinator. The H2 has the 802.15.4 radio and
translates Zigbee/Tuya traffic into the small PlantLink protocol.

This reduces coupling: display updates cannot break the Zigbee network, and
Zigbee decoder changes do not require reworking the LCD/touch stack.

## H2 ownership

The H2 owns:

- Zigbee coordinator/network formation and restoration
- permit-join, pairing/removal, sleepy-device handling
- 64-bit IEEE device identity
- raw Zigbee APS observation
- ZG-303Z/Tuya decoding
- supported Zigbee configuration commands
- persistent Zigbee network state

## Waveshare ownership

The Waveshare owns:

- plant slots and plant names
- user thresholds and preferences
- IEEE-address-to-plant assignment
- 7-inch touch UI
- local history
- Wi-Fi/time/web/update experience
- persistent user configuration

A 16-bit Zigbee network address is diagnostic only. The 64-bit IEEE address is
the permanent hardware identity.

## Persistence/update rule

Normal firmware updates must preserve user state and Zigbee network state.
Factory reset is a separate, deliberate action.

The Waveshare partition layout reserves two OTA app slots plus a separate
`plantdata` NVS partition and a separate history partition. The H2 uses the
M5Stack-recommended Zigbee partitions so the Zigbee stack can retain its network
across ordinary reboots/updates.

## Replacement behavior

A plant slot belongs to the user's plant, not permanently to one physical
sensor. Replacing a failed sensor should pair a new IEEE address into the same
plant slot while retaining the plant name, thresholds, and history.
