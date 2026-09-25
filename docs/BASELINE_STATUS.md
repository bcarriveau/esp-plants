# Baseline status

## Current product baseline

The active development baseline is the `waveshare-zigbee` branch:

```text
HOBEIAN ZG-303Z
      |
    Zigbee
      |
M5Stack ESP32-H2
      |
 PlantLink UART
      |
Waveshare ESP32-S3 7-inch
```

The original Waveshare 7-inch target is the physically verified display baseline. The Waveshare 7B target exists in source but is not claimed as physically verified ESP PLANTS runtime hardware.

The Waveshare and H2 are independently versioned firmware products. The root `VERSION` tracks the Waveshare release identity.

## Current verified behavior

Repository history and current documentation establish physical verification on the original Waveshare 7-inch development hardware for:

- H2 Zigbee coordinator startup,
- persistent Zigbee network behavior,
- ZG-303Z commissioning,
- IEEE-64 identity,
- current ZG-303Z decoding,
- soil moisture,
- temperature,
- air humidity,
- battery,
- PlantLink UART communication,
- live sensor-data delivery to the Waveshare UI.

This file does not extend those claims beyond what has actually been tested.

## Current source status

The active source contains:

- Waveshare 800x480 application/UI,
- H2 Zigbee coordinator/translator,
- authoritative PlantLink protocol in `shared/plantlink/plantlink.h`,
- Waveshare Wi-Fi/setup behavior,
- A/B Waveshare OTA package installation,
- verified-HTTPS update retrieval,
- H2-through-Waveshare OTA implementation and release metadata.

H2-through-Waveshare OTA is **implemented in source but not claimed as physically verified**.

## Freshness rule

Known sensors may be restored by identity after a reboot, but pre-reboot readings are not treated as current. A sensor becomes current only after reporting during the present boot, and watering summaries must exclude stale values.

## Legacy baseline history

The repository also preserves the original LILYGO T5 4.7-inch S3 Pro + Seeed XIAO ESP32-C6 Wi-Fi/UDP implementation.

`BASELINE_MANIFEST.md` records hashes for that original imported baseline. It is historical evidence and should remain intact.

Legacy T5/XIAO sources remain useful for comparison and history, but they are not the current product architecture.

## Release gate

Before a wider/stable release, require at minimum:

- clean Waveshare 7 build,
- clean H2 build,
- repository tests pass,
- release assets produced from the release tooling,
- update identity/hash checks validated,
- sensor registry/freshness behavior verified,
- persistence verified across normal update,
- long-duration H2/Zigbee stability testing,
- explicit physical verification of H2-through-Waveshare OTA before claiming it,
- separate physical verification of Waveshare 7B before claiming 7B support as proven.
