# ESP PLANTS alpha.18 — repeater pairing + reboot rejoin fix

Base branch: `waveshare-zigbee`
Base commit: `13eb131e408616d4b8e797c0500224e04a891330`
Target: `0.2.0-alpha.18`

## Changes

1. **First Add Repeater attempt**
   - Keeps the locally requested 120-second pairing window from being overwritten by a queued, pre-request H2 `permit_join=0` status.
   - The guard lasts at most 3 seconds and clears immediately when the H2 reports a positive join countdown.
   - A genuine failure still falls back to zero after the guard expires.

2. **Repeater after coordinator reboot**
   - Makes `erase_nvs=false` explicit when the H2 Zigbee coordinator starts.
   - Opens Zigbee steering for 30 seconds after a non-factory-new coordinator reboot so an already-powered router/repeater has a clean chance to re-establish/rejoin.
   - Does not factory-reset Zigbee and does not erase `nvs`, `zb_storage`, or `zb_fct`.

3. **Version**
   - Waveshare and H2 identities are both `0.2.0-alpha.18` so Phase 2 Update All derives the matching H2 release asset correctly.

## Use

Copy the contents of this ZIP over the repository root on `waveshare-zigbee`.

Then:

1. Run normal local builds for Waveshare and H2.
2. Run `tools\make-waveshare-release.cmd`.
3. Confirm the alpha.18 release directory contains:
   - `esp-plants-waveshare-0.2.0-alpha.18.plantsota`
   - `esp-plants-waveshare.manifest.json`
   - `esp-plants-h2-0.2.0-alpha.18.bin`
   - `esp-plants-h2-0.2.0-alpha.18.bin.sha256`
4. Publish those four files in the alpha.18 GitHub release.
5. Flash the alpha.18 Waveshare locally once, then use **Update All** to exercise H2-over-PlantLink OTA.

## Expected test

- First press of **Add Repeater** should remain in `PAIRING...` and count down instead of immediately flashing `120` then `0 / TRY AGAIN`.
- Once the Aeotec is paired, reboot/power-cycle ESP PLANTS. The saved repeater may initially display OFFLINE while the H2 restarts, but it should return ONLINE and the Aeotec should not remain in its soft-pulsing unjoined state.

No UI geometry/layout is changed by this package.

## Validation status

The package contains complete replacement files plus the same style of fail-fast PlatformIO pre-build source injection already used by Phase 2. Python syntax and injection-anchor simulations were validated in the packaging environment. PlatformIO is not installed here, so firmware compilation and physical hardware verification are still required on your PC/hardware.
