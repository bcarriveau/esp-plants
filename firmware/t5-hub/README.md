# T5 Hub Firmware

Current imported baseline: **Phase 3B home-Wi-Fi / UDP receiver + setup portal**

Build this folder as its own PlatformIO project.

## Responsibilities

- LILYGO T5 e-paper/display/power handling
- setup hotspot and local setup web page
- home-Wi-Fi configuration
- nearby ESP-NOW sensor provisioning
- central `sensor_id -> plant name` persistence
- UDP v3 receiver on port 42100
- application ACK

## Important compatibility note

The v3 ACK currently contains `next_wake_seconds` and this T5 baseline may still
populate the development value of 60 seconds.

The current Phase 3D+ / Phase 3E XIAO firmware intentionally uses its own
adaptive scheduler and does not treat this ACK value as the authoritative local
wake interval.

## Build

```bash
pio run
```

From repository root:

```bash
pio run -d firmware/t5-hub
```

See the repository root documentation for architecture, setup, protocol, and
verification status.
