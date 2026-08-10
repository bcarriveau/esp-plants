# LILYGO T5 Plant Monitor

Standalone, local-first plant monitoring system built around:

- **LILYGO T5 4.7-inch S3 Pro e-paper hub**
- **Seeed Studio XIAO ESP32-C6 Soil Moisture Monitor sensors**
- local home Wi-Fi / router / mesh for normal sensor traffic
- ESP-NOW only for nearby provisioning
- no Home Assistant requirement
- no MQTT requirement
- no cloud service requirement
- plant names stored centrally on the T5 so a rename happens in one place

This repository is the clean GitHub starting baseline for the project.

**Repository baseline:** `0.1.0-alpha.1`  
**Date established:** `2026-08-09`  
**Protocol:** v3

> Status: development baseline. The T5 Phase 3B transport/setup work and earlier
> XIAO Wi-Fi/UDP path have been exercised on hardware. The newest XIAO Phase 3E
> calibration additions are included as the forward baseline but should be
> compiled and physically verified before calling this a stable release.

## What the system does

Each XIAO sensor is a self-contained battery soil node. It wakes, powers and
samples the probe, calculates moisture/battery state, and decides whether the
change is important enough to turn Wi-Fi on.

Normal data flow:

```text
XIAO sensor
    |
    | local soil/battery measurement
    v
adaptive decision
    |
    | only when a report is needed
    v
home Wi-Fi / router / mesh
    |
    | UDP protocol v3
    v
LILYGO T5 hub
    |
    +--> application ACK
    +--> e-paper display
    +--> local setup page
    +--> persistent plant names
```

Provisioning uses a different path:

```text
T5 local setup mode
    |
    | ESP-NOW channel 1
    v
nearby unprovisioned XIAO
    |
    +--> receives home Wi-Fi credentials
    +--> stores credentials in NVS
    +--> reboots into normal Wi-Fi/UDP operation
```

## Current feature baseline

### T5 hub

- 960×540 e-paper display support for the LILYGO T5 S3 Pro / H752-01 style board
- local `PlantMonitor-xxxx` setup hotspot
- generated setup password stored in NVS
- local setup page at `192.168.4.1` while in setup mode
- setup page also available from the T5 home-network IP in normal mode
- save home Wi-Fi SSID/password
- discover and provision nearby XIAO sensors
- up to 16 persisted plant records
- rename plants centrally by stable sensor ID
- plant names are **not** compiled into sensor firmware
- normal UDP receiver on port `42100`
- validates protocol/checksum and returns application ACK
- physical IO48-labeled function button short press enters/leaves setup mode
- physical IO48-labeled function button long hold draws the final OFF screen and
  requests PMU shutdown
- physical PWR/QON button wakes the T5 after PMU shutdown
- on-board T5 battery percentage / voltage reporting
- USB/external-power indication
- self-healing home-Wi-Fi and UDP-listener recovery
- last-known plant reading restored after T5 reboot while waiting for a fresh
  sensor report
- persistent `ESP PLANTS / OFF` e-paper screen before true PMU shutdown
- e-paper refresh protection

### T5 physical controls

- **PWR**: PMU/QON wake / power-on.
- **RST**: independent reset.
- **BOOT**: boot/programming control.
- **IO48-labeled button**: firmware function button.
  - short press enters/leaves setup mode,
  - long hold draws the final OFF screen, waits for the e-paper refresh, and
    requests BQ25896 PMU shutdown.

The physical `IO48` enclosure label is not the ESP32-S3 GPIO48 signal in this
firmware. The user-button state is read through PCA9535 P1.2; ESP32 GPIO48
remains the e-paper CKV line.

### XIAO soil sensor

- real board pin mapping and sensor excitation
- 10-sample soil measurement averaging
- battery measurement
- persistent Wi-Fi provisioning in NVS
- normal UDP broadcast to the LAN
- application ACK retry
- deep-sleep timer wake
- GPIO2 top-button wake
- two-minute manual service mode
- green LED remains on while manually awake
- automatic 5-second samples during service mode
- 10-second deliberate Wi-Fi reset hold
- adaptive local checking
- meaningful-change reporting
- state-change reporting
- 6-hour heartbeat
- watering detection with 5-minute then 10-minute follow-ups
- factory-style two-point dry/wet calibration stored per sensor in NVS

