from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LV_CONF = ROOT / "firmware" / "waveshare-hub" / "include" / "lv_conf.h"
PIO = ROOT / "firmware" / "waveshare-hub" / "platformio.ini"
GUARD = ROOT / "firmware" / "waveshare-hub" / "src" / "lvgl_transition_guard.cpp"


def test_lvgl_transitions_remain_disabled_at_compile_time():
    lv = LV_CONF.read_text(encoding="utf-8")
    assert "#define LV_THEME_DEFAULT_TRANSITION_TIME 0" in lv


def test_obsolete_button_wrapper_is_not_in_active_build():
    pio = PIO.read_text(encoding="utf-8")
    guard = GUARD.read_text(encoding="utf-8")

    assert "-Wl,--wrap=lv_btn_create" not in pio
    assert "__wrap_lv_btn_create" not in guard
    assert "lv_style_set_transition" not in guard


def test_current_diagnostic_wrappers_are_preserved():
    pio = PIO.read_text(encoding="utf-8")
    for flag in (
        "-Wl,--wrap=_Z4loopv",
        "-Wl,--wrap=lvgl_port_lock",
        "-Wl,--wrap=lvgl_port_unlock",
        "-Wl,--wrap=lv_timer_handler",
        "-Wl,--wrap=_ZN16espplants_update7serviceEv",
    ):
        assert flag in pio
