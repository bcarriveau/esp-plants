from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware" / "waveshare-hub"
PIO = (HUB / "platformio.ini").read_text(encoding="utf-8")
BOARD = (HUB / "include" / "waveshare_panel_board_7b.h").read_text(encoding="utf-8")
DRIVER_H = (
    HUB / "lib" / "Waveshare_ST7262_LVGL" / "src" / "Waveshare_ST7262_LVGL.h"
).read_text(encoding="utf-8")
BUILDER = (HUB / "scripts" / "build_7b_vsync_sdk.sh").read_text(encoding="utf-8")
WRAPPER = (HUB / "scripts" / "build_7b_vsync_sdk.ps1").read_text(encoding="utf-8")
HOOK = (HUB / "scripts" / "use_7b_vsync_sdk.py").read_text(encoding="utf-8")
GUARD = (HUB / "include" / "esp_plants_7b_vsync_guard.h").read_text(encoding="utf-8")
TASKS = (ROOT / ".vscode" / "tasks.json").read_text(encoding="utf-8")


def env_section(name: str) -> str:
    marker = f"[env:{name}]"
    start = PIO.index(marker)
    next_section = PIO.find("\n[env:", start + len(marker))
    if next_section < 0:
        return PIO[start:]
    return PIO[start:next_section]


def test_24mhz_15line_baseline_and_mode2_are_locked():
    assert "#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)" in BOARD
    assert "#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE (ESP_PANEL_LCD_WIDTH * 15)" in BOARD
    assert "#define LVGL_PORT_AVOID_TEARING_MODE (2)" in DRIVER_H
    assert "#define LVGL_PORT_DISP_BUFFER_NUM (3)" in DRIVER_H
    assert "#define LVGL_PORT_FULL_REFRESH (1)" in DRIVER_H
    assert "#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 15)" in DRIVER_H
    assert 600 % (2 * 15) == 0


def test_original_7_stays_on_stock_sdk_and_com11():
    base = env_section("waveshare_s3_touch_lcd_7")
    assert "upload_port = COM11" in base
    assert "esp32-3.0.7-h.zip" in base
    assert "use_7b_vsync_sdk.py" not in base
    assert "esp_plants_7b_vsync_guard.h" not in base


def test_7b_only_uses_real_sdk_overlay_without_fake_config_define():
    seven_b = env_section("waveshare_s3_touch_lcd_7b")
    assert "upload_port = COM13" in seven_b
    assert "post:scripts/use_7b_vsync_sdk.py" in seven_b
    assert "-include esp_plants_7b_vsync_guard.h" in seven_b
    assert "-Wl,--wrap=esp_mbedtls_mem_calloc" in seven_b
    assert "-DCONFIG_LCD_RGB_RESTART_IN_VSYNC" not in PIO
    assert "-DCONFIG_GDMA_CTRL_FUNC_IN_IRAM" not in PIO


def test_release_and_diag_inherit_vsync_overlay_and_existing_tls_path():
    release = env_section("waveshare_s3_touch_lcd_7b_release")
    diag = env_section("waveshare_s3_touch_lcd_7b_diag")
    assert "${env:waveshare_s3_touch_lcd_7b.extra_scripts}" in release
    assert "post:scripts/build_plants_ota.py" in release
    assert "${env:waveshare_s3_touch_lcd_7b.build_flags}" in release
    assert "${env:waveshare_s3_touch_lcd_7b.build_flags}" in diag


def test_builder_is_pinned_to_3_0_7_h_identity_and_exact_idf_commit():
    assert "esp-arduino-libs/esp32-arduino-lib-builder.git" in BUILDER
    assert "9e2e6b17b99af5b677f7a09b7c208cfe9a4c6e1d" in BUILDER
    assert 'ARDUINO_CORE="3.0.7"' in BUILDER
    assert 'ARDUINO_CORE_COMMIT="3bfa3e0a56c80305eec90f10e8318af8d8091bab"' in BUILDER
    assert 'IDF_BRANCH="release/v5.1"' in BUILDER
    assert 'IDF_COMMIT_EXPECTED="632e0c2a9fc7c754db4135dabb67f7fc6aa9fb87"' in BUILDER
    assert "41f67e1c11f68b57d651955c93b63d6a8d35808ce6aff6ba3d1e1476178758f2" in BUILDER



