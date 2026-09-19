# M5Stack ESP32-H2 Zigbee firmware

This directory is reserved for the M5Stack ESP32-H2 Thread/Zigbee Gateway Unit
used by ESP PLANTS.

## Responsibility

The H2 side owns:

- Zigbee coordinator/network creation and restore
- permit-join, pairing, removal, and device lifecycle
- stable identification by 64-bit IEEE address
- HOBEIAN ZG-303Z Zigbee/Tuya decoding and supported sensor commands
- persistent Zigbee network state
- PlantLink device communication to the Waveshare ESP32-S3
- human-readable USB debug logging, kept separate from the PlantLink UART

The H2 must not own plant names or user-facing plant configuration.

## Development status

Scaffold only. The exact ESP-IDF/PlatformIO environment and connector wiring
will be added only after current M5Stack/Waveshare documentation is checked.
No build or hardware verification is claimed yet.
