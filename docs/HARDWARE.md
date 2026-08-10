# Hardware

## LILYGO T5 hub

Target family:

- LILYGO T5 4.7-inch S3 Pro / H752-01 style board
- ESP32-S3
- 960×540 ED047TC1-class e-paper panel
- PSRAM
- PMU / e-paper power circuitry
- capacitive touch hardware available on the board, although the current
  receiver/setup baseline is centered on display + function-button behavior

### Current T5 source pin/constants

The current firmware defines:

| Function | Pin / address |
|---|---|
| I2C SDA | GPIO39 |
| I2C SCL | GPIO40 |
| PCA9535 | `0x20` |
| BQ25896 | `0x6B` |
| TPS65185 | `0x68` |
| EPD CKH | GPIO4 |
| EPD D0 | GPIO5 |
| EPD D1 | GPIO6 |
| EPD D2 | GPIO7 |
| EPD D7 | GPIO8 |
| touch reset | GPIO9 |
| backlight | GPIO11 |
| EPD D3 | GPIO15 |
| EPD D4 | GPIO16 |
| EPD D5 | GPIO17 |
| EPD D6 | GPIO18 |
| EPD STH | GPIO41 |
| EPD LE | GPIO42 |
| EPD STV | GPIO45 |
| EPD CKV | GPIO48 |

The function button in the current source is read through the PCA9535 as P1.2.

**Do not treat GPIO48 as a spare input in this codebase.** It is assigned to
the e-paper CKV line.

Board revisions and physical button labeling can be confusing. Verify physical
behavior before changing power/button code.

## Seeed XIAO ESP32-C6 Soil Moisture Monitor

Current verified firmware mapping:

| Function | GPIO |
|---|---:|
| battery ADC | 0 |
| soil ADC | 1 |
| top button, active-low | 2 |
| factory LOW | 3 |
| factory HIGH | 14 |
| yellow LED | 18 |
| green LED | 19 |
| red LED | 20 |
| sensor excitation PWM | 21 |

Sensor excitation:

- 200 kHz
- 7-bit PWM configuration
- approximately 68% duty

The firmware avoids relying on a permanently powered probe. It starts the
excitation when the sensor wakes and measures.

## ADC / calibration

Soil:

- 10 normal soil samples per measurement
- percentage calculated from the active dry/wet endpoints

Battery:

- 16 ADC samples
- current simple mapping:
  - 1200 mV = 0%
  - 1500 mV = 100%

These battery endpoints are product calibration values, not a claim about the
raw battery-cell terminal voltage.

## Power behavior

### XIAO

Deep sleep enables both:

- timer wake,
- GPIO2 top-button wake.

### T5

The current receiver uses the board PMU shutdown path for deliberate power-off.
Do not replace real PMU shutdown with deep sleep and call it equivalent; the two
behaviors have different wake semantics.

## Hardware change rule

Pin assignments and power sequencing are hardware-critical.

Any change should include:

1. source/documentation evidence,
2. a build,
3. actual-board verification,
4. a CHANGELOG note when user-visible behavior changes.
