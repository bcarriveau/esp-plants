# Architecture

## Active product

ESP PLANTS is a standalone local appliance built around:

```text
HOBEIAN ZG-303Z Zigbee sensors
          |
        Zigbee
          |
M5Stack ESP32-H2 Gateway Unit
          |
     PlantLink UART
          |
Waveshare ESP32-S3 800x480 display
```

No Home Assistant, MQTT, Zigbee2MQTT, or cloud service is required for normal monitoring.

## Component responsibilities

### Waveshare ESP32-S3

Owns:

- LVGL UI
- plant names and sensor assignments
- user preferences
- sensor freshness/current-boot presentation
- Wi-Fi
- local setup/update UX
- release/update orchestration
- persistent user configuration

### M5Stack ESP32-H2

Owns:

- Zigbee coordinator/network
- permit join
- ZG-303Z device handling
- Zigbee/Tuya normalization
- Zigbee persistence
- IEEE-64 device identity
- PlantLink reports to the Waveshare

### ZG-303Z

The sensor remains a stock Zigbee endpoint. It reports plant/environment values to the H2; it does not require household Wi-Fi.

## PlantLink boundary

PlantLink is the framed UART boundary between H2 and Waveshare.

The authoritative protocol definition is:

```text
shared/plantlink/plantlink.h
```

Current messages include link/heartbeat, Zigbee network state, permit join, device/infrastructure reports, normalized sensor reports, raw Zigbee diagnostics, command results, and H2 OTA messages.

## Sensor identity and freshness

The permanent identity is the Zigbee IEEE-64 address.

A sensor's Zigbee 16-bit short address is transient and must not be used as the persistent plant key.

The Waveshare may restore known sensor identity and user names at boot, but readings are not current until that sensor reports during the current boot. This prevents stale pre-reboot values from entering `WHO NEEDS WATER?` and similar summaries.

## Persistence

Normal firmware updates must preserve user data and Zigbee network state.

Waveshare persistent data includes plant names, device name, sensor assignments, Wi-Fi credentials, and user settings.

H2 persistent data includes the Zigbee network/coordinator state required to avoid unnecessary re-pairing.

Transient readings/history should not be persisted unless intentionally designed.

## Wi-Fi

Wi-Fi exists on the Waveshare for setup and updates.

Setup behavior is designed to support SSID scanning/selection and QR-assisted phone setup, while keeping known-good credentials until a replacement connection succeeds.

Disconnect and Forget Wi-Fi are separate actions: disconnect suppresses reconnect without erasing credentials; Forget Wi-Fi deliberately erases them.

## OTA

### Waveshare

The Waveshare updater uses an ESP PLANTS `.plantsota` package and manifest with:

- product/hardware/build identity,
- package and firmware SHA-256,
- ESP32-S3 image validation,
- certificate-verified HTTPS,
- bounded downloads/redirects,
- A/B app partitions,
- inactive-slot writes,
- final boot-partition activation after successful validation.

### H2 through Waveshare

Current source contains the Phase 2 path that transfers an H2 distribution image over PlantLink. The release flow builds H2 first and records its metadata/hash in the Waveshare manifest.

This path is **implemented in source but not claimed as physically verified**.

## Waveshare 7B

The 7B is a separate PlatformIO hardware target that shares the ESP PLANTS application where practical.

The original Waveshare 7-inch hardware remains the physically verified runtime baseline. Do not infer 7B runtime verification from successful source integration alone.

## Legacy architecture

`firmware/t5-hub` and `firmware/xiao-soil-sensor` preserve the older T5/XIAO Wi-Fi/UDP/ESP-NOW architecture. That history remains useful, but it is not the active product architecture.
