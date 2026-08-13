# Changelog

This project uses this file for user-visible behavior, protocol, hardware, and
architecture changes. Do not rely on commit messages alone for project history.

## [Unreleased]

### Authoritative sensor identity / service checkpoint

#### Added

- T5-owned authoritative sensor numbering for slots `#1` through `#16`, with
  the persisted T5 array index remaining the source of the visible sensor number.
- Additive protocol-v3 identity `ASSIGN` / `CLEAR` packets and identity ACKs
  without changing the existing reading, ACK, provisioning, or Locate packet
  layouts.
- XIAO NVS cache for the currently assigned T5 slot, while keeping the physical
  sensor ID as the immutable hardware identity.
- RED numbered identity indication on the XIAO after current-wake T5
  confirmation: short RED pulse = 1, long RED pulse = 10.
- T5 setup-page and e-paper slot-number visibility plus `Identify Sensor #n`.
- T5 UDP sequence-number logging so sender retries can be compared directly
  against received packet sequences.
- ESP32-S3 USB CDC-on-boot configuration for the T5 so application `Serial`
  diagnostics appear on the same USB connection as the ROM boot output.

#### Changed

- Manual XIAO service wake keeps GREEN on while Wi-Fi and T5 identity
  confirmation complete, briefly shows the confirmed RED sensor number, then
  returns to solid GREEN service state.
- An ordinary short press during the two-minute service window restarts the
  timer, replays the confirmed RED sensor number, then sends a fresh reading.
- The 5-second service auto-sampler pauses while the firmware is waiting to
  determine whether a button action is a single/two-click action or the
  existing triple-click calibration command.
- Automatic T5 identity assignment is throttled across UDP and ESP-NOW so a
  normal two-minute service session receives one automatic assignment instead
  of another `ASSIGN` every 5 seconds.
- XIAO startup diagnostics now report the actual hardware STA MAC address and
  no longer print a literal `\n` in the cached-slot message.

#### Verified on hardware

- T5 `#1` assignment was accepted and cached by XIAO sensor
  `0xDFBBF6A6`.
- Manual wake showed the confirmed RED `#1` indication followed by GREEN
  service state.
- Re-pressing the service button restarted the two-minute timer and replayed
  the confirmed sensor number.
- Repeated automatic identity-assignment chatter was eliminated after the
  initial assignment.
- T5 application serial output was restored over native ESP32-S3 USB CDC.
- T5 received UDP reading sequences `49` through `72` consecutively with no
  gaps during the diagnostic service run.
- XIAO retry logging showed intermittent first-attempt UDP ACK timeouts while
  still completing every tested exchange within the bounded retry count.

### Added

- T5 on-board battery state-of-charge and battery voltage reporting from the
  BQ27220 fuel gauge.
- T5 power source, battery, Wi-Fi, IP, and UDP-listener diagnostics on the local
  web status page.
- T5 battery/power indication in the e-paper header.
- Throttled NVS caching of the last-known plant reading so a T5 reboot can show
  useful plant data while waiting for a sleeping sensor to report again.
- Final 960x540 `ESP PLANTS / OFF` e-paper screen with
  `Designed by Bill Carriveau` and `Press PWR to wake`.

### Fixed

- T5 home Wi-Fi / UDP receive path now recovers after Wi-Fi loss instead of
  leaving a dead UDP listener until the next reset.
- UDP listener startup is retried if the initial bind fails.
- Added a short post-Wi-Fi settling period before the first UDP bind to reduce
  the battery-reset timing issue where Wi-Fi appeared connected but sensor
  packets were not received.
- A fresh sensor report always replaces the `LAST KNOWN READING` state even when
  the new moisture/battery values are unchanged.

### Changed

- The physical button labeled `IO48` is now named/documented as the T5
  function/setup/shutdown button.
- Long-holding the physical IO48-labeled button draws the final OFF image,
  waits for the e-paper refresh to finish, and then requests PMU shutdown.
- Physical `PWR` is documented as the PMU/QON wake/power-on control; `RST`
  remains the separate reset button.
- ESP32-S3 GPIO48 remains the e-paper CKV signal; the physical IO48-labeled
  function button is read through the PCA9535 expander.

### Verification status

- Battery-only reset/UDP receive, BQ27220 battery reporting, last-known restore,
  OFF-screen persistence, IO48 shutdown, and PWR/QON wake have been physically
  exercised during development.
- Authoritative T5 slot assignment, XIAO identity caching, RED numbered
  identification, service-button timer restart, USB CDC diagnostics, and
  sequential UDP reception have been physically verified on the current
  development hardware.
- Full multi-sensor enrollment/replacement/unassign lifecycle and final release
  verification remain open.
### Planned / open

- Physically verify the complete Phase 3E calibration workflow.
- Decide and document final production moisture-state thresholds.
- Add authenticated/encrypted provisioning before calling provisioning
  production-ready.
