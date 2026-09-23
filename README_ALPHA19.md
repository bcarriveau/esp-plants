# ESP PLANTS alpha.19 — H2 coordinator-ready fix

Base branch: `waveshare-zigbee`
Base commit: `094bb6b46c820c5a7cc8ccb7802d4fe73b1c99e1`

This is a narrow follow-up to alpha.18.

## Fix

Alpha.18 used `Zigbee.setRebootOpenNetwork(30)`. On an ESP32-H2 coordinator reboot, the Arduino Zigbee library restores the existing network but that reboot-open path can leave `Zigbee.connected()` false. ESP PLANTS therefore reported H2 online while Zigbee remained STARTING and refused Add Repeater.

Alpha.19 keeps Zigbee NVRAM preservation (`Zigbee.begin(&coordinatorConfig, false)`), restores `setRebootOpenNetwork(0)`, waits for the coordinator to become connected, then explicitly opens a 30-second router rejoin window.

No UI geometry or unrelated behavior is changed.

## Install

Copy this ZIP over the repository root on `waveshare-zigbee`, build normally, then run `tools\make-waveshare-release.cmd` if producing the OTA release assets.

Both Waveshare and H2 identities are bumped to `0.2.0-alpha.19` so Update All continues to target matching firmware versions.

## Validation in packaging environment

- Base commit inspected before packaging.
- Current H2 startup anchors verified against branch source.
- Python syntax compilation passed for the replacement pre-build script.
- Transformation behavior tested against the alpha.18 startup snippet: reboot-open becomes 0, NVRAM-preserving begin remains, and post-start 30-second router rejoin window is inserted once.
- No UI files/layouts changed.
- PlatformIO toolchain is not available in this packaging environment, so firmware compilation and hardware testing were not performed here.
