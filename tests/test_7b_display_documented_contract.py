"""Focused source invariants for the hardware-tested 7B display path.

These checks do not replace PlatformIO compilation or physical panel testing.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / 'firmware/waveshare-hub'


def source(path):
    return (HUB / path).read_text(encoding='utf-8')


def test_documented_framebuffer_is_runtime_configuration_not_board_fallback():
    board = source('include/waveshare_panel_board_7b.h')
    port = source('lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp')
    cfg = source('lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.h')
    assert '#define ESP_PANEL_LCD_RGB_FRAME_BUF_NUM (1)' in board
    assert '3,686,400 bytes of PSRAM' in board
    assert '#define LVGL_PORT_AVOID_TEARING_MODE (2)' in cfg
    assert '#define LVGL_PORT_DISP_BUFFER_NUM (3)' in cfg
    assert 'rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);' in port
    assert port.index('configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM)') < port.index('panel->begin();')


def test_keep_hardware_tested_display_timings_and_bounce_buffer():
    board = source('include/waveshare_panel_board_7b.h')
    cfg = source('lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.h')
    pio = source('platformio.ini')
    assert '#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)' in board
    assert '#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE (ESP_PANEL_LCD_WIDTH * 15)' in board
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 15)' in cfg
    assert 'post:scripts/use_7b_vsync_sdk.py' in pio
    assert 'upload_port = COM11' in pio
    assert 'upload_port = COM13' in pio


def test_existing_flash_recovery_remains_event_driven_not_frame_rate_driven():
    guard = source('src/runtime_flash_guard.cpp')
    port = source('lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp')
    assert 'gDisplayRecoveryPending' in guard
    assert '__wrap_nvs_commit' in guard
    assert 'if (restart_rgb_panel_scan())' in guard
    assert 'seven_b_diag_bounce_frame_finish_count' in port
    assert re.search(r'\[display-diag\].*bounce_frame_finish', port)
    assert 'restart_rgb_panel_scan()' not in port.split('static void lvgl_port_task(', 1)[1].split('IRAM_ATTR bool onRefreshFinishCallback', 1)[0]


def test_readme_distinguishes_7b_hardware_stress_from_release_qualification():
    root = (ROOT / 'README.md').read_text(encoding='utf-8')
    hub = source('README.md')
    assert 'physically tested' in root
    assert 'production-release qualification' in root
    assert 'long-duration' in hub
    assert 'bounce_frame_finish' in hub
