# Test plan

Use this as the minimum regression plan for the GitHub baseline.

## A. Static / repository checks

- [ ] `python tools/check_protocol_sync.py` passes.
- [ ] no credentials are present in committed source.
- [ ] both projects still have independent `platformio.ini` files.
- [ ] protocol version documented matches source.
- [ ] CHANGELOG updated for user-visible changes.

## B. Build checks

### T5

- [ ] clean PlatformIO build passes.
- [ ] custom board JSON resolves.
- [ ] M5GFX dependency resolves.
- [ ] XPowersLib dependency resolves.
- [ ] no accidental package/toolchain crossover from the XIAO project.

### XIAO

- [ ] clean PlatformIO build passes.
- [ ] pioarduino platform resolves.
- [ ] `seeed_xiao_esp32c6` resolves.
- [ ] no accidental package/toolchain crossover from the T5 project.

## C. T5 hardware smoke test

- [ ] serial boot is normal.
- [ ] PSRAM detected as expected.
- [ ] e-paper initializes.
- [ ] normal display refresh occurs.
- [ ] refresh protection avoids needless rapid updates.
- [ ] physical IO48-labeled function button short press toggles setup mode.
- [ ] physical IO48-labeled function button long hold draws the final OFF screen.
- [ ] final OFF image remains visible after PMU shutdown.
- [ ] physical PWR button wakes the shut-down T5.
- [ ] RST remains an independent reset action.
- [ ] T5 header shows plausible BQ27220 battery percentage while unplugged.
- [ ] USB/external-power state is reported correctly.
- [ ] battery-only reset reaches home Wi-Fi and UDP READY without USB power.
- [ ] reboot with cached plant data immediately shows `LAST KNOWN READING`.
- [ ] next fresh sensor report removes the last-known indication.

## D. T5 setup page

- [ ] `PlantMonitor-xxxx` AP appears.
- [ ] setup password works.
- [ ] `192.168.4.1` loads.
- [ ] home SSID saves.
- [ ] Wi-Fi password saves without being displayed back.
- [ ] setup page survives refresh.
- [ ] discovered sensor appears by stable ID.
- [ ] plant rename persists across T5 restart.
- [ ] more than one sensor can be named independently.
- [ ] no plant name is required in XIAO source.

## E. Provisioning

With an XIAO that has no saved home network:

- [ ] XIAO enters provisioning window.
- [ ] T5 discovers XIAO nearby.
- [ ] **Send Wi-Fi to Sensor** transmits.
- [ ] XIAO accepts configuration.
- [ ] provisioning ACK reaches T5.
- [ ] XIAO restarts/continues into home-Wi-Fi operation.
- [ ] provisioning survives XIAO restart.

## F. Normal UDP path

- [ ] T5 is in normal home-Wi-Fi mode.
- [ ] XIAO obtains an IP.
- [ ] XIAO calculates directed LAN broadcast.
- [ ] reading reaches T5 UDP 42100.
- [ ] T5 validates protocol/checksum.
- [ ] T5 returns ACK with matching sensor ID and sequence.
- [ ] XIAO recognizes ACK.
- [ ] T5 displays/maps the correct plant name.
- [ ] if the initial UDP bind fails, the T5 retries without requiring a reset.
- [ ] if home Wi-Fi drops, UDP is stopped and reconnect is requested.
- [ ] after Wi-Fi recovery, UDP returns to READY and a later sensor packet is accepted.
- [ ] web status page reports Wi-Fi, T5 IP, and UDP listener state accurately.

## G. Adaptive unattended wake

Start from a known successful report.

Stable reading:

- [ ] timer wakes sensor.
- [ ] local measurement occurs.
- [ ] <4 point change does not start Wi-Fi unless another trigger is due.
- [ ] next local sleep matches state policy.

Meaningful change:

- [ ] >=4 point change triggers report.

State change:

- [ ] state boundary crossing triggers report.

Heartbeat:

- [ ] report occurs when heartbeat age reaches policy even if moisture is stable.

## H. Watering watch

Use a plant/probe setup where a clear upward change occurs.

- [ ] >=8 point rise arms watering watch.
- [ ] changed reading reports.
- [ ] first follow-up sleep is 5 minutes.
- [ ] later active-watch sleeps are 10 minutes.
- [ ] two stable follow-ups can end watch.
- [ ] follow-up cap can end watch.
- [ ] after watch ends, normal state interval resumes.

Manual-service watering edge case:

- [ ] wake with top button.
- [ ] green service LED appears.
- [ ] water while service mode is active.
- [ ] 5-second samples detect rise.
- [ ] watering watch remains armed when service mode ends.
- [ ] sensor does **not** choose ordinary 30-minute WET sleep.
- [ ] first post-service follow-up occurs in 5 minutes.

## I. Service mode

- [ ] top button wakes from deep sleep.
- [ ] green LED remains on while manually awake.
- [ ] immediate measurement/report is attempted.
- [ ] automatic reading occurs every 5 seconds.
- [ ] single short press requests another fresh reading.
- [ ] service inactivity timer is extended by interaction.
- [ ] after two minutes of inactivity, green LED goes off and deep sleep begins.

## J. Calibration

- [ ] triple press is recognized without becoming three unwanted manual sends.
- [ ] red dry placement stage is visible.
- [ ] 10 dry samples are captured.
- [ ] green wet placement stage is visible.
- [ ] 10 wet samples are captured.
- [ ] valid dry>wet span saves.
- [ ] two green flashes show success.
- [ ] invalid endpoint order/span is rejected.
- [ ] two red flashes show rejection.
- [ ] saved calibration survives reset/deep sleep.
- [ ] calibration does not erase home-Wi-Fi credentials.
- [ ] calibration resets adaptive percentage history.
- [ ] post-calibration reading uses new endpoints.

## K. Deliberate Wi-Fi reset

- [ ] ordinary boot/button wake does not accidentally erase Wi-Fi.
- [ ] 10-second hold while already awake triggers reset.
- [ ] red indication occurs.
- [ ] network config is erased.
- [ ] sensor returns to provisioning behavior.

## L. Battery / long-run test

T5 hub:

- [ ] BQ27220 state-of-charge is plausible across battery discharge.
- [ ] BQ27220 voltage is plausible.
- [ ] T5 battery-only cold/reset boots continue to receive sensor UDP reports.
- [ ] battery-driven display updates respect refresh thresholds.
- [ ] last-reading cache writes are throttled rather than written every packet.

XIAO sensors:

- [ ] no unexpected rapid wake loop.
- [ ] no persistent Wi-Fi when stable.
- [ ] heartbeat prevents indefinite apparent silence.
- [ ] battery percentage is plausible and changes smoothly.
- [ ] repeated deep sleep does not corrupt configuration.
