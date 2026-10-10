# ESP PLANTS

ESP PLANTS is a standalone, local-first plant monitoring system. The active product line is the **Waveshare ESP32-S3 7-inch display + M5Stack ESP32-H2 Thread/Zigbee Gateway Unit + HOBEIAN ZG-303Z Zigbee plant sensors** on the `waveshare-zigbee` branch.

Normal operation is direct and local. Home Assistant, MQTT, Zigbee2MQTT, and cloud services are not required.

## Active product line

```text
HOBEIAN ZG-303Z
      |
    Zigbee
      |
M5Stack ESP32-H2
Zigbee coordinator / translator
      |
 PlantLink UART
      |
Waveshare ESP32-S3
7: 800x480 / 7B: 1024x600
ESP PLANTS UI
```

### Waveshare display hub

The Waveshare owns the user-facing application:

- LVGL UI: 800x480 on the original 7 and 1024x600 on the 7B
- plant names and Zigbee sensor assignments
- device/title name
- temperature unit and personality/theme settings
- current-boot sensor freshness state
- Wi-Fi configuration
- update experience
- persistent user configuration

The original Waveshare 7-inch target is the physically verified ESP PLANTS display baseline.

The Waveshare 7B has its own 1024x600 PlatformIO target and board-specific display driver. Bill has physically tested its ESP PLANTS runtime, including sensor bursts, page changes, Wi-Fi activity, OTA preflight, and RGB resync without seeing the former vertical roll/bounce. These are development-hardware observations, **not** a complete production-release qualification.

### M5Stack ESP32-H2

The H2 owns:

- Zigbee coordinator/network state
- permit-join and commissioning
- ZG-303Z Zigbee/Tuya translation
- stable IEEE-64 sensor identity
- Zigbee-side persistence
- normalized PlantLink messages to the Waveshare

The Waveshare and H2 are separate firmware projects and are independently versioned.

### HOBEIAN ZG-303Z sensors

The current product line uses stock ZG-303Z Zigbee plant sensors. The verified Waveshare 7 path includes Zigbee commissioning and live soil moisture, temperature, air humidity, battery, and PlantLink delivery to the display.

After a Waveshare reboot, stored sensor identity may be visible immediately, but a sensor is not current until it reports during that boot. Watering summaries exclude stale pre-reboot readings.

## LVGL scalability and stability

The Waveshare keeps **32 logical plant-sensor slots and 32 logical infrastructure/repeater slots**, but scrolling UI capacity is no longer tied to those maxima. The current UI uses fixed reusable LVGL row pools:

- HOME: 7 physical rows for up to 32 logical sensors
- ALL SENSORS: 7 physical rows for up to 32 logical sensors
- ADVANCED ZIGBEE: 5 physical rows for up to 32 logical infrastructure nodes

The row pools are created once and rebound while scrolling; off-screen sensors and repeaters remain full data-model participants. Current-boot freshness, watering summaries, sorting, rename/remove behavior, and Plant detail selection do not depend on whether a row is currently visible.

The alpha.44-alpha.50 stability work also removed common software shadows, avoids unchanged label writes, limits hidden-page mutation, uses page/modal-specific dirty state, and adds LVGL allocator/display/runtime telemetry. The LVGL heap remains 128 KB and the proven RGB framebuffer/full-refresh policy has not been changed without measurement. Long-duration alpha.50 soak testing remains an active validation item.

## Wi-Fi and updates

Wi-Fi belongs to the Waveshare. The current source includes local Wi-Fi setup/change/disconnect/forget behavior and the ESP PLANTS updater.

The normal Waveshare update architecture uses:

- ESP PLANTS `.plantsota` packages
- A/B application partitions
- inactive-slot writes
- product/hardware/build identity checks
- package and firmware SHA-256 validation
- ESP32-S3 image validation
- certificate-verified HTTPS
- bounded download/redirect handling
- boot-partition change only after complete validation
- preserved user-data partitions

The source also contains the Phase 2 H2-through-Waveshare OTA path. Release tooling builds the H2 distribution image first, includes its metadata in the Waveshare release manifest, and the installer transfers H2 firmware over PlantLink before installing the Waveshare image.

