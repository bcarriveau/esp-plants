"""7B-only raw VSYNC vs PSRAM bounce-frame probe contract and host callback test."""
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp'
HEADER = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.h'
BOARD = ROOT / 'firmware/waveshare-hub/include/waveshare_panel_board_7b.h'
INI = ROOT / 'firmware/waveshare-hub/platformio.ini'
SRC = CPP.read_text()


def test_probe_is_7b_only_and_compares_native_events():
    assert '#if defined(ESP_PLANTS_WAVESHARE_7B) && !defined(ESP_PLANTS_DISTRIBUTION_BUILD)\n// The native LCD ISR emits raw VSYNC' in SRC
    assert 'callbacks.on_vsync = on7bRawVsync;' in SRC
    assert 'callbacks.on_bounce_frame_finish = on7bBounceFrameFinish;' in SRC
    assert 'esp_lcd_rgb_panel_register_event_callbacks(' in SRC
    assert 'return onRgbVsyncCallback(user_ctx);' in SRC
    assert 'lcd->attachRefreshFinishCallback(onRgbVsyncCallback, (void *)lvgl_task_handle);' in SRC
    assert 'err == ESP_OK ? "enabled" : "UNAVAILABLE (original callback retained)"' in SRC
    assert '[rgb-path-diag] dt=%lums raw_vsync=%lu bounce_frame_finish=%lu ' in SRC
    assert '[rgb-gap-diag] callback_at_us=' in SRC
    assert SRC.count('esp_lcd_rgb_panel_register_event_callbacks(') == 1
    assert '#if defined(ESP_PLANTS_WAVESHARE_7B) && !defined(ESP_PLANTS_DISTRIBUTION_BUILD)\n    // The ESP_Panel wrapper' in SRC


def test_no_changes_to_config_or_ui_behavior():
    assert '#define LVGL_PORT_AVOID_TEARING_MODE (2)' in HEADER.read_text()
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 20)' in HEADER.read_text()
    assert '#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)' in BOARD.read_text()
    ini = INI.read_text()
    assert 'upload_port = COM11' in ini
    assert 'upload_port = COM13' in ini
    assert 'post:scripts/use_7b_vsync_sdk.py' in ini
    callback = SRC.split('IRAM_ATTR static bool on7bRawVsync', 1)[1].split('IRAM_ATTR bool onRefreshFinishCallback', 1)[0]
    assert 'Serial.' not in callback
    assert 'esp_lcd_rgb_panel_restart(' not in callback
    assert 'lv_' not in callback


def test_raw_vsync_callback_host_compiles_and_tracks_long_gaps():
    compiler = shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        import pytest
        pytest.skip('C++ compiler unavailable')
    callback = 'IRAM_ATTR static bool on7bRawVsync' + SRC.split('IRAM_ATTR static bool on7bRawVsync', 1)[1].split('// Preserve the original LVGL', 1)[0]
    adapter = 'IRAM_ATTR static bool on7bBounceFrameFinish' + SRC.split('IRAM_ATTR static bool on7bBounceFrameFinish', 1)[1].split('#endif', 1)[0]
    stub = r'''#include <stdint.h>
#include <assert.h>
#define IRAM_ATTR
using esp_lcd_panel_handle_t = void*;
struct esp_lcd_rgb_panel_event_data_t {};
static uint32_t mock_now_us = 0;
static uint64_t esp_timer_get_time() { return mock_now_us; }
static volatile uint32_t seven_b_diag_raw_vsync_count = 0;
static volatile uint32_t seven_b_diag_raw_last_us = 0;
static volatile uint32_t seven_b_diag_raw_max_gap_us = 0;
static volatile uint32_t seven_b_diag_raw_gaps_over_60ms = 0;
static volatile uint32_t seven_b_diag_raw_long_gap_at_us = 0;
static volatile uint32_t seven_b_diag_raw_long_gap_us = 0;
static uint32_t forwarded_bounce_count = 0;
static void* forwarded_ctx = nullptr;
static bool onRgbVsyncCallback(void *ctx) { ++forwarded_bounce_count; forwarded_ctx = ctx; return false; }
'''
    main = r'''int main() {
    mock_now_us = 100000; on7bRawVsync(nullptr, nullptr, nullptr);
    mock_now_us = 138000; on7bRawVsync(nullptr, nullptr, nullptr);
    mock_now_us = 214000; on7bRawVsync(nullptr, nullptr, nullptr);
    assert(seven_b_diag_raw_vsync_count == 3);
    assert(seven_b_diag_raw_max_gap_us == 76000);
    assert(seven_b_diag_raw_gaps_over_60ms == 1);
    assert(seven_b_diag_raw_long_gap_at_us == 214000);
    assert(seven_b_diag_raw_long_gap_us == 76000);
    void *ctx = (void*)0x1234;
    assert(!on7bBounceFrameFinish(nullptr, nullptr, ctx));
    assert(forwarded_bounce_count == 1 && forwarded_ctx == ctx);
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'probe.cpp'
        binary = Path(tmp) / 'probe'
        path.write_text(stub + '\n' + callback + '\n' + adapter + '\n' + main)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-o', str(binary), str(path)], check=True, capture_output=True, text=True)
        subprocess.run([str(binary)], check=True, capture_output=True, text=True)
