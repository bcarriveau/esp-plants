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
    assert "-include esp_plants_7b_vsync_guard.h" not in seven_b
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
    assert 'TINYUSB_COMMIT="5217cee5de4cd555018da90f9f1bcc87fb1c1d3a"' in BUILDER



def test_builder_pins_tinyusb_instead_of_following_master():
    assert './tools/update-components.sh' not in BUILDER
    assert 'TINYUSB_REPO="https://github.com/hathach/tinyusb.git"' in BUILDER
    assert 'checkout --detach "$TINYUSB_COMMIT"' in BUILDER
    assert 'src/device/usbd_control.c' in BUILDER
    assert 'rev-parse HEAD' in BUILDER


def test_builder_checks_openocd_libusb_runtime_early():
    assert "ldconfig -p" in BUILDER
    assert "libusb-1.0.so.0" in BUILDER
    assert "libusb-1.0-0" in BUILDER


def test_builder_bypasses_detached_arduino_tag_pull_bug():
    assert 'checkout --detach "$ARDUINO_CORE_COMMIT"' in BUILDER
    assert './tools/install-arduino.sh' not in BUILDER
    assert '-A "$ARDUINO_CORE"' not in BUILDER
    assert 'IDF_COMMIT_EXPECTED' in BUILDER
    assert '-e components/arduino/' in BUILDER
    assert '-e components/arduino_tinyusb/tinyusb/' in BUILDER


def test_builder_sets_up_pinned_idf_without_obsolete_c6_patch():
    assert 'source "$BUILDER_DIR/tools/install-esp-idf.sh"' not in BUILDER
    assert 'checkout --detach "$IDF_COMMIT_EXPECTED"' in BUILDER
    assert 'submodule update --init --recursive --force' in BUILDER
    assert 'apply_builder_patch "esp32s2_i2c_ll_master_init.diff"' in BUILDER
    assert 'apply_builder_patch "mmu_map.diff"' in BUILDER
    assert 'apply_builder_patch "lwip_max_tcp_pcb.diff"' in BUILDER
    assert 'apply_builder_patch "esp32c6_provisioning_bluedroid.diff"' not in BUILDER
    assert 'For all other chips supporting BLE Only' in BUILDER
    assert 'Skipping obsolete ESP32-C6 provisioning patch' in BUILDER
    assert 'set +u\nsource "$IDF_PATH/export.sh"\nset -u' in BUILDER


def test_builder_checks_python_venv_before_expensive_setup():
    assert "python3 -c 'import ensurepip'" in BUILDER
    assert 'python3-venv' in BUILDER

def test_builder_preserves_high_perf_settings_and_adds_only_lcd_kconfig():
    assert 'unset_config(common, "COMPILER_OPTIMIZATION_SIZE")' in BUILDER
    assert 'set_config(common, "COMPILER_OPTIMIZATION_PERF", "y")' in BUILDER
    assert 'set_config(s3, "ESP32S3_DATA_CACHE_LINE_64B", "y")' in BUILDER
    assert 'set_config(s3, "SPIRAM_FETCH_INSTRUCTIONS", "y")' in BUILDER
    assert 'set_config(s3, "SPIRAM_RODATA", "y")' in BUILDER
    assert 'set_config(s3, "LCD_RGB_RESTART_IN_VSYNC", "y")' in BUILDER
    assert 'set_config(s3, "GDMA_CTRL_FUNC_IN_IRAM"' not in BUILDER
    assert "#define CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1" in BUILDER


def test_builder_outputs_custom_lcd_and_matching_linker_script_only():
    for token in (
        "common/libesp_lcd_vsync.a",
        "qio_opi/sections.ld",
        "qio_opi/include/sdkconfig.h",
        "manifest.json",
    ):
        assert token in BUILDER or token in HOOK
    assert "-b idf-libs" not in BUILDER
    assert "-b mem-variant" not in BUILDER
    assert "cmake --build build --target __idf_esp_lcd" in BUILDER
    assert "__idf_esp_hw_support" not in BUILDER
    assert "libesp_hw_support_vsync.a" not in BUILDER
    assert "configs/defconfig.qio_ram" in BUILDER
    assert "configs/defconfig.opi_ram" in BUILDER
    assert "build/esp-idf/esp_lcd/libesp_lcd.a" in BUILDER
    assert "build/esp-idf/esp_system/ld/sections.ld" not in BUILDER
    assert "cmake --build build --target __ldgen_output_sections.ld" not in BUILDER
    assert "build/config/sdkconfig.h" in BUILDER
    assert "esp_lcd_panel_rgb" in BUILDER
    assert "STOCK_SECTIONS_CANDIDATES" in BUILDER
    assert "esp32s3/qio_opi/sections.ld" in BUILDER
    assert "ESP PLANTS 7B VSYNC: CONFIG_GDMA_CTRL_FUNC_IN_IRAM" in BUILDER
    for symbol in ("gdma_start", "gdma_stop", "gdma_append", "gdma_reset"):
        assert symbol in BUILDER