**The normal Waveshare A/B OTA path and H2-through-Waveshare OTA path have both been physically verified end-to-end on the original Waveshare 7 development hardware.** Waveshare and H2 remain independently versioned, so an update may legitimately apply to one controller without reflashing the other.

## Repository layout

```text
esp-plants/
├── .vscode/
│   └── tasks.json
├── firmware/
│   ├── waveshare-hub/       # active display/application firmware
│   ├── m5-h2-zigbee/       # active Zigbee coordinator firmware
│   ├── t5-hub/              # legacy product line
│   └── xiao-soil-sensor/    # legacy product line
├── shared/
│   ├── plantlink/
│   └── zg303z/
├── tests/
├── tools/
├── docs/
├── release/
├── AGENTS.md
├── CHANGELOG.md
└── VERSION
```

## VS Code workspace

Open the repository root as the permanent workspace. Root `.vscode/tasks.json` contains Build, Clean, Upload, and Monitor tasks for the individual firmware projects and combined Waveshare + H2 builds.

`Ctrl+Shift+B` defaults to `ESP PLANTS: Build Waveshare + H2`.

The current Waveshare 7 developer environment intentionally keeps the established local upload target in `firmware/waveshare-hub/platformio.ini`:

```text
upload_port = COM11
```

That is a developer-machine setting, not a product requirement.

## Command-line builds

There is no `tools/build_all.ps1` in the current repository. Build from the root with PlatformIO directly, or use the root VS Code tasks.

Windows examples:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"

& $pio run -d ".\firmware\waveshare-hub"
& $pio run -d ".\firmware\waveshare-hub" -e waveshare_s3_touch_lcd_7b
& $pio run -d ".\firmware\m5-h2-zigbee"
```

Legacy builds remain available when needed:

```powershell
& $pio run -d ".\firmware\t5-hub"
& $pio run -d ".\firmware\xiao-soil-sensor"
```

Release packaging for the active Waveshare/H2 product line is performed with:

```text
tools\make-waveshare-release.cmd
```

That command builds the H2 release image and then the Waveshare release package/manifest.

## Tests

Run the repository Python tests from the root with:

```powershell
python -m pytest tests
```

Host-side C++ protocol coverage is retained in `tests/host/test_shared.cpp`; use the local compiler/tooling available on the development machine when exercising it.

## Legacy product line

`firmware/t5-hub` and `firmware/xiao-soil-sensor` preserve the earlier LILYGO T5 / XIAO Wi-Fi/UDP product line and its development history.

They are **legacy**, not the current Waveshare product architecture. Do not carry T5/XIAO transport, UI, provisioning, or ownership assumptions into Waveshare/H2 work unless explicitly comparing the platforms.

Historical baseline documents may still describe that legacy implementation. They should be labeled as historical rather than treated as current-product instructions.

## Verification status

### Physically verified on the original Waveshare 7-inch development hardware

- H2 native Zigbee coordinator startup
- persistent Zigbee network behavior
- ZG-303Z commissioning
- IEEE-64 identity
- current ZG-303Z decoding used by the project
- soil moisture, temperature, air humidity, and battery reporting
- H2-to-Waveshare PlantLink UART
- live sensor-data delivery to the Waveshare UI
- Waveshare A/B OTA installation end-to-end
- H2-through-Waveshare OTA transfer/install before the Waveshare update

### Physically exercised on the Waveshare 7B development hardware

- ESP PLANTS runtime and touch/page navigation during stress testing
- sensor-report bursts, Wi-Fi activity, OTA preflight, and explicit RGB resync
- no visible roll/bounce during Bill's reported stress-test sessions

### Not claimed as physically verified

- complete production-release lifecycle across every target
- exhaustive 7B long-duration and all-update-path qualification

## Source of truth

Repository: `bcarriveau/esp-plants`  
Active development branch: `waveshare-zigbee`

For detailed architecture and working rules, see `AGENTS.md`, `docs/WAVESHARE_ZIGBEE_ARCHITECTURE.md`, `docs/PLANTLINK_PROTOCOL.md`, `docs/TESTING.md`, and `docs/RELEASE_CHECKLIST.md`.
