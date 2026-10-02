from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware/waveshare-hub"
PIO = (HUB / "platformio.ini").read_text(encoding="utf-8")
TLS = (HUB / "src" / "tls_memory_7b.cpp").read_text(encoding="utf-8")
BOARD_7B = (HUB / "include" / "waveshare_panel_board_7b.h").read_text(
    encoding="utf-8"
)


def env_section(name: str) -> str:
    marker = f"[env:{name}]"
    start = PIO.index(marker)
    next_section = PIO.find("\n[env:", start + len(marker))
    if next_section < 0:
        return PIO[start:]
    return PIO[start:next_section]


def test_tls_large_allocations_move_to_psram_on_7b_only():
    base = env_section("waveshare_s3_touch_lcd_7")
    seven_b = env_section("waveshare_s3_touch_lcd_7b")

    assert "-Wl,--wrap=esp_mbedtls_mem_calloc" not in base
    assert "-Wl,--wrap=esp_mbedtls_mem_calloc" in seven_b
    assert "-DESP_PLANTS_WAVESHARE_7B=1" in seven_b

    assert "#if defined(ESP_PLANTS_WAVESHARE_7B)" in TLS
    assert "kTlsPsramThresholdBytes = 4U * 1024U" in TLS
    assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in TLS
    assert "__real_esp_mbedtls_mem_calloc" in TLS
    assert "size > SIZE_MAX / n" in TLS


def test_existing_ports_and_7b_display_tuning_stay_locked():
    assert "upload_port = COM11" in env_section("waveshare_s3_touch_lcd_7")
    assert "upload_port = COM13" in env_section("waveshare_s3_touch_lcd_7b")

    assert "#define ESP_PANEL_LCD_RGB_CLK_HZ (26 * 1000 * 1000)" in BOARD_7B
    assert (
        "#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE "
        "(ESP_PANEL_LCD_WIDTH * 10)"
    ) in BOARD_7B


def test_release_and_diag_inherit_same_7b_tls_fix():
    release = env_section("waveshare_s3_touch_lcd_7b_release")
    diag = env_section("waveshare_s3_touch_lcd_7b_diag")
    assert "${env:waveshare_s3_touch_lcd_7b.build_flags}" in release
    assert "${env:waveshare_s3_touch_lcd_7b.build_flags}" in diag
