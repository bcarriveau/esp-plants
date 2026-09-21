# Waveshare ESP32-S3 hub firmware

Waveshare ESP32-S3-Touch-LCD-7 application controller for the direct-Zigbee
ESP PLANTS branch.

## Hardware/runtime ownership

- Waveshare: 800x480 UI, plant/user configuration, history, Wi-Fi and updates.
- M5Stack ESP32-H2: Zigbee coordinator/network, ZG-303Z decoding and Zigbee
  persistence over PlantLink UART.
- Home Assistant is not part of this build.

## Build environments

Developer/USB build:

```text
pio run -d firmware/waveshare-hub -e waveshare_s3_touch_lcd_7
```

Public OTA producer:

```text
pio run -d firmware/waveshare-hub -e waveshare_s3_touch_lcd_7_release
```

Only the release environment embeds the public-distribution provenance marker
and generates `.plantsota` + manifest release assets.

## Persistence

The 16 MB flash layout keeps two 6 MB application slots separate from the
`plantdata` NVS partition and history partition. Phase 1 migrates pre-existing
ESP PLANTS user records from default NVS into `plantdata` without erasing the
legacy copy.

## Phase 1 update safety

The GitHub updater follows the Aircraft Radar Product 102 safety model:
certificate-verified HTTPS, fixed package identity, hardware/product/build
validation, package + firmware SHA-256, ESP32-S3 image validation, inactive OTA
slot writes, final activation only after `esp_ota_end()`, and the proven
Waveshare controlled restart handoff.

H2 firmware updating is deliberately deferred to Phase 2.
