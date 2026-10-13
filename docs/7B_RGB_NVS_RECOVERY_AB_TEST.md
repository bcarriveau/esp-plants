# Waveshare 7B — NVS automatic RGB recovery A/B test

**Base branch:** `waveshare-zigbee`  
**Base commit:** `f980518df6670c30512d9bac0c58409be1bef898`  
**Scope:** 7B-only, temporary diagnostic, **not a release fix**.

## What changes

In `firmware/waveshare-hub/src/runtime_flash_guard.cpp`, the constant
`k7bSuppressNvsAutoRecoveryForABTest` is `true` on the **7B**. A successful
`nvs_commit()` still commits data normally, but no longer schedules a new
RGB VSYNC resync on its own. Existing **explicit** requests using
`espplants_runtime_flash::requestDisplayRecovery()` (such as after OTA
worker activity) still queue a resync and retain the existing retry logic.

**Important limitation:** the NVS hook does not identify which subsystem
made the commit; this A/B test disables the automatic recovery after *all*
7B NVS commits, including those occurring during Wi-Fi setup/connection.
This is for diagnosis only, not a permanent Wi-Fi-specific policy.

The original Waveshare 7 retains its existing behavior. No changes to
partitions, settings, saved credentials, persistence format, OTA writer,
Zigbee/H2, display timings, bounce-buffer size, LVGL, or the boot animation.

## Check the test is running

Build the **7B** PlatformIO environment (COM13 configured in the unchanged
`platformio.ini`), flash it, and watch serial at 115200 baud. Early startup
should include:

```
[rgb-ab] 7B TEST: NVS auto-resync OFF; explicit recovery ON
```

Each successful runtime NVS commit will say:

```
[rgb-ab] NVS commit OK; automatic RGB recovery suppressed
```

You might **still** see `[display] RGB VSYNC resync queued` following an
explicit recovery request. That is expected and does not mean the A/B test
failed. Keep collecting `[display-diag] ... max_frame_gap_us=... gaps_over_60ms=...`
while noting when the screen physically bounces.

## Suggested physical comparison

1. Compare with the previous unmodified build where NVS commits logged
   `[flash] runtime NVS commit complete; RGB recovery queued`.
2. With this **test build**, restart the 7B several times with your existing
   Wi-Fi connection and note whether the visible roll/bounce happens at
   `Wi-Fi connected` or during splash.
3. Optionally use **Set Up / Change Wi-Fi** (without erasing known-good
   credentials) and watch for a bounce at hotspot start and after a
   verified connection. Save serial logs covering the moment.
4. Compare the *new* increments in `gaps_over_60ms` per test, not the
   repeatedly printed cumulative count; also check for repeated visible
   roll after an NVS write with resync suppressed.

If visible rolling persists, or the screen gets stuck, return immediately
to the original branch version of `runtime_flash_guard.cpp` and rebuild.
Do **not** use this experimental build as a customer/release firmware or
for an OTA install validation.

This A/B test distinguishes **NVS-triggered resync as a cause** from
**flash or cache contention that occurs even without an automatic resync**.
It cannot prove the root cause by itself and is not physically validated.
