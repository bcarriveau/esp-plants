"""Guards for the 7B RGB/flash timing investigation; no UI or panel retuning."""
from pathlib import Path

HUB = Path(__file__).resolve().parents[1] / 'firmware' / 'waveshare-hub'
DRIVER = (HUB / 'lib' / 'Waveshare_ST7262_LVGL' / 'src' / 'Waveshare_ST7262_LVGL.cpp').read_text()
INSTALLER = (HUB / 'src' / 'plants_ota_installer.cpp').read_text()
CONFIG = (HUB / 'lib' / 'Waveshare_ST7262_LVGL' / 'src' / 'Waveshare_ST7262_LVGL.h').read_text()
BOARD = (HUB / 'include' / 'waveshare_panel_board_7b.h').read_text()
HOOK = (HUB / 'scripts' / 'use_7b_vsync_sdk.py').read_text()
PIO = (HUB / 'platformio.ini').read_text()


def test_vsync_link_validation_stays_on_custom_driver_and_iram():
    assert 'LIBS=["esp_lcd_vsync"]' in HOOK
    assert 'libesp_lcd_vsync.a(' in HOOK
    assert 'CONFIG_LCD_RGB_RESTART_IN_VSYNC 1' in HOOK
    assert 'linked outside S3 IRAM/noflash' in HOOK
    assert 'post:scripts/use_7b_vsync_sdk.py' in PIO


def test_three_buffers_and_panel_clock_unchanged():
    assert '#define LVGL_PORT_AVOID_TEARING_MODE (2)' in CONFIG
    assert '#define LVGL_PORT_DISP_BUFFER_NUM (3)' in CONFIG
    assert '#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 15)' in CONFIG
    assert '#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)' in BOARD
    assert 'upload_port = COM11' in PIO
    assert 'upload_port = COM13' in PIO


def test_irq_probe_is_counters_only_and_never_logs_or_restarts():
    isr = DRIVER.split('IRAM_ATTR bool onRgbVsyncCallback(void *user_data)', 1)[1].split('\n#else\n\nvoid flush_callback(', 1)[0]
    assert '#ifdef ESP_PLANTS_WAVESHARE_7B' in isr
    assert 'esp_timer_get_time()' in isr
    assert 'seven_b_diag_max_frame_gap_us' in isr
    assert 'seven_b_diag_gaps_over_60ms' in isr
    assert 'Serial.' not in isr
    assert 'restart_rgb_panel_scan' not in isr
    assert 'lv_obj_' not in isr


def test_display_probe_logs_cumulative_gaps_without_restart():
    lvgl_task = DRIVER.split('static void lvgl_port_task(void *arg)', 1)[1].split('IRAM_ATTR bool onRefreshFinishCallback', 1)[0]
    assert 'max_frame_gap_us=%lu' in lvgl_task
    assert 'gaps_over_60ms=%lu' in lvgl_task
    assert 'restart_rgb_panel_scan' not in lvgl_task
    assert 'app_sdkconfig ' in DRIVER  # distinguishes Arduino header from linked IDF archive


def test_ota_probe_preserves_verified_writer_and_bounds():
    writer = INSTALLER.split('void flashWriterTask(void *parameter)', 1)[1].split('bool waitForWriterState(', 1)[0]
    assert '#ifdef ESP_PLANTS_WAVESHARE_7B' in writer
    assert 'esp_ota_write(writer.otaHandle, writer.internalWriteBuffer, chunk)' in writer
    assert 'kInternalFlashWriteBufferBytes = 1024U' in INSTALLER
    assert 'memcpy(writer.internalWriteBuffer, source + offset, chunk)' in writer
    assert 'max_write_us=%lu' in writer
    assert 'writes_over_20ms=%lu' in writer
    assert 'esp_ota_end(writer.otaHandle)' in writer
    assert 'esp_ota_set_boot_partition(writer.updatePartition)' in writer


def test_progress_logging_is_throttled_to_256k():
    assert 'writer.writtenBytes % (256U * 1024U) == 0U' in INSTALLER
    assert 'esp_timer_get_time() - flashStartedUs' in INSTALLER
    assert 'static_assert(sizeof(FlashWriterContext) <= 2048U' in INSTALLER
