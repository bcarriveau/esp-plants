# Test plan

This is the minimum regression plan for the active Waveshare ESP32-S3 + M5Stack ESP32-H2 + ZG-303Z product line.

## A. Repository/static checks

- [ ] `python -m pytest tests` passes.
- [ ] no documented command references a missing repository tool.
- [ ] `shared/plantlink/plantlink.h` remains the authoritative PlantLink definition.
- [ ] `shared/plantlink_protocol.h` remains only a forwarding compatibility include.
- [ ] Waveshare 7 `upload_port = COM11` remains intact unless deliberately changed by the owner.
- [ ] no source-mutating pre-build injector is in the active build path.
- [ ] no stale release binary is presented as the current H2 release.
- [ ] CHANGELOG/README/security docs do not describe T5/XIAO as the active product.

## B. Build checks

### Waveshare 7

- [ ] clean `waveshare_s3_touch_lcd_7` build passes.
- [ ] USB serial diagnostics build configuration remains intact.
- [ ] A/B partition table resolves.
- [ ] normal developer build does not generate a public release package.

### H2

- [ ] clean `m5_gateway_h2` build passes.
- [ ] Zigbee coordinator configuration resolves.
- [ ] PlantLink and ZG-303Z shared headers resolve.

### Release build

- [ ] `tools\make-waveshare-release.cmd` builds H2 release first.
- [ ] H2 distribution image contains its distribution/build identity.
- [ ] Waveshare release build generates `.plantsota` + manifest.
- [ ] manifest contains the intended H2 asset metadata/hash.
- [ ] old release outputs are not mistaken for current assets.

### Waveshare 7B

- [ ] `waveshare_s3_touch_lcd_7b` compiles when validating source compatibility.
- [ ] do not mark runtime behavior verified from compilation alone.

## C. PlantLink / H2 link

- [ ] H2 link comes up after normal boot.
- [ ] Hello/heartbeat capability reporting is consistent.
- [ ] Zigbee network state reaches the Waveshare.
- [ ] Sensor reports decode with stable IEEE-64 identity.
- [ ] infrastructure/router records do not replace plant-sensor identity.
- [ ] UART reconnect does not erase Zigbee state.

## D. Zigbee / ZG-303Z

- [ ] H2 coordinator starts normally.
- [ ] permit join is user-initiated and closes as expected.
- [ ] ZG-303Z can commission.
- [ ] soil moisture reports.
- [ ] temperature reports.
- [ ] air humidity reports.
- [ ] battery reports.
- [ ] sleepy sensor remains joined without requiring constant application chatter.
- [ ] reboot does not silently factory-reset the Zigbee network.

## E. Waveshare sensor freshness

After Waveshare reboot with registered sensors:

- [ ] known names/identities may appear immediately.
- [ ] sensors not heard from during this boot show WAITING / NOT YET REPORTED.
- [ ] pre-reboot values are not labeled current.
- [ ] `WHO NEEDS WATER?` excludes not-yet-reported sensors.
- [ ] the first actual moisture report makes that sensor current.
- [ ] temperature/humidity/battery/LQI-only traffic does not falsely mark moisture current.

## F. UI regression

For any UI-affecting change, verify 800x480 geometry mathematically before hardware screenshots.

- [ ] header remains in y=0..65.
- [ ] content remains in y=66..421.
- [ ] bottom navigation beginning at y=422 is untouched unless explicitly changed.
- [ ] children remain inside parent/card bounds including padding.
- [ ] labels and controls do not overlap.
- [ ] text wrapping fits allocated height.
- [ ] unrelated screens remain unchanged.

Repository-hygiene-only changes should not alter UI source.

## G. Wi-Fi

- [ ] nearby SSID scan/selection works.
- [ ] QR-assisted setup renders and is usable.
- [ ] CONNECTING / SUCCESS / FAILED states are clear.
- [ ] failed replacement setup returns to an editable form.
- [ ] failed setup does not overwrite known-good credentials.
- [ ] Disconnect Wi-Fi disconnects without erasing credentials.
- [ ] automatic reconnect remains suppressed until reconnect/reboot after Disconnect.
- [ ] Forget Wi-Fi requires confirmation and erases saved credentials.

## H. Waveshare OTA

- [ ] updater uses certificate-verified HTTPS.
- [ ] package/product/hardware/build identity is validated.
- [ ] package SHA-256 is validated.
- [ ] firmware SHA-256 is validated.
- [ ] ESP32-S3 image validation passes only for the intended image.
- [ ] download/redirect bounds are enforced.
- [ ] write target is the inactive OTA slot.
- [ ] boot partition changes only after successful final validation.
- [ ] failed update leaves the current bootable app intact.
- [ ] plant names, device name, assignments, Wi-Fi, settings, and persistent plant data survive.

## I. H2-through-Waveshare OTA

Current source implements this path, but it is not yet claimed physically verified.

Physical verification should include:

- [ ] current Waveshare downloads/verifies the intended H2 asset.
- [ ] H2 update begins only with compatible metadata.
- [ ] chunks transfer over PlantLink and offsets/status are enforced.
- [ ] H2 writes only its inactive app slot.
- [ ] H2 validates image/build/distribution identity/hash.
- [ ] H2 activates only after successful validation.
- [ ] H2 reboots and reports the expected build.
- [ ] Waveshare install proceeds only after successful H2 update.
- [ ] failure in H2 stage prevents the Waveshare stage.
- [ ] Zigbee network survives.
- [ ] user data survives.

Do not mark this section verified until it is exercised on actual hardware.

## J. Long-run / recovery

- [ ] overnight H2/Zigbee serial monitoring shows no unexpected coordinator loss.
- [ ] Waveshare reboot recovers PlantLink.
- [ ] H2 reboot recovers PlantLink and Zigbee state.
- [ ] router/AP outage does not break local Zigbee monitoring.
- [ ] repeated UI navigation does not cause crash/stall regressions.
- [ ] repeated update checks do not leak memory or destabilize normal operation.

## Legacy tests

T5/XIAO-specific hardware checks remain useful only when maintaining that legacy product line. They are not the release gate for the active Waveshare/H2 product.
