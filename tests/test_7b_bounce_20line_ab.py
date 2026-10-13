"""7B RGB bounce-buffer rollback regression test; no runtime shims or injectors."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.h'
CPP = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp'
BOARD = ROOT / 'firmware/waveshare-hub/include/waveshare_panel_board_7b.h'
INI = ROOT / 'firmware/waveshare-hub/platformio.ini'


def test_effective_runtime_path_uses_header_macro():
    header = HEADER.read_text()
    driver = CPP.read_text()
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 15)' in header
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 10)' in header
    assert 'rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);' in driver
    assert 'rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);' in driver
    assert '#define LVGL_PORT_AVOID_TEARING_MODE (2)' in header
    assert 'callbacks.on_vsync = on7bRawVsync;' in driver
    assert 'callbacks.on_bounce_frame_finish = on7bBounceFrameFinish;' in driver
    assert '#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)' in BOARD.read_text()
    ini = INI.read_text()
    assert 'upload_port = COM11' in ini
    assert 'upload_port = COM13' in ini
    assert 'post:scripts/use_7b_vsync_sdk.py' in ini


def test_rollback_restores_20k_internal_sram():
    # Driver allocates TWO RGB565 bounce buffers, even with THREE PSRAM framebuffers.
    assert 1024 * 20 * 2 * 2 == 81920
    assert 1024 * 15 * 2 * 2 == 61440
    assert (1024 * 20 * 4) - (1024 * 15 * 4) == 20480
    assert 600 % (2 * 15) == 0


def test_compiler_expands_7b_and_original_7_macros():
    compiler = shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        import pytest
        pytest.skip('No host C++ compiler')
    with tempfile.TemporaryDirectory() as td:
        stub = Path(td)
        (stub / 'Arduino.h').write_text('#pragma once\n#include <stdint.h>\n')
        (stub / 'lvgl.h').write_text('#pragma once\n')
        (stub / 'ESP_Panel_Library.h').write_text('#pragma once\nclass ESP_PanelLcd {};\nclass ESP_PanelTouch {};\n')
        for board_7b, distribution in [(True, False), (True, True), (False, False)]:
            width = 1024 if board_7b else 800
            lines = 15 if board_7b else 10
            src = stub / 'check.cpp'
            src.write_text(
                '#include "Waveshare_ST7262_LVGL.h"\n'
                f'static_assert(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE == {width} * {lines}, "bounce depth");\n'
                'static_assert(LVGL_PORT_DISP_BUFFER_NUM == 3, "triple buffering");\n'
                'static_assert(LVGL_PORT_FULL_REFRESH == 1, "full refresh");\n'
                'int main() { return 0; }\n'
            )
            cmd = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                   f'-DESP_PANEL_LCD_WIDTH={width}', '-DESP_PANEL_LCD_HEIGHT=600',
                   '-I', str(stub), '-I', str(HEADER.parent)]
            if board_7b:
                cmd.append('-DESP_PLANTS_WAVESHARE_7B=1')
            if distribution:
                cmd.append('-DESP_PLANTS_DISTRIBUTION_BUILD=1')
            cmd.append(str(src))
            subprocess.run(cmd, check=True, capture_output=True, text=True)
