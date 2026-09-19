# ESP PLANTS

ESP PLANTS is a standalone, local-first plant monitoring project.

Current hardware lines in this repository:

- **Waveshare ESP32-S3 7-inch hub + M5Stack ESP32-H2 Zigbee gateway**
  - current development work lives on the `waveshare-zigbee` branch
  - intended for direct communication with HOBEIAN ZG-303Z Zigbee plant sensors
  - no Home Assistant, MQTT, Zigbee2MQTT, or cloud service is required for normal operation
- **LILYGO T5 4.7-inch S3 Pro hub + Seeed XIAO ESP32-C6 soil sensors**
  - existing hardware-proven baseline retained in the same repository

## VS Code workspace

Open the repository root once and leave it open:

```text
D:\esp plants
```

Do **not** open each firmware subfolder as a separate VS Code workspace.

The firmware projects intentionally remain separate PlatformIO projects because
they use different boards, frameworks/toolchain generations, and package sets.
The root `.vscode/tasks.json` is the single workspace control layer.

### Build / Clean / Upload / Monitor

In VS Code use:

```text
Terminal -> Run Task...
```

Available root tasks include Build, Clean, Upload, and Monitor for:

- Waveshare
- M5Stack H2
- LILYGO T5
- XIAO soil sensor

There are also:

- `ESP PLANTS: Build Waveshare + H2`
- `ESP PLANTS: Clean Waveshare + H2`
- `ESP PLANTS: Build ALL`
- `ESP PLANTS: Clean ALL`

`Ctrl+Shift+B` defaults to **Build Waveshare + H2** on the current branch.

On Windows the tasks call PlatformIO from its normal VS Code installation path:

```text
%USERPROFILE%\.platformio\penv\Scripts\platformio.exe
```

That is only the PlatformIO executable/toolchain location. The repository and
all project paths remain under `D:\esp plants`.

## Repository layout

```text
esp plants/
├── .vscode/
│   └── tasks.json
├── firmware/
│   ├── waveshare-hub/
│   │   └── platformio.ini
│   ├── m5-h2-zigbee/
│   │   └── platformio.ini
│   ├── t5-hub/
│   │   └── platformio.ini
│   └── xiao-soil-sensor/
│       └── platformio.ini
├── shared/
├── tests/
├── tools/
├── docs/
├── AGENTS.md
├── CHANGELOG.md
└── VERSION
```

## Command-line builds

From the repository root:

```powershell
.\tools\build_all.ps1
```

Or build an individual project directly:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\waveshare-hub"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\m5-h2-zigbee"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\t5-hub"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d ".\firmware\xiao-soil-sensor"
```

## Architecture rules

- The repository root is the one permanent VS Code workspace.
- The individual firmware projects stay isolated under `firmware/`.
- The Waveshare S3 owns the UI, plant names, user settings, history, Wi-Fi, and update experience.
- The ESP32-H2 owns Zigbee networking, pairing, device handling, and ZG-303Z translation.
- Zigbee sensors are permanently identified by their 64-bit IEEE address, not a transient 16-bit network address.
- User configuration and Zigbee network state must survive normal firmware updates.
- Do not guess pin mappings or connector wiring; hardware changes require source/documentation verification and physical validation.

See `AGENTS.md` for repository working rules.

## Documentation

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

## Source of truth

GitHub repository:

```text
bcarriveau/esp-plants
```

Use the intended development branch when reviewing or changing current work.
