# ESP PLANTS 7B VSYNC SDK overlay

Generated locally by `scripts/build_7b_vsync_sdk.ps1` / `.sh`.

The generated files are intentionally not committed:

- `common/libesp_lcd_vsync.a`
- `qio_opi/libesp_hw_support_vsync.a`
- `qio_opi/include/sdkconfig.h`
- `manifest.json`

The 7B PlatformIO environment refuses to build unless all four are present,
their SHA-256 values match the manifest, and the generated sdkconfig contains
the real `LCD_RGB_RESTART_IN_VSYNC` plus its `GDMA_CTRL_FUNC_IN_IRAM` Kconfig
dependency. The original 800x480 Waveshare 7 does not use this overlay.
