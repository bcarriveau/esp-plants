"""7B-only bounded framebuffer-present telemetry, not framebuffer behavior changes."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp'
HEADER = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.h'
INI = ROOT / 'firmware/waveshare-hub/platformio.ini'
SRC = DRIVER.read_text()


def test_7b_only_presentation_trace_does_not_modify_buffer_switching():
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 15)' in HEADER.read_text()
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 10)' in HEADER.read_text()
    assert '#define LVGL_PORT_AVOID_TEARING_MODE (2)' in HEADER.read_text()
    assert 'post:scripts/use_7b_vsync_sdk.py' in INI.read_text()
    assert 'upload_port = COM11' in INI.read_text()
    assert 'upload_port = COM13' in INI.read_text()
    assert 'static constexpr uint8_t k7bPresentTraceCapacity = 8;' in SRC
    assert 'record7bFramePresentation(' in SRC
    assert 'report7bFramePresentations();' in SRC
    assert '[rgb-present-diag] submit_us=' in SRC
    assert 'const uint32_t last_raw_us = seven_b_diag_raw_last_us;' in SRC
    assert 'const uint32_t last_finish_us = seven_b_diag_last_frame_us;' in SRC
    assert 'lcd->drawBitmap(offsetx1, offsety1, offsetx2 - offsetx1 + 1, offsety2 - offsety1 + 1, (const uint8_t *)color_map);\n\n    lvgl_port_rgb_next_buf = color_map;' in SRC
    assert 'lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf;' in SRC
    assert 'lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf;' in SRC
    assert 'callbacks.on_vsync = on7bRawVsync;' in SRC
    assert 'callbacks.on_bounce_frame_finish = on7bBounceFrameFinish;' in SRC
    assert 'esp_lcd_rgb_panel_restart(' in SRC  # explicit recovery still supported
    block = SRC.split('// Trace the *submission*', 1)[1].split('static bool seven_b_set_backlight_duty', 1)[0]
    assert 'malloc(' not in block
    assert 'new ' not in block
    assert 'lv_' not in block
    assert 'esp_lcd_rgb_panel_restart(' not in block
    report = block.split('static void report7bFramePresentations()', 1)[1]
    assert 'portEXIT_CRITICAL(&seven_b_present_mux);\n    if (count' in report
    assert 'portENTER_CRITICAL(&seven_b_present_mux);' in report


def test_bounded_trace_host_compiles_and_retains_newest_records_without_serial_under_lock():
    compiler = shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        import pytest
        pytest.skip('Host C++ compiler unavailable')
    code = 'struct SevenBPresentTrace {' + SRC.split('struct SevenBPresentTrace {', 1)[1].split('\n#endif\n\nstatic bool seven_b_set_backlight_duty', 1)[0]
    stub = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
static int critical = 0;
#define portENTER_CRITICAL(m) do { assert(critical == 0); ++critical; (void)(m); } while(0)
#define portEXIT_CRITICAL(m) do { assert(critical == 1); --critical; (void)(m); } while(0)
static std::vector<std::string> output;
struct FakeSerial {
    template<typename... Args> void printf(const char* format, Args... args) {
        assert(critical == 0);
        char out[256];
        snprintf(out, sizeof(out), format, args...);
        output.emplace_back(out);
    }
} Serial;
'''
    main = r'''
int main() {
    for (uint32_t i = 1; i <= 10; ++i)
        record7bFramePresentation(i * 1000, i, i * 3, i * 4, (uintptr_t)(0x3c400000 + i));
    report7bFramePresentations();
    assert(critical == 0);
    assert(output.size() == 9);
    assert(output[0].find("recent=8 overwritten=2") != std::string::npos);
    assert(output[1].find("submit_us=3000") != std::string::npos);
    assert(output[8].find("submit_us=10000") != std::string::npos);
    output.clear();
    report7bFramePresentations();
    assert(output.empty());
    record7bFramePresentation(17000, 5, 9, 11, 0x1000);
    report7bFramePresentations();
    assert(output.size() == 2);
    assert(output[0].find("recent=1 overwritten=0") != std::string::npos);
    assert(output[1].find("draw_us=5 raw_age_us=9 finish_age_us=11") != std::string::npos);
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / 'main.cpp'
        b = Path(td) / 'probe'
        p.write_text(stub + '\n' + code + '\n' + main)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        str(p), '-o', str(b)], check=True, capture_output=True, text=True)
        subprocess.run([str(b)], check=True, capture_output=True, text=True)
