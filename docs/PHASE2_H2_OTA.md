# Phase 2 — H2 firmware over PlantLink

Base: `waveshare-zigbee` commit `7896a8de1b65a12021ba55bbe99b9586545c16e7` (Waveshare alpha.10).

Alpha.11 adds an H2 OTA receiver using the existing H2 A/B app partitions. Begin carries protocol, image size, SHA-256 and expected build ID. Chunks carry a 32-bit offset plus at most 248 firmware bytes. The H2 validates the incoming ESP image against the running H2 chip ID, streams SHA-256, requires the expected build ID and distribution marker, calls `esp_ota_end`, and changes the boot partition only after all checks pass.

`Update All` ordering is H2 first, Waveshare second. If the H2 does not answer the Phase 2 begin command, the display update is stopped and the unit requires the one-time H2 USB bootstrap. Once alpha.11 H2 is installed, subsequent release H2 binaries are transferred over PlantLink.

The H2 release image is a controlled GitHub release asset with a SHA-256 sidecar. The Waveshare downloads both over verified HTTPS, verifies the binary SHA-256, transfers it stop-and-wait over PlantLink, waits for the H2 to reboot and report the expected build, and only then enters the existing Waveshare A/B installer.

No Zigbee/NVS/user-data partition is erased. No sensor registry or UI geometry is changed by this package.

## First Phase 2 installation

1. Build the alpha.11 H2 release environment and USB-flash the H2 once.
2. Build/upload all four release assets produced by `tools/make-waveshare-release.cmd`.
3. Update the Waveshare to alpha.11 using the existing updater path.
4. From then on, normal release installation updates H2 first over PlantLink and the Waveshare second.

Hardware verification is still required on the actual Waveshare + M5Stack H2 pair.
