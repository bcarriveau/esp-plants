# Security

## Scope

The active ESP PLANTS product line is local-first:

```text
ZG-303Z -> Zigbee -> M5Stack ESP32-H2 -> PlantLink UART -> Waveshare ESP32-S3
```

Home Assistant, MQTT, Zigbee2MQTT, and a cloud account are not required for normal operation.

The older T5/XIAO Wi-Fi/UDP/ESP-NOW design remains in the repository as a legacy product line and is not the security model for current Waveshare/H2 development.

## Trust boundaries

### Zigbee / M5Stack ESP32-H2

The H2 is the Zigbee coordinator and owns the Zigbee network, permit-join state, device handling, ZG-303Z translation, and Zigbee persistence.

Commissioning should be user-initiated and time-bounded. Zigbee network reset must remain a deliberate action; normal firmware updates must not erase the network.

### PlantLink UART

PlantLink is the local wired protocol between the H2 and Waveshare. It uses framed messages with COBS/CRC integrity checks, but the physical UART link is not an authenticated or encrypted security boundary.

Physical access to the exposed UART should therefore be treated as trusted-device access.

### Waveshare Wi-Fi

Wi-Fi belongs to the Waveshare. Saved Wi-Fi credentials are device-local persistent data.

Setup/change flows must not overwrite known-good credentials until the new connection succeeds. Disconnect should not erase credentials; Forget Wi-Fi should require deliberate confirmation before erasing them.

Firmware diagnostics must never intentionally print Wi-Fi passwords.

## OTA / release security

The current Waveshare updater is designed around ESP PLANTS-specific release assets rather than arbitrary raw firmware.

The update path includes:

- product/hardware/build identity checks,
- package and firmware SHA-256 verification,
- ESP32-S3 image validation,
- certificate-verified HTTPS,
- bounded redirects/download sizes,
- A/B application partitions,
- inactive-slot-only writes,
- boot-partition activation only after full validation,
- persistence outside normal application-image replacement.

Release tooling builds a distribution-marked H2 image and includes H2 identity/hash metadata in the Waveshare manifest.

The source contains H2-through-Waveshare OTA transfer over PlantLink. **Do not describe that path as physically verified until it has been exercised and confirmed on the actual Waveshare + M5Stack H2 hardware.**

## Release assets

Files in `release/` are generated output, not a place to keep historical firmware that could be mistaken for the current release.

Do not hand-create or rename OTA assets. Generate current assets with `tools/make-waveshare-release.cmd` and publish only assets matching the intended release identity and hashes.

## Persistent user data

Normal OTA must not intentionally erase:

- plant names,
- device name,
- sensor assignments,
- Zigbee/router records,
- Wi-Fi credentials,
- user settings,
- intentionally persistent plant data.

Any persistence schema change needs backward compatibility or explicit migration.

## Logs and public reports

Before posting logs publicly, remove information you do not want published, such as:

- Wi-Fi SSIDs,
- local IP/network details,
- device identifiers or IEEE/MAC addresses,
- firmware dumps or configuration files containing private data.

Never publish real credentials in an issue.

## Reporting security problems

Until a dedicated private security contact/process is established, do not post exploitable details containing real credentials in a public issue. Open a minimal issue requesting a private contact path instead.
