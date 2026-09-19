# Waveshare ESP32-S3 hub firmware

This directory is reserved for the Waveshare ESP32-S3 7-inch ESP PLANTS hub.

## Responsibility

The Waveshare side owns:

- 7-inch touch UI and display behavior
- plant slots, names, thresholds, and user-facing configuration
- persistent mapping from Zigbee sensor IEEE address to plant slot
- local history and stale/last-seen presentation
- Wi-Fi, time, web/setup functions, and GitHub-oriented update workflow
- PlantLink host communication with the M5Stack ESP32-H2

It does **not** own the Zigbee stack or HOBEIAN/Tuya packet decoding.

## Development status

Scaffold only. No board pinout, display bus mapping, PlatformIO environment, or
hardware behavior is claimed here yet.

Before adding hardware definitions, verify them against the current Waveshare
board documentation and then mark physical verification separately.
