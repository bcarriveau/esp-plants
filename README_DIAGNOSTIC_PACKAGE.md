# ESP PLANTS Waveshare crash diagnostics package

Base branch: `waveshare-zigbee`
Base commit: `459951ba9ad7bb7f4323386857da878cdc3a7e1f`
Firmware identity remains `0.2.0-alpha.11` so this temporary diagnostic build does **not** force an unrelated H2 firmware/version update.
Diagnostic identity printed on serial: `ESPPLANTS-WAVESHARE-alpha.11-diag1`.

## What this changes

- Adds a low-priority diagnostic monitor task on Core 0.
- Observes the existing Arduino main loop, `espplants_update::service()`, LVGL mutex lock/unlock, and `lv_timer_handler()` using linker wrappers.
- Prints reset reason, previous RTC breadcrumb, internal heap/PSRAM telemetry, and existing flash coredump state at boot.
- Warns after a 3-second main-loop stall and again after 8 seconds.
- If the main loop remains stalled for 15 seconds outside `UPDATE_SERVICE`, intentionally calls `abort()` so the ESP-IDF panic handler writes a post-mortem dump containing task stacks to the **existing** `coredump` partition.
- Enables PlatformIO's `esp32_exception_decoder` monitor filter for live panic/backtrace decoding.
- Adds `tools/read-waveshare-crashdump.ps1` to read the existing `0xff0000 / 0x10000` coredump partition and decode it against the exact `firmware.elf` when `esp-coredump` is available.

No UI geometry, sensor logic, Zigbee logic, persistence layout, OTA partition layout, or Waveshare LVGL/display library file is replaced by this package.

## Install/test

1. Copy the replacement files over the repository at the base commit (or later commit only if you first confirm no conflicts in these exact paths).
2. Build and USB-flash the normal Waveshare environment: `waveshare_s3_touch_lcd_7`.
3. Keep VS Code/PlatformIO serial monitor open at 115200 and reproduce the rapid-navigation freeze.
4. If the main loop hangs while Core 0 is still alive, serial should first show `[diag] STALL...`; at 15 seconds the diagnostic monitor forces a panic and reboot so the flash coredump is retained.
5. Do **not** clean `.pio` after the crash. The coredump must be decoded using the exact ELF that produced it.
6. Close the serial monitor, then run from the repo root:

   `powershell -ExecutionPolicy Bypass -File tools\read-waveshare-crashdump.ps1 -Port COMx`

7. Keep/upload the generated raw `.bin`, decoded `.txt` if produced, the matching `firmware.elf`, and the serial log.

## Important diagnostic behavior

This is intentionally a diagnostic build. A prolonged main-loop deadlock is converted into an `abort()` after 15 seconds so a post-mortem can be collected. The wrapper marks `UPDATE_SERVICE` as exempt from the automatic abort so a long update-service call is not deliberately killed.

This package has not been hardware-tested here. PlatformIO/ESP32 toolchains are not installed in the generation environment, so firmware compilation must be performed in the normal VS Code/PlatformIO workspace before flashing.
