# ESP PLANTS Phase 2 replacement package

Base commit: `7896a8de1b65a12021ba55bbe99b9586545c16e7`
Target version: `0.2.0-alpha.11`

Copy this ZIP over the repository root on `waveshare-zigbee`.

Important: the current H2 firmware has no PlantLink OTA receiver. Flash the alpha.11 H2 release environment over USB once. After that bootstrap, release installation transfers H2 firmware over PlantLink first and only proceeds to the existing Waveshare A/B installer after the H2 validates, reboots and reports the target build.

This package does not alter the alpha.10 display/LVGL layout code. The small PlatformIO pre-build integration script inserts the H2-first call into the existing alpha.10 installer without reorganizing that proven installer/restart implementation.

Validation performed in the packaging environment: Python syntax checks, five Phase 2 source-guard tests, synthetic release-package/manifest generation, branch HEAD recheck. PlatformIO is not installed in this execution environment, so ESP32-S3/H2 firmware compilation and physical hardware testing remain to be done locally/on hardware.