- Decide whether adaptive thresholds/intervals should become T5-configurable.
- Expand the T5 dashboard from the current receiver/setup baseline.
- Add a stable release tag only after both firmware projects build cleanly and
  the release checklist passes on hardware.

## [0.1.0-alpha.1] - 2026-08-09

### Added

- Established the first clean GitHub-oriented repository baseline.
- Added both firmware projects as intentionally separate PlatformIO projects.
- Added repository documentation, test plan, release checklist, security notes,
  development rules, CI skeleton, issue templates, and protocol-sync checking.
- Added a baseline source manifest so the imported firmware can be identified
  later.

### Baseline firmware

- T5: Phase 3B home-Wi-Fi / UDP receiver and local setup/provisioning portal.
- XIAO: Phase 3E adaptive sender with factory-style per-sensor calibration.
- Shared wire protocol: v3.

### Verification note

This repository baseline is a development starting point, not a claim that every
new Phase 3E path has already passed final hardware verification.

---

# Pre-GitHub development history

These entries summarize confirmed project milestones that produced the current
baseline.

## Phase 3E - Factory-style calibration

### Added

- Restored per-sensor two-point calibration behavior inspired by the factory
  firmware.
- Triple short-press starts calibration while already awake in service mode.
- Dry stage and wet stage each use a placement window and 10 averaged samples.
- Calibration is validated before saving.
- Calibration values are stored in a separate XIAO NVS namespace.
- Existing home-Wi-Fi provisioning data remains separate.
- Adaptive RTC percentage history is cleared after a successful calibration so
  values calculated with the old scale are not compared to the new scale.

### Preserved

- Plant names remain T5-only.
- Adaptive scheduling and watering follow-up remain sensor-side.
- Normal Wi-Fi/UDP transport remains unchanged.

## Phase 3D.1 - Watering follow-up fix

### Fixed

- Watering detected during the two-minute manual service window now arms the
  same post-watering watch used by scheduled wakes.
- An initially soaked/WET probe can no longer end the service window and then
  automatically fall into the ordinary 30-minute WET sleep.
- Active watering watch forces the first follow-up after 5 minutes.

## Phase 3D - Adaptive plant logic

### Added

- Local sensor checks separate from network transmissions.
- 15-minute checks for DRY and ALMOST DRY.
- 30-minute checks for NORMAL and WET.
- Wi-Fi skipped when no meaningful report is needed.
- 4-percentage-point meaningful-change trigger.
- state-change reporting.
- 8-percentage-point watering-rise detection.
- 5-minute first watering follow-up.
- 10-minute later watering follow-ups.
- stable-reading exit logic for watering watch.
- 6-hour heartbeat.
- RTC-retained adaptive state across deep sleep.

### Changed

- Sensor-side adaptive logic became authoritative for the next local check.
- The development `next_wake_seconds` value returned in the T5 ACK is retained
  for protocol compatibility but no longer controls Phase 3D+ sensor scheduling.

## Phase 3C.1 - Automatic service sampling

### Added

- While the green service LED is on, the XIAO automatically takes and sends a
  fresh measurement every 5 seconds.
- Physical actions such as removing, wiping, reinserting, or watering the probe
  can be observed without another button press.

## Phase 3C - Button service mode

### Added

- GPIO2 top-button deep-sleep wake.
- Two-minute manual-awake service window.
- Green LED stays on during the service window.
- Short press requests another immediate reading and resets the service timer.
- Deliberate 10-second hold while already awake erases saved home-Wi-Fi
  provisioning.
- Timer wake and button wake are both enabled before deep sleep.

### Removed

- Risky hold-during-boot Wi-Fi erase behavior.

## Phase 3B - Home Wi-Fi / UDP transport

### Added

- Household Wi-Fi/router/mesh became the normal transport for sensor data.
- XIAO uses directed LAN broadcast for protocol-v3 readings.
- T5 listens on UDP port 42100 and returns application ACK.
- ESP-NOW retained for nearby provisioning rather than whole-house telemetry.
- T5 setup page saves home Wi-Fi and provisions nearby unconfigured sensors.
- XIAO stores received Wi-Fi credentials in NVS.

### T5 fixes

- Removed duplicate setup/product constants accidentally carried over from the
  prior setup-page work.
- Fixed the live-plant source-IP field mismatch.

## Phase 3A - T5 setup portal / centralized naming

### Added

- Local `PlantMonitor-xxxx` setup hotspot.
- Generated setup password stored on the T5.
- Local setup page.
- persistent home-Wi-Fi settings.
- sensor discovery.
- up to 16 persisted plant records.
- rename by stable sensor ID.
- plant names stored centrally on the T5 rather than compiled into sensors.

## Earlier transport proof

### Proven direction

- Direct ESP-NOW sensor-to-T5 readings and T5-to-sensor application ACK were
  proven locally.
- Whole-house fringe behavior showed that direct ESP-NOW was not the desired
  family-proof normal transport.
- The architecture therefore moved normal data to the household Wi-Fi
  infrastructure while retaining ESP-NOW for nearby provisioning.
