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
800x480 ESP PLANTS UI
```

### Waveshare display hub

The Waveshare owns the user-facing application:

- 800x480 LVGL UI
- plant names and Zigbee sensor assignments
- device/title name
- temperature unit and personality/theme settings
- current-boot sensor freshness state
- Wi-Fi configuration
- update experience
- persistent user configuration

The original Waveshare 7-inch target is the physically verified ESP PLANTS display baseline.

The Waveshare 7B has its own PlatformIO target and board-specific boundary. It is **not** claimed as physically verified ESP PLANTS runtime hardware yet.

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

**H2-through-Waveshare OTA is implemented in source but is not claimed here as physically verified on hardware.**

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

### Not claimed as physically verified

- Waveshare 7B ESP PLANTS runtime
- final 7B board-specific behavior
- H2-through-Waveshare OTA
- complete production release lifecycle across every target

## Source of truth

Repository: `bcarriveau/esp-plants`  
Active development branch: `waveshare-zigbee`

For detailed architecture and working rules, see `AGENTS.md`, `docs/WAVESHARE_ZIGBEE_ARCHITECTURE.md`, `docs/PLANTLINK_PROTOCOL.md`, `docs/TESTING.md`, and `docs/RELEASE_CHECKLIST.md`.
