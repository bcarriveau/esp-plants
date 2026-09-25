# ESP PLANTS

ESP PLANTS is a standalone, local-first plant monitoring system built around an ESP32-S3 touch display, a dedicated ESP32-H2 Zigbee gateway, and battery-powered Zigbee plant sensors.

The active development line is the **Waveshare 7-inch / 7B + M5Stack ESP32-H2 + HOBEIAN ZG-303Z** platform on the `waveshare-zigbee` branch. Normal operation is direct and local: no Home Assistant, MQTT, Zigbee2MQTT, or cloud service is required.

## Current platform

### Display hub

ESP PLANTS supports two 800x480 Waveshare ESP32-S3 display targets from the same application project:

- **Waveshare ESP32-S3 Touch LCD 7**
  - current hardware-proven ESP PLANTS target
  - 800x480 LVGL touch UI
- **Waveshare ESP32-S3 Touch LCD 7B**
  - separate PlatformIO target now scaffolded
  - shares the ESP PLANTS application/UI architecture where the hardware allows
  - keeps 7B-specific hardware differences behind the dedicated build target
  - not yet claimed as physically verified ESP PLANTS hardware

The goal is one ESP PLANTS product/application rather than two diverging UIs. Hardware-specific behavior belongs behind the appropriate board target.

### Zigbee gateway

The **M5Stack ESP32-H2 Thread/Zigbee Gateway Unit** owns the Zigbee side of the system:

- Zigbee coordinator/network state
- sensor commissioning and permit-join
- ZG-303Z device handling and Tuya translation
- stable IEEE-64 sensor identity
- normalized PlantLink messages to the Waveshare

The H2 and Waveshare communicate directly over PlantLink UART.

### Plant sensors

The current Waveshare system uses **HOBEIAN ZG-303Z Zigbee plant sensors**. The hardware-tested path is:

```text
ZG-303Z
   |
 Zigbee
   |
M5Stack ESP32-H2
   |
PlantLink UART
   |
Waveshare ESP32-S3
   |
800x480 ESP PLANTS UI
```

The complete ZG-303Z -> H2 -> PlantLink -> Waveshare live-data path has been verified on the original Waveshare 7-inch hardware.

## What the Waveshare owns

The Waveshare is the user-facing controller and owns:

- plant names and sensor assignments
- device/title name
- temperature-unit preference
- UI/personality settings
- current-boot report state
- Wi-Fi configuration
- update experience
- persistent user configuration

Known sensors remain identified by their Zigbee IEEE-64 address rather than their transient 16-bit Zigbee network address.

After a reboot, stored sensor identity may be shown immediately, but a sensor is not considered current until it actually reports during that boot. `WHO NEEDS WATER?` excludes stale pre-reboot readings.

## Current UI

The Waveshare application uses an 800x480 touch interface with the existing dashboard geometry preserved across supported Waveshare targets.

Current functionality includes:

- HOME plant dashboard
- individual plant detail
- ALL SENSORS view designed for a larger sensor registry
- `WHO NEEDS WATER?` based only on fresh current-boot moisture reports
- persistent plant renaming
- persistent ESP PLANTS device/title renaming
- Celsius/Fahrenheit display preference
- Zigbee/H2 status
- sensor registration and Add Sensor controls
- settings and diagnostics
- dark greenhouse-style UI

Sleepy sensors that have not yet reported after a reboot remain visibly waiting/not-yet-reported rather than displaying old readings as current.

## Wi-Fi and updates

The Waveshare side is the owner of Wi-Fi and the customer update experience.

The update architecture is designed around an ESP PLANTS-specific OTA package and A/B application partitions. Normal updates are expected to preserve user data including plant names, device name, sensor assignments, Zigbee/router records, Wi-Fi credentials, and user settings.

OTA work must retain product/hardware identity, version/build identity, SHA-256 verification, ESP32-S3 image validation, verified HTTPS, bounded downloads/redirects, inactive-slot writes, and boot-partition switching only after validation.

The ESP32-H2 update path through the Waveshare is a later phase; it is not presented here as completed functionality.

## VS Code workspace

Open the repository root once and leave it open:

```text
D:\esp plants
```

Do **not** open each firmware subfolder as a separate VS Code workspace.

The firmware projects remain separate PlatformIO projects because the hardware targets use different boards and toolchain/package requirements. Root `.vscode/tasks.json` is the workspace control layer.

Use:

```text
Terminal -> Run Task...
```

