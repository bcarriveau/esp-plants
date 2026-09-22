from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
lv = (ROOT/'firmware/waveshare-hub/include/lv_conf.h').read_text()
pio = (ROOT/'firmware/waveshare-hub/platformio.ini').read_text()
guard = (ROOT/'firmware/waveshare-hub/src/lvgl_transition_guard.cpp').read_text()
wv = (ROOT/'firmware/waveshare-hub/include/build_version.h').read_text()
h2 = (ROOT/'firmware/m5-h2-zigbee/include/build_version.h').read_text()
assert '#define LV_THEME_DEFAULT_TRANSITION_TIME 0' in lv
assert '-Wl,--wrap=lv_btn_create' not in pio
assert '__wrap_lv_btn_create' not in guard
assert 'lv_style_set_transition' not in guard
assert '0.2.0-alpha.13' in wv
assert '0.2.0-alpha.13' in h2
assert '-Wl,--wrap=lv_timer_handler' in pio
print('PASS: alpha.13 compile-time LVGL transition hotfix guards')
