# HOBEIAN ZG-303Z integration notes

## Design rule

ZG-303Z decoding lives on the H2. The Waveshare receives normalized plant data
and does not need to know Tuya/Zigbee packet details.

Every raw vendor packet remains observable in H2 USB logs during bring-up.
Unknown Tuya datapoints are logged instead of silently discarded.

## Known stock-firmware datapoints

Current community drivers show at least two stock-firmware datapoint families.
The initial decoder deliberately accepts both where known:

| Meaning | Known DP IDs |
| --- | --- |
| Temperature, tenths °C | 5, 101 |
| Soil moisture % | 3, 107 |
| Battery % | 15, 108 |
| Air humidity % | 109 |
| Water warning | 1, 14 |
| Soil calibration | 102 |
| Temperature calibration | 104 |
| Humidity calibration | 105 |
| Temperature unit | 106 |
| Soil warning threshold | 110 |
| Temperature sample interval | 111 |
| Soil sample interval | 112 |

This table is a starting decoder, not a claim that every HOBEIAN firmware build
uses the same mapping.

## Standard Zigbee fallback

The H2 also watches normal Zigbee attribute reports so the product does not
depend only on Tuya 0xEF00:

- 0x0402 measured temperature
- 0x0405 measured humidity
- 0x0001 battery percentage remaining

Soil moisture is expected to require the vendor/Tuya path on stock ZG-303Z.

## Pairing UX

The screen should eventually instruct the user to hold the sensor's water/drop
button **until its red LED begins flashing**, rather than promise one exact hold
time. Published instructions vary between firmware/manual revisions.

## Sleepy-device behavior

The ZG-303Z is battery powered and can sleep between reports. Configuration
writes (sample interval, calibration, warning threshold) therefore need a
pending-command queue and should be applied when that sensor next wakes.
That command queue is intentionally a later layer; first bring-up proves receive
traffic and stable IEEE identity before writing settings back to sensors.
