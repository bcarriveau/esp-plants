# ESP PLANTS — Aeotec Range Extender Zi pairing fix

Base branch: `waveshare-zigbee`
Base commit: `72ac4d10550ff13c9abcd1a04fc5e593e6c2ca43`

## What this changes

This is an H2-only replacement package. It does not move or redesign any Waveshare UI.

The coordinator previously honored the Waveshare's immediate `PermitJoin(0)` as soon as a Zigbee router first appeared in the neighbor table. A router can appear in the neighbor table before it has finished its Zigbee join/announce process. That can close the Zigbee network too early; the Aeotec can then fall back to its unpaired slow-breathing LED while ESP PLANTS has already recorded it and later shows it offline.

The replacement H2 `main.cpp` adds a 15-second router settle guard:
- detects a router newly appearing or reappearing while permit-join is active;
- defers an immediate close request long enough for the router to finish joining;
- closes permit-join automatically after the settle window;
- keeps manual USB `c` close immediate;
- does not alter plant-sensor decoding or persistence.

H2 build identity is bumped from `0.2.0-alpha.13` to `0.2.0-alpha.14`.

## Aeotec LED reference

Aeotec documents:
- fading in/out = powered but not joined;
- rapid flashing = attempting to join;
- solid on/off = joined.

Before retrying, factory-reset the Range Extender Zi by holding its action button for about 10 seconds until reset, then verify the LED is fading in/out. Start Add Repeater and tap the Aeotec action button once.

## Validation status

Source-level validation was performed on this package. It was **not compiled in this environment** because PlatformIO/ESP32 toolchains are not installed here, and it was **not hardware-tested**.

After flashing the H2, serial should show a sequence similar to:
- `permit join open for 120 seconds`
- `router join activity ... protecting settle window`
- `permit join close deferred 15000 ms for router settle`
- `deferred permit join close completed after router settle`

The Aeotec LED should finish in solid ON or OFF, not slow breathing.
