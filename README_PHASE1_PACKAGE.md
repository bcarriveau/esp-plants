# ESP PLANTS Phase 1 - Radar-grade Wi-Fi + Waveshare OTA

**Source branch:** `waveshare-zigbee`  
**Exact base commit:** `4c43aae8bb780e4a8e8c57fad2e619f5f97dc9d5`  
**Base Waveshare `main.cpp` blob:** `e764f1c0f5a5d1421673201432bf548ca1c7f480`  
**Phase 1 version:** `0.2.0-alpha.1`

This package **supersedes the earlier Phase 1 Wi-Fi/OTA ZIP**. Do not use the earlier ZIP.
The OTA engine in this package is deliberately modeled after the hardened
Aircraft Radar Product 102 release/installer boundary rather than the earlier
raw-`firmware.bin` prototype.

## Hardware-test fixes in this revision

- Wi-Fi setup now has a real result flow instead of a dead-end testing page. The phone page refreshes while ESP PLANTS tests the credentials, then clearly reports **CONNECTED AND SAVED** or **CONNECTION FAILED**. A failure returns to the SSID/password form with the failed network still selected and the password field blank for a clean retry.
- Failed Wi-Fi tests do not replace the previous known-good credentials. If a previous network exists, ESP PLANTS reconnects to it while leaving the setup hotspot available for another attempt.
- The Waveshare Network & Updates screen now uses the same QR rendering approach proven in ESP Aircraft Radar: LVGL canvas + LVGL's bundled `qrcodegen`. During phone setup it displays a **SCAN TO CONNECT** QR containing the temporary ESP PLANTS hotspot credentials. The captive setup page should then open automatically; `192.168.4.1` remains the manual fallback.
- `tools\make-waveshare-release.cmd` is now safe to double-click. It first looks for PlatformIO in `%USERPROFILE%\.platformio\penv\Scripts\platformio.exe`, then PATH, prints the real build error, and **pauses on success or failure** instead of flashing closed.

## What to overlay

Copy the contents of this ZIP onto the root of the ESP PLANTS workspace after
running `VERIFY_BASE.cmd`. The package contains complete replacement/new files,
not a patch script.

## What Phase 1 does

- migrates existing device name, F/C preference, plant registry/names and
  repeater registry/names from default NVS to the dedicated `plantdata` NVS
  partition while leaving the legacy copy untouched;
- stores Wi-Fi/update metadata in a separate `espnet` namespace inside
  `plantdata`;
- adds `SETTINGS -> NETWORK & UPDATES`;
- starts phone setup only when explicitly requested on the physical display;
- scans nearby SSIDs and tests a new Wi-Fi password before replacing the
  previously working credentials;
- checks GitHub Releases manually or approximately once per day;
- requires valid internet time before certificate-verified GitHub HTTPS;
- installs only a versioned `.plantsota` package into the inactive OTA app slot;
- checks hardware ID, product ID, build ID, package length, package SHA-256,
  firmware length, firmware SHA-256, ESP32-S3 image header and the embedded
  public-distribution marker;
- calls `esp_ota_set_boot_partition()` only after the complete image passes
  `esp_ota_end()` and all package checks;
- uses the same style of controlled Core-0 restart handoff proven on the
  Waveshare Aircraft Radar.

**Phase 1 does not flash the H2.** H2 firmware-over-PlantLink remains Phase 2.

## First local build / USB install

The normal developer environment is still:

```bat
pio run -d firmware\waveshare-hub -e waveshare_s3_touch_lcd_7
```

Flash that build by USB for the first Phase 1 install. It can receive a future
public OTA, but it **cannot generate a public OTA package** because it lacks the
distribution provenance marker.

## Build a public GitHub OTA release

Use:

```bat
tools\make-waveshare-release.cmd
```

That builds the separate environment:

```text
waveshare_s3_touch_lcd_7_release
```

The post-build packager refuses firmware that does not contain both:

```text
ESP-PLANTS-DISTRIBUTION-BUILD
ESPPLANTS-WAVESHARE-<version>
```

Successful output under `release/` is:

```text
esp-plants-waveshare-0.2.0-alpha.1.plantsota
esp-plants-waveshare.manifest.json
```

Create GitHub Release tag `v0.2.0-alpha.1` and upload **both** files. Draft
releases are ignored. This alpha build accepts a matching alpha-channel release.

## Important distinction

A normal PlatformIO USB upload or normal OTA is **not** a factory erase. It
updates application flash and preserves `plantdata`, `history`, and the H2
Zigbee network partitions. A future destructive/factory install must remain a
separate, explicitly labeled recovery/migration operation.

## Verification performed in this package

The included Python tests exercise the `.plantsota` producer and source safety
guards. At packaging time they pass 14/14 tests, including wrong-chip rejection,
private-build rejection, missing-build-ID rejection, tamper rejection,
certificate-bundle HTTPS guards, inactive OTA slot use and boot-partition ordering.

Run them locally with:

```bat
python -m unittest discover -s tests -v
```

A full embedded PlatformIO compile and the physical OTA/reboot test still need
to be performed on Bill's local toolchain/hardware before calling Phase 1
hardware-verified.
