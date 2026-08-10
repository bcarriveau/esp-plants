# XIAO Soil Sensor Firmware

Current imported baseline: **Phase 3E factory-style calibration + adaptive
sensor scheduling**

Build this folder as its own PlatformIO project.

## Responsibilities

- physical soil/battery measurement
- Wi-Fi provisioning persistence
- UDP v3 sender / ACK retry
- deep sleep
- top-button service mode
- adaptive local checking/reporting
- watering follow-up
- per-sensor calibration

## Plant-name rule

Human-readable plant names do not belong in this firmware.

The XIAO sends its stable sensor ID and the T5 maps that ID to the plant name.

## Calibration

Fallback defaults:

```text
dry = 2875 mV
wet = 2104 mV
```

Triple short-press while awake enters dry/wet calibration.

See `../../docs/CALIBRATION.md`.

## Build

```bash
pio run
```

From repository root:

```bash
pio run -d firmware/xiao-soil-sensor
```

See the repository root documentation for architecture, setup, protocol, and
verification status.
