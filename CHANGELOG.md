# Changelog

This project uses this file for user-visible behavior, protocol, hardware, and
architecture changes. Do not rely on commit messages alone for project history.

## [Unreleased]

### Waveshare / H2 executable bring-up scaffold

#### Added

- Fixed LVGL 8.3 button-matrix keyboard map declarations so the custom Waveshare plant-name keyboard compiles with lv_btnmatrix_set_map().

- Reworked the on-device plant-name editor with a purpose-built touch keyboard: explicit SPACE, SAVE, CANCEL, DEL, and ABC/abc controls with larger buttons and a stronger full-screen layout.

- Added persistent Waveshare plant slots keyed by each Zigbee sensor's IEEE-64 address. Plant numbering and user names now survive reboots and normal firmware updates instead of depending on report order after boot.
- Added a three-page Waveshare touch UI with HOME, PLANT, and SETTINGS navigation. The HA-inspired overview stays moisture-focused while detailed radio/system information moves off the main screen.
- Added on-device plant renaming with the LVGL touch keyboard. Names are stored in Waveshare NVS and remain bound to the same IEEE sensor identity.
- Known plants now appear immediately after reboot while waiting for their sleepy Zigbee sensors to check in; live values fill back in as reports arrive.
- Moved Zigbee pairing, H2/Zigbee diagnostics, registered-plant count, and the persistent F/C control to the SETTINGS page.

- Replaced the temporary Waveshare engineering table with an HA-inspired 800x480 plant dashboard: playful plant status text, a large selected-plant card, soil bar, temperature, air RH, battery, signal/last-update details, water-warning badge, and a scrollable all-plants list.
- Added a persistent F/C display toggle on the Waveshare; PlantLink continues to transport normalized centi-degrees C internally.
- Fixed Waveshare native USB Serial routing by explicitly enabling ARDUINO_USB_MODE=1 and ARDUINO_USB_CDC_ON_BOOT=1, matching the proven ESP32-S3 native USB console pattern while leaving UART0 GPIO43/44 dedicated to PlantLink.
- Physical hardware now verifies the complete ZG-303Z -> H2 Zigbee -> PlantLink UART -> Waveshare data path; the Waveshare receives H2 status and live sensor data.

- Corrected legacy HOBEIAN ZG-303Z DP106 handling: after DP3/5/15 identify the legacy map, DP106 is normalized as the dry/water-shortage warning and is exposed in H2 console status. Alternate ZG-303Z mappings are not globally remapped.

- Corrected the hardware-verified HOBEIAN ZG-303Z `0x0405` behavior: the device mirrors soil moisture through the standard humidity-cluster encoding, while DP109 is the actual air-humidity source. `0x0405` is now a soil fallback and can no longer create a false RH reading during startup.

- Registered Tuya `0xEF00` as a custom client cluster on the H2 gateway endpoint so stock ZG-303Z Tuya commands are accepted by the Zigbee stack instead of producing repeated missing-client-cluster errors.
- Made ZG-303Z Tuya DP109 humidity authoritative after first observation because physical hardware emits a bogus standard `0x0405` humidity value of zero that can otherwise overwrite the valid RH reading.
- Documented physical ZG-303Z captures: DP5 temperature, DP3 soil moisture, DP15 battery, DP109 humidity, standard temperature/battery cross-checks, and the observed DP106 boolean behavior.

- Fixed H2 USB console routing by explicitly mapping Arduino Serial to the ESP32-H2 hardware USB Serial/JTAG interface (ARDUINO_USB_MODE=1, ARDUINO_USB_CDC_ON_BOOT=1); enabled monitor echo for interactive console testing.

- Added a development USB serial console on the H2: p opens Zigbee joining for 120 seconds, c closes joining, s prints coordinator/sensor status, and h/? prints help. This allows H2 + ZG-303Z testing before the Waveshare hub is connected.

- Fixed ESP32-H2 APS capture compatibility with the Arduino-ESP32 3.3.7 Zigbee API, which exposes LQI but not an APS RSSI member; unavailable RSSI is now carried as -128 instead of referencing a nonexistent field.

- Waveshare PlatformIO project based on the hardware-proven ESP Aircraft Radar
  display/touch stack rather than introducing a new LCD framework generation.
- Native USB CDC debug on the Waveshare while dedicating UART0 GPIO43/44 to
  PlantLink through the physical UART2 connector.
- M5Stack Gateway H2 PlatformIO project for its 2 MB ESP32-H2 module in Zigbee
  coordinator/router mode, using the M5Stack-recommended Zigbee partition map.
- Framed PlantLink v1 COBS/CRC transport implementation and normalized
  `SensorReport` payload.
- H2 raw APS diagnostics, IEEE-64 sensor identity, standard
  temperature/humidity/battery decoding, and initial stock ZG-303Z Tuya DP
  decoding with unknown-DP visibility.
- Waveshare bring-up UI with H2 link state, Zigbee state, sensor count, latest
  event, and a 120-second `ADD SENSOR` permit-join action.
- Root VS Code tasks for Waveshare/H2 build, upload, and monitor without opening
  separate workspaces.
- Removed the unwanted GitHub Actions firmware-build workflow; builds remain local
  in the single VS Code workspace.
- Native Waveshare USB-C is the normal upload/Serial Monitor connection; the
  CH343 UART USB-C is not part of the normal ESP PLANTS development path.
- Hardware wiring notes using Waveshare UART2 for data and the selectable 5 V
  I2C header supply for H2 power.

#### Verification status

- Shared PlantLink framing/CRC/COBS and ZG-303Z parser logic were compiled and
  exercised with host-side C++ tests.
- Both partition tables were structurally checked for overlap and exact flash
  bounds.
- The full embedded projects are prepared/structurally reviewed but have not
  been PlatformIO-compiled in this environment because PlatformIO/toolchain
  downloads are unavailable here.
- H2 USB, native coordinator startup, persistent Zigbee network, permit join, ZG-303Z commissioning, IEEE identity, raw APS capture, EF00 decoding, temperature, soil moisture, air humidity, and battery reporting have been verified on physical hardware. Waveshare-to-H2 UART/PlantLink runtime wiring and live sensor-data delivery are verified on physical hardware.

### Waveshare / Zigbee development branch

#### Added

- Initial isolated project scaffold for a Waveshare ESP32-S3 7-inch hub and
  M5Stack ESP32-H2 Zigbee coprocessor while retaining the existing T5/XIAO
  baseline unchanged.
- PlantLink v1 architecture for a framed, checksummed UART boundary between the
  H2 Zigbee side and the Waveshare application side.
- Repository rules establishing IEEE-64 Zigbee identity, separate persistent
  user/Zigbee state, and a single root VS Code workspace.

#### Architecture

- The H2 is responsible for the Zigbee coordinator/network, pairing, device
  handling, and ZG-303Z Zigbee/Tuya translation.
- The Waveshare S3 is responsible for UI, plant identity/names, user settings,
  history, Wi-Fi, and the update experience.
- Home Assistant, MQTT, Zigbee2MQTT, and cloud services are not part of the
  required runtime path.

#### Verification status

- Architecture/scaffold only. No Waveshare or H2 firmware build or hardware
  verification is claimed by this entry.

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

- Changed the HOME right-side plant list into an in-place selector: tapping a plant updates the large HOME card without navigating away; tapping the large card or the PLANT tab still opens full plant details.

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
