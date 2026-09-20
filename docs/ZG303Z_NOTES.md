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
- 0x0405 measured-value frame (legacy HOBEIAN firmware mirrors soil moisture here)
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
## Hardware observation: HOBEIAN ZG-303Z

Physical testing on the ESP32-H2 coordinator confirmed this stock sensor family
uses Tuya EF00 DP 5 for temperature (tenths C), DP 3 for soil moisture, DP 15
for battery percentage, and DP 109 for air humidity.

The same device also emits standard Zigbee temperature and battery reports.
Although it uses the standard 0x0405 Relative Humidity cluster format, physical
captures proved that this HOBEIAN firmware mirrors soil moisture there: DP3=0
was followed by 0x0405=0, and DP3=98 was followed by 0x0405=9800. Actual air
humidity is DP109. The H2 therefore treats 0x0405 as a soil-moisture fallback
for this ZG-303Z path and never uses it as air RH.

The sensor battery report was cross-checked against physical battery voltage:
with a low pair it reported about 2.5 V / 5%, and with a fresh pair it reported
3.0 V / 100%. Battery decoding should not be rescaled in the H2.

DP106 was hardware-observed as a boolean on this legacy HOBEIAN mapping and
correlates with the dry/water-shortage state: it was 1 with dry soil and 0 after
the probe was returned to wet soil. Independent HOBEIAN ZG-303Z community
mapping also identifies legacy DP106 as water shortage. ESP PLANTS therefore
uses DP106 as water warning only after legacy DP3/5/15 mapping evidence has
been seen; alternate ZG-303Z mappings are not forced to that meaning.

The user-facing C/F preference remains a Waveshare UI setting while PlantLink
keeps normalized temperature in centi-degrees C.
