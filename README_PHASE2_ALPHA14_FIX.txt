ESP PLANTS Phase 2 H2 release identity fix
Base commit: 72ac4d10550ff13c9abcd1a04fc5e593e6c2ca43
Target: 0.2.0-alpha.14

The H2 distribution marker was previously only used as a strstr() search needle.
An optimized build can eliminate that literal from firmware.bin, causing the
release packager to correctly reject the H2 image.

This replacement retains a real referenced identity block containing:
  ESP-PLANTS-H2-DISTRIBUTION-BUILD
  ESPPLANTS-H2-0.2.0-alpha.14

Apply this ZIP over the repo root, then run the existing combined release build:
  tools\make-waveshare-release.cmd

That script builds m5_gateway_h2_release FIRST, then builds/packages the Waveshare
release. Do not build only waveshare_s3_touch_lcd_7_release for Phase 2.

Expected release assets:
  release\esp-plants-waveshare-0.2.0-alpha.14.plantsota
  release\esp-plants-waveshare.manifest.json
  release\esp-plants-h2-0.2.0-alpha.14.bin
  release\esp-plants-h2-0.2.0-alpha.14.bin.sha256

No UI, Zigbee behavior, Wi-Fi, persistence schema, or display layout changes.
Not compiled or hardware-tested in this environment.
