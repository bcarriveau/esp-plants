# Design decisions

This file records architectural decisions so future changes do not accidentally undo the reasons behind the current design.

## D001 - Waveshare and H2 are separate firmware projects

**Decision:** Keep `firmware/waveshare-hub` and `firmware/m5-h2-zigbee` as independent PlatformIO projects inside one root VS Code workspace.

**Reason:** They are different chips with different responsibilities and toolchain/platform requirements.

**Consequence:** They can be built/versioned independently while sharing the PlantLink boundary.

## D002 - H2 owns Zigbee

**Decision:** The M5Stack ESP32-H2 is the Zigbee coordinator and owns network state, permit join, device handling, and ZG-303Z translation.

**Reason:** Zigbee radio/protocol responsibility belongs on the H2 rather than the Waveshare application controller.

**Consequence:** H2 Zigbee persistence must survive normal firmware updates and network reset must remain deliberate.

## D003 - Waveshare owns user-facing state

**Decision:** The Waveshare owns plant names, sensor assignments, display settings, Wi-Fi, UI state, and the update experience.

**Reason:** Human-facing configuration belongs with the application/UI controller rather than the radio coprocessor.

**Consequence:** Normal OTA must preserve that state outside application-image replacement.

## D004 - Zigbee identity is IEEE-64

**Decision:** Persist sensor identity by 64-bit IEEE address, not 16-bit network address.

**Reason:** Zigbee short addresses can change.

**Consequence:** Sensor assignments remain stable across normal Zigbee address churn.

## D005 - PlantLink has one authoritative definition

**Decision:** `shared/plantlink/plantlink.h` is the authoritative core protocol definition.

**Reason:** Duplicate protocol definitions drift.

**Consequence:** `shared/plantlink_protocol.h` is only a compatibility forwarding include; it must not redeclare message IDs, capabilities, or payload limits.

## D006 - Sensor freshness is per boot

**Decision:** Restored sensor identity is not equivalent to a fresh reading.

**Reason:** Sleepy sensors may not report immediately after a reboot, and stale readings must not be presented as current.

**Consequence:** Watering/current-status summaries only use sensors that have actually reported during the current boot.

## D007 - Wi-Fi belongs to the Waveshare

**Decision:** The active Zigbee sensors do not use the household Wi-Fi path.

**Reason:** The H2 provides direct Zigbee communication. Wi-Fi is needed for Waveshare setup/update functions, not normal ZG-303Z telemetry.

**Consequence:** The current product has no Home Assistant/MQTT/Zigbee2MQTT/cloud dependency.

## D008 - OTA is product-specific and A/B

**Decision:** Waveshare OTA uses the ESP PLANTS package/manifest format with A/B app partitions and inactive-slot writes.

**Reason:** Arbitrary raw firmware flashing is not an acceptable customer update path.

**Consequence:** Updates validate product/hardware/build identity, hashes, ESP32-S3 image structure, HTTPS certificates, and only activate the new partition after successful validation.

## D009 - H2 update is coordinated by Waveshare

**Decision:** Release tooling builds H2 first, the manifest carries H2 metadata, and the Waveshare source contains the PlantLink H2 update path before its own app update.

**Reason:** Normal customer updates should eventually avoid separate H2 USB access.

**Consequence:** The coordinated H2-first / Waveshare-second update flow is physically verified on the original Waveshare 7 development hardware. Waveshare and H2 remain independently versioned, so the manifest/version state may require updating one controller, the other, or both.

## D010 - Waveshare 7B shares the application, not the proof status

**Decision:** Keep the 7B as a dedicated hardware target behind board-specific code while sharing the application/UI where practical.

**Reason:** Avoid product/UI forks while isolating hardware differences.

**Consequence:** The original Waveshare 7 remains the proven baseline; 7B runtime support is not labeled proven until physically tested.

## D011 - Logical capacity is independent of LVGL row count

**Decision:** Keep 32 logical plant-sensor slots and 32 logical infrastructure slots as product data capacity, while scrolling pages use fixed reusable LVGL row pools rather than one permanent widget tree per slot.

**Reason:** Hidden LVGL objects still consume allocator memory and style/text state. Preallocating maximum widget capacity created hundreds of unnecessary objects and reduced stability headroom.

**Consequence:** HOME and ALL SENSORS currently use seven physical rows each, and ADVANCED ZIGBEE uses five, while all 32 logical records continue to receive data, participate in sorting/freshness/routing, and remain selectable when scrolled into view. UI virtualization must never become the source of truth for sensor or repeater state.

## D012 - LVGL allocator and display policy changes require measurements

**Decision:** Do not increase `LV_MEM_SIZE`, move the entire LVGL heap to PSRAM, or change the proven RGB tearing/framebuffer/full-refresh policy merely to mask UI pressure. Reduce unnecessary objects/mutations first and use runtime telemetry to justify any later memory/display-policy change.

**Reason:** A previous physical coredump stopped in LVGL's temporary shadow-buffer allocation path while normal ESP heap and PSRAM remained healthy. The alpha.44-alpha.50 work removes known pressure sources and instruments LVGL's own allocator and runtime timing directly.

**Consequence:** The current 128 KB LVGL allocator and display policy remain intentional until long-run telemetry or a new coredump demonstrates a specific need to change them.

## Historical decisions

Earlier T5/XIAO decisions remain represented by their source history and `BASELINE_MANIFEST.md`. They are legacy-product decisions, not rules for new Waveshare/H2 implementation work.
