# Waveshare ESP32-S3 hub firmware

Waveshare ESP32-S3-Touch-LCD-7 application controller for the direct-Zigbee
ESP PLANTS branch.

## Current bring-up scope

This first executable scaffold deliberately reuses the exact display/touch stack
from Bill's hardware-proven ESP Aircraft Radar project:

- pioarduino platform 51.03.07 / Arduino-ESP32 3.0.7 generation
- ST7262 800x480 RGB panel
- GT911 touch
- the same custom panel timings/pin map
- Waveshare_ST7262_LVGL v0.1
- ESP32_Display_Panel v0.1.4
- LVGL 8.3.11

The only deliberate USB/UART change is architectural:

- `Serial` = native USB CDC debug
- `Serial0` GPIO43/44 = dedicated PlantLink to the H2 through UART2

That lets the Waveshare UART selector remain on UART2 without losing VS Code
serial diagnostics over the board's native USB port.

## Current screen

The bring-up screen shows:

- H2 link status
- Zigbee coordinator status/channel
- sensor count
- latest join/plant reading event
- an `ADD SENSOR` button that sends a 120-second permit-join request to the H2

This is not the final plant dashboard. It exists to prove the complete physical
path before UI work piles on top of it.

## Persistence layout

The 16 MB flash layout has dual 6 MB OTA app slots plus a separate `plantdata`
NVS partition and separate history partition. Normal application OTA therefore
does not require erasing plant/user data.
