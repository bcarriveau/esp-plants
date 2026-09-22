# ESP PLANTS Waveshare alpha.12 - LVGL transition isolation fix

Base branch: `waveshare-zigbee`

Base commit: `7e8783dd477909315ffbfaf9e1b448aa91c3e838`

## What this changes

The retained alpha.11 diagnostic coredump captured the LVGL task in the default
button style-transition animation path (`trans_anim_cb` -> style lookup -> rounded
mask drawing -> `lv_mem_buf_get`) after rapid navigation caused the application
loop to stop making progress.

Alpha.12 keeps the existing UI, display buffering, diagnostic monitor, Zigbee,
Wi-Fi, persistence, and Phase 2 OTA behavior intact.  It adds a linker wrapper
around `lv_btn_create()` that appends local styles whose only property is a null
`LV_STYLE_TRANSITION` for default, pressed, checked and disabled button states.
The existing colors, sizes, positions, borders, fonts and pressed appearance are
not replaced.

## Version coupling

The Waveshare and H2 build identifiers are both bumped to `0.2.0-alpha.12` so
Phase 2 `Update All` release generation remains version-consistent.  There is no
H2 functional change in this package beyond its build/version identity.

## UI bounds audit

No UI geometry source is replaced.  `main.cpp` and the vendored Waveshare/LVGL
display library are absent from this replacement package, so all alpha.11 object
positions, dimensions, card bounds and the reserved 422..479 bottom navigation
area remain byte-for-byte controlled by the current branch files.

## Validation scope

This package contains complete replacement/new files only.  It is intended to be
copied over the repository root.  Run `tests/test_alpha12_button_transition_guard.py`
after extraction.  A PlatformIO compile is still required before calling the
firmware release-ready if the build toolchain is unavailable in the packaging
environment.

Hardware validation should specifically hammer HOME / ALL SENSORS / PLANT /
SETTINGS rapidly.  Keep the alpha.11 crash-diagnostic instrumentation enabled so
any remaining stall still produces a retained coredump.
