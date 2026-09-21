# Phase 1 - Waveshare Wi-Fi and Radar-grade OTA

Base: `waveshare-zigbee` @ `4c43aae8bb780e4a8e8c57fad2e619f5f97dc9d5`  
Firmware version: `0.2.0-alpha.1`

## Architecture

The Waveshare remains the only user-facing network/update endpoint:

```text
Phone / home Wi-Fi / GitHub
            |
            v
Waveshare ESP32-S3
  - UI
  - plant/user data
  - Wi-Fi setup
  - release checks
  - self OTA
            |
            | PlantLink UART
            v
M5Stack ESP32-H2
  - Zigbee coordinator
  - Zigbee network state
```

H2 update transport is intentionally not part of Phase 1.

## Persistence boundary

The 16 MB Waveshare layout is unchanged:

```text
nvs        generic/system NVS + retained legacy copy
otadata    OTA selector metadata
app0       6 MB application slot
app1       6 MB application slot
plantdata  ESP PLANTS user/config + Wi-Fi/update settings
history    persistent history area
coredump
```

On first Phase 1 boot, the old `nvs/espplants` records are copied into
`plantdata/espplants` and the old copy is retained. Normal OTA never erases
`plantdata` or `history`.

## Wi-Fi setup

1. Open `SETTINGS -> NETWORK & UPDATES`.
2. Tap `SET UP / CHANGE WI-FI`.
3. Join the temporary password-protected `ESP-PLANTS-xxxx` hotspot.
4. Use the captive page (or `192.168.4.1`) and select a scanned SSID.
5. Enter the password.
6. ESP PLANTS attempts that network while the old saved credentials are still
   intact.
7. Only a successful connection commits the new SSID/password to `plantdata`.

## Secure metadata checks

GitHub metadata and manifest requests use `esp_http_client` with Espressif's
certificate bundle and common-name verification enabled. There is no
`setInsecure()` path.

The device waits for sane NTP time before making certificate-verified GitHub
requests. Automatic checks occur no more than roughly once every 24 hours.

## Release manifest

The fixed manifest asset is:

```text
esp-plants-waveshare.manifest.json
```

Example schema:

```json
{
  "schema": 1,
  "tag": "v0.2.0-alpha.1",
  "product": "esp-plants-waveshare",
  "hardware": "waveshare-esp32-s3-touch-lcd-7",
  "channel": "alpha",
  "version": "0.2.0-alpha.1",
  "build_id": "ESPPLANTS-WAVESHARE-0.2.0-alpha.1",
  "asset": "esp-plants-waveshare-0.2.0-alpha.1.plantsota",
  "package_size": 1234567,
  "package_sha256": "...64 lowercase hex...",
  "firmware_size": 1234055,
  "firmware_sha256": "...64 lowercase hex...",
  "min_updater": 1,
  "notes": "..."
}
```

The GitHub release must contain the fixed manifest and the exact versioned
package named by the manifest. The GitHub asset size must also agree with the
manifest before the INSTALL button becomes usable.

## `.plantsota` package

The package is a 512-byte fixed header followed by the unmodified ESP32-S3
application image.

Header fields include:

```text
magic            ESP-PLANTS-OTA
format           1
hardware         WAVESHARE-ESP32-S3-LCD-7
product          ESP-PLANTS-WAVESHARE
build ID         exact manifest build ID
firmware size    exact bytes
firmware SHA256  exact digest
```

The public build contains both the declared build ID and the marker:

```text
ESP-PLANTS-DISTRIBUTION-BUILD
```

The packager refuses a private/developer firmware image without that marker.

## Install sequence

```text
validate manifest
      ↓
construct bounded GitHub release URL
      ↓
certificate-verified HTTPS
      ↓
validate redirect host + HTTP framing
      ↓
stream package SHA-256
      ↓
validate 512-byte package header
      ↓
open inactive OTA partition
      ↓
validate ESP32-S3 application header
      ↓
stream firmware SHA-256 + flash writes
      ↓
confirm embedded build ID + distribution marker
      ↓
confirm package SHA-256 + firmware SHA-256
      ↓
esp_ota_end()
      ↓
esp_ota_set_boot_partition(inactive slot)
      ↓
controlled Waveshare restart handoff
```

Any failure before final boot-partition selection leaves the currently running
partition selected.

## Public build

`platformio.ini` has two environments:

```text
waveshare_s3_touch_lcd_7          developer/USB build
waveshare_s3_touch_lcd_7_release  public OTA producer
```

Only the second defines `ESP_PLANTS_DISTRIBUTION_BUILD=1` and automatically
runs `scripts/build_plants_ota.py` after a successful build.

## Physical verification checklist

Before publishing this as hardware-verified:

1. USB flash the developer Phase 1 build.
2. Confirm existing device/plant/repeater names and F/C survived migration.
3. Reboot and confirm the registry loads from `plantdata`.
4. Configure Wi-Fi from a phone and verify wrong credentials do not replace the
   previous working credentials.
5. Confirm time synchronizes and `CHECK NOW` uses secure GitHub HTTPS.
6. Build `waveshare_s3_touch_lcd_7_release` at a newer version.
7. Publish the matching `.plantsota` + manifest as a GitHub release.
8. Confirm the display offers the update but does not auto-install it.
9. Install and confirm the new app boots with user data and H2 Zigbee pairings
   unchanged.
10. Publish a deliberately damaged package/manifest in a private test release
    and verify it is rejected without changing the running boot partition.
11. Interrupt a test download and verify the previous app still boots.
12. Verify the controlled restart on the physical Waveshare after a successful
    OTA, because that behavior is hardware-sensitive.

## Phone setup result and QR behavior

When `SET UP / CHANGE WI-FI` is started, the display keeps the temporary setup AP active and shows a `SCAN TO CONNECT` QR code. The QR is a standard WPA Wi-Fi QR containing the generated `ESP-PLANTS-xxxx` SSID and setup password. This follows the QR renderer already proven in ESP Aircraft Radar (`qrcodegen` rendered into an LVGL 1-bit canvas).

The phone setup form scans nearby SSIDs. Submitting credentials enters a bounded test state. `/status` refreshes every two seconds while the connection is being tested. A successful connection is persisted only after the station actually associates, then the phone gets a success result before the temporary AP closes. `WL_CONNECT_FAILED`, `WL_NO_SSID_AVAIL`, or a 20-second timeout produce an explicit failure page and return to the form with the attempted SSID retained. The password is not echoed back. Previous known-good credentials are left untouched and are retried after a failed test.

## Windows release helper

`tools\make-waveshare-release.cmd` may be launched from Explorer or a terminal. It locates the normal PlatformIO Windows virtual environment first and falls back to `pio`/`platformio` on PATH. It pauses on every exit path so build errors remain visible.
