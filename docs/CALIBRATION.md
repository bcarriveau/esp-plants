# Soil calibration

## Why calibration belongs on the sensor

Plant names are human metadata and live on the T5.

Dry/wet calibration describes the electrical behavior of an individual physical
probe, so it lives on the XIAO.

This keeps renaming and calibration independent.

## Current fallback values

If no valid saved calibration exists, Phase 3E currently falls back to:

```text
dry = 2875 mV
wet = 2104 mV
```

These are development values measured from the test sensor and are intentionally
preserved so a firmware update does not silently rescale an already-used probe.

They should not be assumed to be universal values for every sensor and soil.

## Percentage conversion

The current firmware uses a linear mapping between active endpoints:

```text
dry endpoint -> 0%
wet endpoint -> 100%
```

Values outside the range are clamped.

Moisture-state bands are currently:

| Percent | State |
|---:|---|
| 0–20 | DRY |
| 21–40 | ALMOST DRY |
| 41–80 | NORMAL |
| 81–100 | WET |

These are product-state thresholds and can be revisited independently of the
physical dry/wet calibration.

## Starting calibration

Calibration is available while already awake in the two-minute green-LED
service mode.

1. Wake the XIAO using the top button.
2. Release it.
3. Triple short-press the top button.

## Dry stage

- red LED flashes during a 10-second placement window,
- place the probe in the intended dry reference soil,
- firmware then averages 10 soil readings,
- samples are separated by 200 ms.

## Wet stage

After the dry capture and a short pause:

- green LED flashes during a 10-second placement window,
- place the probe in the intended fully wet/saturated reference soil,
- firmware averages 10 readings,
- samples are separated by 200 ms.

## Validation

Calibration is rejected if:

- dry or wet value is invalid,
- dry voltage is not higher than wet voltage,
- endpoint span is less than 150 mV,
- ADC values are outside broad sanity bounds.

Success:

- two quick green flashes,
- values are persisted in the `plantcal` NVS namespace.

Failure:

- two quick red flashes,
- old calibration remains active.

## After calibration

The firmware clears adaptive RTC percentage history because percentages from the
old scale should not be compared with percentages from the new scale.

It then resumes service operation and can send a fresh reading with the new
scale.

## Recommended real-world method

For meaningful plant percentages, use representative potting soil rather than
trying to force the XIAO to match another manufacturer's percentage display.

A useful process is:

1. establish a genuinely dry reference,
2. record the dry endpoint,
3. saturate representative soil thoroughly,
4. allow water to distribute rather than using only the instant when water
   directly hits the probe,
5. record the wet endpoint,
6. observe the resulting values for several watering/drying cycles,
7. adjust state thresholds only after the physical endpoints are trustworthy.

Two brands can legitimately show different percentages in the same pot because
their sensing geometry and calibration scale can differ.

## NVS wear

Calibration is written only when the user deliberately completes calibration.

Do not move ordinary sample history into NVS. The sensor can wake many times
during its life; transient sample history belongs in RTC-retained state.