def test_builder_preserves_stock_hw_support_and_patches_stock_sections_ld_only():
    assert "Keep Arduino's stock libesp_hw_support.a" in BUILDER
    assert "do NOT rebuild esp_hw_support" in BUILDER
    assert 'PIO_PACKAGES_DIR="$HUB_DIR/.pio-packages"' in BUILDER
    assert "STOCK_MEM_SDKCONFIG" in BUILDER
    assert "installed stock qio_opi sdkconfig.h is not the expected 3.0.7-h" in BUILDER
    assert 'insert_into_output_section(lines, ".iram0.text :", iram_rule)' in BUILDER
    assert 'insert_into_output_section(lines, ".dram0.data :", dram_rule)' in BUILDER
    assert "GNU ld consumes an input section at its first matching output rule" in BUILDER
    assert 'Path("qio_opi/sections.ld")' in BUILDER
    assert 'Path("qio_opi/libesp_hw_support_vsync.a")' not in BUILDER
    assert '"stock_qio_opi_sections_sha256": stock_sections_sha' in BUILDER


def test_builder_avoids_full_elf_and_unrelated_managed_component_compilation():
    assert 'idf.py -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="$COMMON_CONFIGS" reconfigure' in BUILDER
    assert 'idf.py -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="$MEM_CONFIGS" reconfigure' in BUILDER
    assert "__idf_espressif__esp-tflite-micro" not in BUILDER
    assert "complete application ELF" in BUILDER
    assert "IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1" in BUILDER
    assert "__ldgen_output_sections.ld" not in BUILDER


def test_platformio_hook_uses_custom_lcd_stock_hw_support_and_generated_sections():
    assert "sha256_file" in HOOK
    assert "CONFIG_LCD_RGB_RESTART_IN_VSYNC 1" in HOOK
    assert "CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1" in HOOK
    assert 'SECTIONS_LD = MEM_DIR / "sections.ld"' in HOOK
    assert 'env.Prepend(LIBS=["esp_lcd_vsync"])' in HOOK
    assert 'env.Prepend(LIBS=["esp_lcd_vsync", "esp_hw_support_vsync"])' not in HOOK
    assert 'HW_LIB = MEM_DIR / "libesp_hw_support_vsync.a"' not in HOOK
    assert "replace_sections_linker_script(env)" in HOOK
    assert "expected to replace exactly one framework sections.ld linker flag" in HOOK
    assert "libesp_lcd_vsync.a(" in HOOK
    assert "libesp_hw_support.a(" in HOOK
    assert "AddPostAction" in HOOK


def test_post_link_guard_rejects_flash_placed_gdma_or_mspi_symbols():
    for symbol in (
        "gdma_start",
        "gdma_stop",
        "gdma_append",
        "gdma_reset",
        "mspi_timing_enter_low_speed_mode",
        "mspi_timing_config_set_psram_clock",
    ):
        assert symbol in HOOK
    assert "0x40300000 <= address < 0x40400000" in HOOK
    assert "linked outside S3 IRAM/noflash" in HOOK
    assert "_read_symbol_addresses_from_map" in HOOK
    assert 'subst("$NM")' not in HOOK
    assert "subprocess" not in HOOK


def test_post_link_symbol_guard_uses_map_file_only_on_windows():
    assert "parts[-1] not in wanted" in HOOK
    assert "address = int(parts[0], 16)" in HOOK
    assert "symbols = _read_symbol_addresses_from_map(map_text, iram_symbols)" in HOOK
    assert "could not inspect ELF symbols" not in HOOK


def test_platformio_post_action_accepts_scons_keyword_env():
    assert "def verify_link_map(target, source, env):" in HOOK
    assert 'Path(env.subst("$BUILD_DIR"))' in HOOK
    assert "def verify_link_map(target, source, build_env):" not in HOOK

def test_overlay_validation_does_not_replace_application_sdkconfig():
    assert "CPPPATH" not in HOOK
    assert "CONFIG_DIR" in HOOK
    assert "SDKCONFIG.read_text" in HOOK
    assert "generated sdkconfig.h is missing" in HOOK
    assert "-include esp_plants_7b_vsync_guard.h" not in PIO


def test_windows_task_uses_supported_wsl_lib_builder_path():
    assert '"label": "ESP PLANTS: Build 7B VSYNC SDK"' in TASKS
    assert "build_7b_vsync_sdk.ps1" in TASKS
    assert "wsl.exe" in WRAPPER
    assert "build_7b_vsync_sdk.sh" in WRAPPER
