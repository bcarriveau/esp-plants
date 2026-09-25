# Hardware

## Active ESP PLANTS hardware

### Waveshare ESP32-S3 Touch LCD 7

Role: user-facing application/display controller.

- ESP32-S3
- 800x480 touch display
- LVGL UI
- Wi-Fi for setup/update functions
- PlantLink UART connection to the H2
- current physically verified Waveshare ESP PLANTS baseline

Do not change connector pin order, GPIO mapping, voltage, panel timing, or power behavior from assumption. Inspect the current board headers/source and verify physical hardware before marking a change proven.

### Waveshare ESP32-S3 Touch LCD 7B

Role: alternate display hardware target sharing the ESP PLANTS application.

The repository has a dedicated `waveshare_s3_touch_lcd_7b` PlatformIO environment and board-specific boundary.

**Runtime verification on ESP PLANTS 7B hardware is not claimed yet.**

### M5Stack ESP32-H2 Thread/Zigbee Gateway Unit

Role: Zigbee coordinator/translator.

Current project responsibilities:

- ESP32-H2 native 802.15.4 radio
- Zigbee coordinator role
- ZG-303Z commissioning/device handling
- Zigbee persistence
- Grove/UART PlantLink connection to the Waveshare
- USB serial diagnostics/development
- H2 A/B application partitions used by the Phase 2 OTA implementation

Current firmware source documents H2 PlantLink UART on RX GPIO23 / TX GPIO24.

### HOBEIAN ZG-303Z

Role: battery-powered Zigbee plant sensor.

The physically verified current data path includes:

- soil moisture,
- temperature,
- air humidity,
- battery,
- IEEE-64 identity,
- current project Zigbee/Tuya decoding.

ZG-303Z firmware variants may not share identical Tuya datapoint layouts; keep raw diagnostics available and do not generalize one observed mapping without evidence.

## Development upload target

The current Waveshare 7 developer environment preserves:

```text
upload_port = COM11
```

This is a local development configuration and should not be silently removed while editing `platformio.ini`.

## Hardware verification rules

A source/build target is not the same as physical verification.

For hardware-sensitive changes:

1. inspect current source,
2. inspect current vendor/board documentation when necessary,
3. build the intended firmware,
4. verify on the actual hardware,
5. record what was physically tested.

Do not claim Waveshare 7B runtime verification or H2-through-Waveshare OTA hardware verification until those tests actually occur.

## Legacy hardware

The repository retains the LILYGO T5 4.7-inch S3 Pro hub and Seeed XIAO ESP32-C6 soil-sensor implementation as a legacy product line.

Its detailed pin/calibration/power information belongs to that legacy source/history and should not be used as the active Waveshare/H2 hardware description.
