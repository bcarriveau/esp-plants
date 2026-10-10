# Waveshare ESP32-S3 hub firmware

Waveshare ESP32-S3-Touch-LCD-7 (800x480) and -7B (1024x600) application
controllers for the direct-Zigbee ESP PLANTS branch.

## Hardware/runtime ownership

- Waveshare: board-specific LVGL UI, plant/user configuration, history, Wi-Fi and updates.
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

The 7B build has its own targets:

```text
pio run -d firmware/waveshare-hub -e waveshare_s3_touch_lcd_7b
pio run -d firmware/waveshare-hub -e waveshare_s3_touch_lcd_7b_release
```

Only release environments embed the public-distribution provenance marker
and generate `.plantsota` + manifest release assets. The original 7
retains `upload_port = COM11`; the 7B has its own COM13 setting.

The 7B uses 24 MHz RGB timing, 15-line bounce buffers, and an IDF
VSYNC-restart overlay verified at build/link time by
`scripts/use_7b_vsync_sdk.py`. Runtime uses three PSRAM RGB framebuffers
(3,686,400 bytes total) in LVGL full-refresh mode, even though the board
header has a fallback macro of one framebuffer. The original 7 does not
use the custom 7B SDK overlay. Do not vary this tested configuration as
an undocumented memory optimization.

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

H2-through-Waveshare OTA is implemented and has been physically verified
end-to-end on the original Waveshare 7 development hardware. The H2 and
Waveshare have independent versions, so a release can update only the
controller needing it. This verification must not be extrapolated to a
complete 7B production OTA qualification.

## 7B physical testing and diagnostics

Bill reported the former 7B vertical roll/bounce absent during stress
testing that included sensor reports, rapid page changes, Wi-Fi activity,
OTA preflight, and explicit RGB resync. This is development hardware
evidence rather than exhaustive long-duration or release verification.

The 7B display diagnostics log `flush`, `present`, `ptr_change`, `commit`,
and `bounce_frame_finish` rates. A normal frame-finish rate (approximately
26/s at 24 MHz with current panel porch timings) alone cannot prove that
the RGB scan is vertically aligned. For that reason, these counters remain
diagnostic only: the firmware does **not** automatically trigger resync
merely because a counter or its jitter changes. Its existing NVS/OTA-idle
recovery and display timings remain unchanged.