## Adaptive sensor policy

Current starting values:

| Condition | Local check interval |
|---|---:|
| Dry | 15 min |
| Almost dry | 15 min |
| Normal | 30 min |
| Wet | 30 min |
| First watering follow-up | 5 min |
| Later watering follow-ups | 10 min |
| Heartbeat | 6 hr |

Normal scheduled wakes do **not** automatically start Wi-Fi.

Wi-Fi/reporting is triggered when:

- there is no prior successful baseline,
- moisture changes by at least 4 percentage points,
- moisture state changes,
- a watering rise of at least 8 percentage points is detected,
- or the heartbeat is due.

This is important for battery life: a stable plant can wake, measure locally,
decide nothing meaningful changed, and return to deep sleep without joining
Wi-Fi.

## Plant names

Plant names deliberately live **only on the T5**.

A sensor identifies itself using its stable sensor ID. The T5 maps:

```text
sensor ID -> plant name
```

This keeps names easy to change and prevents the same name from being duplicated
through sensor firmware, hub code, and other entities.

## Calibration

The XIAO carries calibration because calibration belongs to the physical probe,
not to the plant name.

Default fallback endpoints currently preserve the measured development values:

- dry: `2875 mV`
- wet: `2104 mV`

A deliberate triple short-press while the sensor is awake enters two-point
calibration. See [docs/CALIBRATION.md](docs/CALIBRATION.md).

## Repository layout

```text
.
├── firmware/
│   ├── t5-hub/             # LILYGO T5 receiver/setup/display
│   └── xiao-soil-sensor/   # Seeed XIAO ESP32-C6 sensor firmware
├── docs/
├── tools/
├── .github/
├── AGENTS.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── SECURITY.md
└── VERSION
```

## Important PlatformIO rule

**Do not combine the T5 and XIAO into one PlatformIO project.**

They intentionally remain separate because they use different ESP32 Arduino
platform/toolchain generations.

Open and build one firmware folder at a time:

```text
firmware/t5-hub
firmware/xiao-soil-sensor
```

### T5 toolchain

- PlatformIO `espressif32@6.5.0`
- Arduino ESP32 2.0.x generation
- custom `T5-ePaper-S3` board definition
- M5GFX `0.2.24`
- XPowersLib `0.2.7`

### XIAO toolchain

- pioarduino platform release `55.03.37`
- board `seeed_xiao_esp32c6`
- Arduino 3.3.x generation

Each project uses its own local `.pio-packages` directory.

## Build

From the repository root, if `pio` is on PATH:

```bash
pio run -d firmware/t5-hub
pio run -d firmware/xiao-soil-sensor
```

Windows PowerShell helper:

```powershell
./tools/build_all.ps1
```

Linux/macOS helper:

```bash
./tools/build_all.sh
```

The helper builds the projects sequentially; it does not merge their
toolchains.

## First-time use

See [docs/SETUP.md](docs/SETUP.md).

Short version:

1. build/flash the T5,
2. enter T5 setup mode,
3. save home Wi-Fi,
4. build/flash the XIAO,
5. wake an unprovisioned XIAO near the T5,
6. use **Send Wi-Fi to Sensor** from the T5 setup page,
7. return the T5 to normal home-Wi-Fi mode,
8. verify UDP readings/ACK,
9. give the sensor a plant name on the T5,
10. calibrate the physical probe when ready.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Hardware](docs/HARDWARE.md)
- [Setup and provisioning](docs/SETUP.md)
- [Calibration](docs/CALIBRATION.md)
- [Protocol v3](docs/PROTOCOL.md)
- [Design decisions](docs/DECISIONS.md)
- [Baseline verification status](docs/BASELINE_STATUS.md)
- [Test plan](docs/TESTING.md)
- [Release checklist](docs/RELEASE_CHECKLIST.md)
- [Roadmap](docs/ROADMAP.md)
- [Changelog](CHANGELOG.md)

## Security status

This is a local-first project, but the current ESP-NOW provisioning exchange is
not yet authenticated/encrypted at the application level. Do not describe this
baseline as production-secure. See [SECURITY.md](SECURITY.md).

## License

A public-source license has **not been selected yet**. Choose one before treating
the repository as an open-source release.
