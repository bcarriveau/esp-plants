# Roadmap

This document describes direction for the active Waveshare/H2/ZG-303Z product line. It is not a promise that every item belongs in the next release.

## Near term

### Prove the active hardware matrix

- keep the original Waveshare 7-inch target as the hardware-proven baseline,
- physically verify the Waveshare 7B ESP PLANTS runtime before labeling it proven,
- verify long-duration H2 coordinator stability with sleepy ZG-303Z sensors,
- exercise reboot/rejoin behavior without presenting stale readings as current.

### Complete release/update verification

The source contains the Waveshare A/B updater and the H2-through-Waveshare Phase 2 path.

Next validation work:

- build a complete release package from clean source,
- confirm manifest/product/hardware/build/hash validation,
- verify inactive-slot Waveshare installation and restart behavior,
- physically verify H2 firmware transfer over PlantLink,
- confirm H2-first / Waveshare-second ordering,
- confirm Zigbee network and all user configuration survive the update,
- verify recovery behavior for interrupted/failed updates.

Do not label H2-through-Waveshare OTA physically verified until these tests are actually performed.

### Wi-Fi / setup reliability

Continue hardening the current Waveshare flow:

- nearby SSID scan/selection,
- on-screen QR-assisted phone setup,
- clear CONNECTING / SUCCESS / FAILED states,
- editable retry after failure,
- disconnect without credential erasure,
- deliberate Forget Wi-Fi confirmation,
- no replacement of known-good credentials until a new connection succeeds.

### Sensor-scale behavior

- continue validating ALL SENSORS with 10+ devices,
- verify waiting/not-yet-reported state after reboot,
- keep `WHO NEEDS WATER?` limited to fresh current-boot moisture reports,
- validate router/repeater registry behavior as the Zigbee mesh grows.

## Reliability

- prolonged Waveshare + H2 soak testing,
- Zigbee coordinator reboot recovery,
- sensor rejoin behavior,
- power-cycle recovery,
- Wi-Fi router/AP outage recovery,
- memory/heap stability during UI navigation and update checks,
- repeated update-check and failed-update recovery.

## Security

- document the physical trust boundary of PlantLink UART,
- keep verified HTTPS and identity/hash validation mandatory for OTA,
- keep permit join deliberate and bounded,
- define a private vulnerability-reporting path before wider distribution,
- threat-model release signing/authenticity improvements beyond transport TLS/hash checks if the product moves toward broader deployment.

## Future convenience

Possible work after the active core is stable:

- export/import Waveshare configuration,
- improved local diagnostics,
- per-device firmware/version display,
- H2 update/recovery UX refinements,
- optional local API that does not introduce a required cloud dependency.

## Legacy line

T5/XIAO work remains preserved for historical/reference purposes. New active-product roadmap items should not be written as if the T5/XIAO Wi-Fi/UDP architecture is current.
