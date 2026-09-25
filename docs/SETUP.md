# Setup and development

## 1. Open the repository root

Use the repository root as the permanent VS Code workspace.

Do not open each firmware project as a separate workspace. The firmware projects remain independent PlatformIO build roots under one workspace.

## 2. Build the active product

The normal active-product pair is:

- `firmware/waveshare-hub`
- `firmware/m5-h2-zigbee`

Use root VS Code tasks, or run PlatformIO directly.

Windows examples:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"

& $pio run -d ".\firmware\waveshare-hub"
& $pio run -d ".\firmware\m5-h2-zigbee"
```

The current Waveshare 7 developer environment retains `upload_port = COM11`.

## 3. Connect Waveshare and H2

The active architecture is direct PlantLink UART between the Waveshare and M5Stack H2.

The H2 owns Zigbee. The Waveshare owns UI, user configuration, Wi-Fi, and updates.

Do not substitute the legacy T5/XIAO Wi-Fi/UDP path when setting up the active product.

## 4. Form/use the Zigbee network

Use the Waveshare Settings/Add Sensor flow to request permit join through PlantLink.

The H2 is the coordinator. ZG-303Z sensors join the H2 Zigbee network and are identified persistently by IEEE-64 address.

Network reset should only occur through an intentional reset action; normal reboot/update must not erase Zigbee state.

## 5. Sensor freshness after reboot

Known sensor identities/names may be restored immediately on the Waveshare.

Until a sensor reports during the current boot, it must remain waiting/not-yet-reported and must not contribute stale values to current watering summaries.

## 6. Wi-Fi setup

Wi-Fi is configured on the Waveshare for setup/update functions.

The current source supports the local setup/update service and QR-based Wi-Fi assistance. A failed replacement connection must return to an editable state and must not destroy previously working credentials.

Disconnect and Forget Wi-Fi are separate behaviors.

## 7. Release builds

Use:

```text
tools\make-waveshare-release.cmd
```

The script builds:

1. H2 distribution firmware,
2. Waveshare distribution package and manifest.

Generated release assets belong in `release/` and must match the current firmware identities/hashes.

Do not manually rename old firmware binaries into a new release.

## 8. Waveshare 7B

Build the 7B target explicitly:

```powershell
& $pio run -d ".\firmware\waveshare-hub" -e waveshare_s3_touch_lcd_7b
```

A successful source/build target does not prove runtime behavior. ESP PLANTS 7B hardware verification must be recorded separately after physical testing.

## Legacy product setup

The T5/XIAO setup flow remains in the repository for the legacy product line. It is not the active setup path described by this document.