Current root tasks include Build, Clean, Upload, and Monitor operations for:

- Waveshare 7
- Waveshare 7B
- M5Stack H2
- LILYGO T5
- XIAO soil sensor

Combined tasks include:

- `ESP PLANTS: Build Waveshare + H2`
- `ESP PLANTS: Clean Waveshare + H2`
- `ESP PLANTS: Build Waveshare 7B + H2`
- `ESP PLANTS: Clean Waveshare 7B + H2`
- `ESP PLANTS: Build ALL`
- `ESP PLANTS: Clean ALL`

`Ctrl+Shift+B` currently defaults to **Build Waveshare + H2**.

On Windows, tasks use PlatformIO from:

```text
%USERPROFILE%\.platformio\penv\Scripts\platformio.exe
```

The repository itself remains under `D:\esp plants`.

### Upload and monitor targets

The Waveshare 7 and 7B have separate PlatformIO environments and VS Code tasks so each hardware target can be built and uploaded independently. Machine-specific serial-port assignments are intentionally kept out of the project documentation.

## PlatformIO targets

The Waveshare application is in:

```text
firmware/waveshare-hub
```

Important environments currently include:

```text
waveshare_s3_touch_lcd_7
waveshare_s3_touch_lcd_7_release
waveshare_s3_touch_lcd_7b
```

The 7B target is deliberately separate so board-specific differences do not destabilize the hardware-proven 7-inch target.

## Repository layout

```text
esp plants/
├── .vscode/
│   └── tasks.json
├── firmware/
│   ├── waveshare-hub/
│   ├── m5-h2-zigbee/
│   ├── t5-hub/
│   └── xiao-soil-sensor/
├── shared/
├── tests/
├── tools/
├── docs/
├── release/
├── AGENTS.md
├── CHANGELOG.md
└── VERSION
```

## Command-line builds

From the repository root:

```powershell
.\tools\build_all.ps1
```

Individual projects can also be built directly:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\waveshare-hub"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\waveshare-hub" -e waveshare_s3_touch_lcd_7b
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\m5-h2-zigbee"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\t5-hub"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\xiao-soil-sensor"
```

## Development rules

- `waveshare-zigbee` is the source-of-truth branch for current ESP PLANTS Waveshare development.
- Preserve working UI and behavior before adding features.
- Keep hardware-specific differences isolated rather than forking the whole application.
- Do not use build-time source injectors to implement application behavior.
- Preserve established local build/upload configuration when changing project files.
- Do not casually change persistent record layouts.
- User configuration must survive normal firmware updates.
- Do not guess connector pinouts or hardware behavior; verify them from source/documentation and then on physical hardware.
- Do not claim hardware verification until it has actually been physically tested.

See `AGENTS.md` for repository working rules.

## Legacy hardware line

The repository also retains the earlier **LILYGO T5 4.7-inch S3 Pro + Seeed XIAO ESP32-C6** implementation and its development history.

That platform remains useful as a hardware-proven legacy baseline, but it is separate from the current Waveshare/H2/ZG-303Z architecture. Do not carry T5/XIAO assumptions into current Waveshare work unless explicitly comparing the platforms.

## Documentation

Current project references include:

- `docs/WAVESHARE_ZIGBEE_ARCHITECTURE.md`
- `docs/HARDWARE_WAVESHARE_H2.md`
- `docs/PLANTLINK_PROTOCOL.md`
- `docs/ZG303Z_NOTES.md`
- `docs/ARCHITECTURE.md`
- `docs/HARDWARE.md`
- `docs/SETUP.md`
- `docs/TESTING.md`
- `docs/RELEASE_CHECKLIST.md`
- `CHANGELOG.md`

## Verification status

### Physically verified

On the original Waveshare 7-inch development hardware:

- H2 native Zigbee coordinator startup
- persistent Zigbee network behavior
- ZG-303Z commissioning
- IEEE-64 identity
- raw APS/Tuya handling used by the current implementation
- soil moisture, temperature, air humidity, and battery reporting
- H2-to-Waveshare PlantLink UART communication
- live sensor-data delivery to the Waveshare UI

### Not yet claimed as physically verified

- Waveshare 7B ESP PLANTS runtime
- final 7B board-specific behavior
- final production OTA lifecycle across every supported target
- H2 OTA through the Waveshare

## Source of truth

Repository: `bcarriveau/esp-plants`

Active development branch: `waveshare-zigbee`
