# Waveshare/Zigbee next-stage package status

Prepared for branch: `waveshare-zigbee`

## Included

- Waveshare 7-inch PlatformIO project using the same display/touch generation as ESP Aircraft Radar.
- Native ESP32-S3 USB-C for firmware upload and `Serial` diagnostics.
- UART2 GPIO43/44 reserved for PlantLink to the M5Stack H2.
- M5Stack Unit Gateway H2 Zigbee coordinator project for its 2 MB ESP32-H2-MINI-1-N2.
- M5Stack 2 MB Zigbee coordinator partition map.
- PlantLink v1 COBS + CRC-32 framing.
- ZG-303Z parser supporting known legacy/newer Tuya datapoint families plus standard Zigbee temperature/humidity/battery reports.
- Unknown Tuya datapoint logging for first-hardware capture.
- Root VS Code tasks so no new VS Code workspace is required.
- Local host-side tests for shared protocol/parser code.
- Repository rule explicitly forbidding GitHub Actions firmware-build workflows unless the owner asks for them.

## Tested here

- Shared C++ PlantLink framing/CRC/COBS tests: PASS.
- ZG-303Z known/legacy Tuya parser tests: PASS.
- Standard Zigbee report parser tests: PASS.
- H2 2 MB partition layout: no overlaps; exact 2 MB bound: PASS.
- Waveshare 16 MB partition layout: no overlaps; exact 16 MB bound: PASS.

## Not claimed yet

- Full PlatformIO embedded builds were not run in this environment because its PlatformIO toolchain is unavailable.
- Physical Waveshare/H2 UART wiring has not been tested on the arriving units.
- Actual ZG-303Z traffic still needs one real pairing capture before the datapoint table is considered hardware-verified.

## Existing repo file to delete

Delete `.github/workflows/build.yml` when applying this package. ZIP extraction cannot represent deletion of an existing file. After deletion, GitHub will no longer run that firmware-build workflow on future pushes.
