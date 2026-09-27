from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
BUILD = ROOT / "firmware/waveshare-hub/include/build_version.h"
VERSION = ROOT / "VERSION"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_alpha49_identity():
    assert VERSION.read_text(encoding="utf-8").strip() == "0.2.0-alpha.49"
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.49"' in read(BUILD)


def test_stage_f_adds_measurement_not_policy_changes():
    source = read(MAIN)
    assert "void logDisplayRuntimeConfig()" in source
    assert '"[display-runtime] tearing_mode=%d configured_rgb_buffers=%d full_refresh=%d "' in source
    assert "LVGL_PORT_AVOID_TEARING_MODE" in source
    assert "LVGL_PORT_DISP_BUFFER_NUM" in source
    assert "drv->full_refresh" in source
    assert "drv->direct_mode" in source
    assert "drawBuf->buf1" in source
    assert "drawBuf->buf2" in source
    assert "drawBuf->size" in source


def test_stage_f_logs_pre_ui_allocator_baseline():
    source = read(MAIN)
    setup = source[source.index("void setup()") : source.index("void loop()")]
    assert 'lcd_init();' in setup
    assert 'logDisplayRuntimeConfig();' in setup
    assert 'logLvglMemory("lvgl-init-pre-ui");' in setup
    assert setup.index('lcd_init();') < setup.index('logDisplayRuntimeConfig();')
    assert setup.index('logDisplayRuntimeConfig();') < setup.index('logLvglMemory("lvgl-init-pre-ui");')
    assert setup.index('logLvglMemory("lvgl-init-pre-ui");') < setup.index('buildUi();')


def test_stage_f_keeps_diagnostics_out_of_distribution_builds():
    source = read(MAIN)
    block = source[source.index("void logDisplayRuntimeConfig()") - 64 : source.index("void clearSetupQrCanvas")]
    assert "#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)" in block
    assert "void logDisplayRuntimeConfig() {}" in block


def test_capacity_and_virtual_pool_architecture_remain_intact():
    source = read(MAIN)
    assert "constexpr size_t kMaxSensors = 32;" in source
    assert "constexpr size_t kMaxInfrastructure = 32;" in source
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
    assert "AllSensorRow allRows[espplants_all_virtual_list::kPoolSize];" in source
    assert "InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];" in source


def test_stage_f_does_not_write_display_policy_macros():
    source = read(MAIN)
    forbidden = (
        "#define LVGL_PORT_AVOID_TEARING_MODE",
        "#define LVGL_PORT_DISP_BUFFER_NUM",
        "#define LVGL_PORT_FULL_REFRESH",
        "#define LV_MEM_SIZE",
    )
    for token in forbidden:
        assert token not in source
