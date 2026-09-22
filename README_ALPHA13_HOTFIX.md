# ESP PLANTS Waveshare alpha.13 LVGL transition hotfix

Base branch: `waveshare-zigbee`
Base commit: `a6dba55d23b06a012bbbd9af00de5d87214814eb`

This package replaces the alpha.12 runtime button-transition wrapper that caused
an immediate LoadProhibited boot loop. It disables LVGL default-theme
transitions at compile time via `LV_THEME_DEFAULT_TRANSITION_TIME 0`.

The alpha.11 crash-capture linker wrappers remain enabled. No UI geometry,
PlantLink behavior, sensor logic, Wi-Fi behavior, persistence, partitions, or
display-buffer mode is changed.

The H2 version identity is bumped to alpha.13 only so Update All release
identity remains aligned; there is no H2 behavioral change in this hotfix.

This package was not PlatformIO-compiled or hardware-tested in ChatGPT's
execution environment.
