# Architecture

## Goals

The project is designed to behave like a standalone appliance rather than a
Home Assistant accessory.

Core goals:

- central e-paper display,
- multiple battery soil sensors,
- easy plant renaming,
- local setup,
- reliable whole-house range using existing Wi-Fi infrastructure,
- low sensor battery consumption,
- no cloud dependency.

## Component responsibilities

### T5 hub

The T5 owns:

- plant-name mapping,
- home-Wi-Fi configuration for itself,
- local setup page,
- nearby sensor provisioning,
- UDP receiving,
- application ACKs,
- display state,
- persistent list of known sensors.

### XIAO sensor

Each XIAO owns:

- stable sensor identity,
- physical soil/battery measurement,
- its own Wi-Fi credentials,
- its own dry/wet calibration,
- adaptive wake/report decisions,
- deep-sleep behavior,
- top-button service behavior.

The sensor does **not** own the human-readable plant name.

## Normal telemetry path

The XIAO performs a cheap local check first.

```text
deep sleep
   |
   v
wake CPU
   |
   v
start soil excitation
   |
   v
take averaged soil + battery reading
   |
   v
compare with RTC-retained history
   |
   +---- no meaningful report ----> deep sleep
   |
   +---- report needed
             |
             v
        join home Wi-Fi
             |
             v
        UDP broadcast reading
             |
             v
        T5 validates packet
             |
             v
        T5 sends ACK
             |
             v
        XIAO deep sleeps
```

This separation between a **local check** and a **network report** is one of the
main battery-saving design decisions.

## Adaptive reporting

Report triggers currently include:

- no previous successful report,
- >=4 percentage-point change since the last successful report,
- moisture-state change,
- >=8 percentage-point upward jump interpreted as watering,
- heartbeat age >=6 hours.

The adaptive state is RTC-retained across deep sleep rather than written to NVS
on every wake.

## Watering watch

A watering event needs higher temporal resolution than a stable plant.

Current policy:

1. detect an upward rise of >=8 points,
2. send the changed reading,
3. next local check after 5 minutes,
4. then 10-minute follow-ups,
5. exit after two sufficiently stable follow-ups or the follow-up cap,
6. resume ordinary 15/30-minute local checks.

Water hitting the probe during manual service mode also arms this watch.

## Manual service mode

GPIO2 button wake means a person is physically interacting with the sensor.

The sensor therefore:

- turns on the green LED,
- remains awake for two minutes of inactivity,
- takes/sends readings every 5 seconds,
- allows manual fresh-read presses,
- supports triple-press calibration,
- supports deliberate long-hold Wi-Fi reset.

This is intentionally more active than unattended battery mode.

## Provisioning path

Unprovisioned XIAO:

1. starts ESP-NOW provisioning mode,
2. stays available for a bounded window,
3. sends discovery/beacon information.

T5 setup mode:

1. leaves home Wi-Fi,
2. starts its local setup AP,
3. uses ESP-NOW channel 1,
4. discovers nearby unprovisioned sensors,
5. sends saved home-Wi-Fi credentials to the selected sensor,
6. receives provisioning acknowledgement.

The XIAO stores the credentials and reboots into normal home-Wi-Fi operation.

## Why the router/mesh is used

Direct ESP-NOW was useful for proving the radio/protocol path but was not chosen
as the normal whole-house transport because fringe placement made return ACK
reliability dependent on T5 location.

The home Wi-Fi/router/mesh already exists to solve house coverage. The plant
system uses that infrastructure while remaining an otherwise standalone local
application.

## Persistence

### T5 NVS

Stores product/setup configuration including:

- setup password,
- home-Wi-Fi credentials,
- known sensor records,
- plant names.

### XIAO NVS

Separate records currently store:

- network provisioning configuration,
- physical calibration.

### XIAO RTC-retained state

Stores transient adaptive state across deep sleep:

- previous observed moisture,
- previous successfully reported moisture,
- watering-watch state,
- heartbeat age accounting,
- planned sleep duration.

## Scale

The T5 currently allows up to 16 persisted plant records.

Protocol identity is based on a stable `uint32_t sensor_id`.

## Future configuration direction

If plant-specific thresholds or timing are added later, the preferred design is:

- configure them centrally from the T5,
- send explicit machine settings to the XIAO,
- keep the human-readable name T5-only.

That preserves the "rename once" architecture.
