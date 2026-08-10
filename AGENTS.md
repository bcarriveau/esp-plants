# Repository working rules

These rules are part of the project baseline and should be followed by coding
agents and contributors.

## Source of truth

1. Current repository files.
2. Current intended Git branch.
3. Confirmed hardware test results and logs.
4. Historical notes.

Never overwrite newer repository work with older chat/context material.

## PlatformIO separation

The T5 and XIAO firmware are intentionally separate PlatformIO projects.

Do not create one combined root PlatformIO environment and do not make them
share a PlatformIO package directory.

Reason: the T5 and XIAO currently require different ESP32 Arduino platform /
toolchain generations.

## Protocol discipline

`firmware/t5-hub/include/plant_protocol.h` and
`firmware/xiao-soil-sensor/include/plant_protocol.h` must remain byte-identical.

Run:

```bash
python tools/check_protocol_sync.py
```

If the wire layout changes:

1. update both copies,
2. increment `PROTOCOL_VERSION` when compatibility requires it,
3. document the exact packet/layout change in `docs/PROTOCOL.md`,
4. add a CHANGELOG entry,
5. verify both firmware builds before hardware testing.

Do not casually reuse reserved fields for incompatible semantics.

## Naming architecture

Plant names belong on the T5.

Do not add plant-name strings to XIAO firmware, provisioning packets, or
hard-coded sensor definitions unless the architecture is explicitly changed.

The sensor should remain identified by stable sensor ID.

## Sensor calibration

Physical dry/wet calibration belongs on the XIAO because it describes the
individual probe.

Do not tie calibration to a plant name.

Do not write calibration to flash on every sample. NVS is for deliberate
calibration/configuration; transient adaptive history belongs in RTC-retained
state.

## Transport architecture

Normal telemetry:

```text
XIAO -> home Wi-Fi/router/mesh -> UDP -> T5 -> application ACK
```

Provisioning:

```text
T5 setup mode -> nearby ESP-NOW -> XIAO
```

Do not switch normal telemetry back to direct ESP-NOW without an explicit
architecture decision.

## Product constraints

- standalone/local-first
- no required Home Assistant
- no required MQTT
- no required cloud account
- no required fixed T5 IP
- e-paper behavior must avoid unnecessary refreshes
- sensor battery behavior must avoid unnecessary Wi-Fi use

## Hardware changes

Do not guess pin mappings.

Before changing pin assignments, power sequencing, PMU behavior, e-paper bus
details, sensor excitation, ADC behavior, or wake sources:

1. inspect the current code,
2. inspect the relevant board/source documentation when necessary,
3. prefer physically observed behavior when a board revision differs,
4. document the reason.

## Change discipline

For a behavior change:

- keep the change narrow,
- preserve unrelated work,
- update `CHANGELOG.md`,
- update affected documentation,
- update tests/checklists where appropriate,
- clearly distinguish "compiled" from "structurally inspected",
- clearly distinguish "hardware verified" from "not yet physically verified."

Do not claim a build passed unless a build actually ran.

Do not tag a stable release until `docs/RELEASE_CHECKLIST.md` passes.