def test_builder_bypasses_detached_arduino_tag_pull_bug():
    assert 'checkout --detach "$ARDUINO_CORE_COMMIT"' in BUILDER
    assert 'source "$BUILDER_DIR/tools/install-esp-idf.sh"' in BUILDER
    assert BUILDER.count("./build.sh \\\n    -s") >= 2
    assert '-A "$ARDUINO_CORE"' not in BUILDER
    assert 'IDF_COMMIT_EXPECTED' in BUILDER
    assert '-e components/arduino/' in BUILDER
    assert '-e components/arduino_tinyusb/tinyusb/' in BUILDER

def test_builder_preserves_high_perf_settings_and_adds_only_lcd_kconfig():
    assert 'unset_config(common, "COMPILER_OPTIMIZATION_SIZE")' in BUILDER
    assert 'set_config(common, "COMPILER_OPTIMIZATION_PERF", "y")' in BUILDER
    assert 'set_config(s3, "ESP32S3_DATA_CACHE_LINE_64B", "y")' in BUILDER
    assert 'set_config(s3, "SPIRAM_FETCH_INSTRUCTIONS", "y")' in BUILDER
    assert 'set_config(s3, "SPIRAM_RODATA", "y")' in BUILDER
    assert 'set_config(s3, "LCD_RGB_RESTART_IN_VSYNC", "y")' in BUILDER
    assert 'set_config(s3, "GDMA_CTRL_FUNC_IN_IRAM"' not in BUILDER
    assert "#define CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1" in BUILDER


def test_builder_outputs_only_the_two_rebuilt_archives_and_matching_config():
    for token in (
        "common/libesp_lcd_vsync.a",
        "qio_opi/libesp_hw_support_vsync.a",
        "qio_opi/include/sdkconfig.h",
        "manifest.json",
    ):
        assert token in BUILDER or token in HOOK
    assert "-b idf-libs" in BUILDER
    assert "qio 80m qio_ram" in BUILDER
    assert "-b mem-variant" in BUILDER
    assert "qio 80m opi_ram" in BUILDER
    assert "esp_lcd_panel_rgb" in BUILDER
    assert "grep -qi 'gdma'" in BUILDER


def test_platformio_hook_verifies_hashes_config_and_actual_link_map_members():
    assert "sha256_file" in HOOK
    assert "CONFIG_LCD_RGB_RESTART_IN_VSYNC 1" in HOOK
    assert "CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1" in HOOK
    assert 'env.Prepend(LIBS=["esp_lcd_vsync", "esp_hw_support_vsync"])' in HOOK
    assert "libesp_lcd_vsync.a(" in HOOK
    assert "libesp_hw_support_vsync.a(" in HOOK
    assert "AddPostAction" in HOOK


def test_compile_guard_rejects_header_only_or_partial_sdk_builds():
    for token in (
        "CONFIG_LCD_RGB_RESTART_IN_VSYNC",
        "CONFIG_GDMA_CTRL_FUNC_IN_IRAM",
        "CONFIG_COMPILER_OPTIMIZATION_PERF",
        "CONFIG_ESP32S3_DATA_CACHE_LINE_64B",
        "CONFIG_SPIRAM_FETCH_INSTRUCTIONS",
        "CONFIG_SPIRAM_RODATA",
    ):
        assert token in GUARD


def test_windows_task_uses_supported_wsl_lib_builder_path():
    assert '"label": "ESP PLANTS: Build 7B VSYNC SDK"' in TASKS
    assert "build_7b_vsync_sdk.ps1" in TASKS
    assert "wsl.exe" in WRAPPER
    assert "build_7b_vsync_sdk.sh" in WRAPPER
