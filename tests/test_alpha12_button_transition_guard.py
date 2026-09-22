from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIO = ROOT / "firmware" / "waveshare-hub" / "platformio.ini"
GUARD = ROOT / "firmware" / "waveshare-hub" / "src" / "lvgl_transition_guard.cpp"
WAVE_VERSION = ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h"
H2_VERSION = ROOT / "firmware" / "m5-h2-zigbee" / "include" / "build_version.h"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    pio = PIO.read_text(encoding="utf-8")
    guard = GUARD.read_text(encoding="utf-8")
    wave = WAVE_VERSION.read_text(encoding="utf-8")
    h2 = H2_VERSION.read_text(encoding="utf-8")

    require('-Wl,--wrap=lv_btn_create' in pio,
            'lv_btn_create linker wrapper is missing')

    # Preserve the alpha.11 diagnostic wrappers while this fix is tested.
    for flag in (
        '-Wl,--wrap=_Z4loopv',
        '-Wl,--wrap=lvgl_port_lock',
        '-Wl,--wrap=lvgl_port_unlock',
        '-Wl,--wrap=lv_timer_handler',
        '-Wl,--wrap=_ZN16espplants_update7serviceEv',
    ):
        require(flag in pio, f'diagnostic wrapper was lost: {flag}')

    require('__wrap_lv_btn_create' in guard, 'button wrapper implementation missing')
    require('__real_lv_btn_create' in guard, 'button wrapper does not call original LVGL creator')
    require(guard.count('lv_style_set_transition') >= 4,
            'expected no-transition styles for default/pressed/checked/disabled states')
    require('LV_STATE_PRESSED' in guard,
            'pressed-state transition override is missing')
    require('LV_STYLE_TRANSITION' in guard,
            'source comment/guard does not document transition-only scope')

    # This fix must not replace the locked application layout or display driver.
    require(not (ROOT / 'firmware' / 'waveshare-hub' / 'src' / 'main.cpp').exists(),
            'alpha.12 package must not replace locked main.cpp/UI layout')
    require(not (ROOT / 'firmware' / 'waveshare-hub' / 'lib').exists(),
            'alpha.12 package must not replace the display/LVGL library')

    require('0.2.0-alpha.12' in wave, 'Waveshare version was not bumped to alpha.12')
    require('0.2.0-alpha.12' in h2,
            'H2 version must stay coupled for Phase 2 Update All release assets')

    print('PASS: alpha.12 button transition guard source/package checks')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
