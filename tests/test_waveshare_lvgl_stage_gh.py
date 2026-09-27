from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
BUILD = ROOT / "firmware/waveshare-hub/include/build_version.h"
VERSION = ROOT / "VERSION"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_alpha50_identity():
    assert VERSION.read_text(encoding="utf-8").strip() == "0.2.0-alpha.50"
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.50"' in read(BUILD)


def test_stage_gh_adds_long_run_memory_telemetry():
    source = read(MAIN)
    assert "#include <esp_heap_caps.h>" in source
    assert "constexpr uint32_t kRuntimeTelemetryIntervalMs = 60U * 1000U;" in source
    assert 'logEspMemory("display-pre-init");' in source
    assert 'logEspMemory("display-post-init");' in source
    assert 'logEspMemory("runtime-60s");' in source
    assert 'logLvglMemory("runtime-60s");' in source
    assert '"[runtime-mem] %s internal_free=%lu internal_largest=%lu "' in source


def test_stage_gh_profiles_ui_refresh_and_lock_wait():
    source = read(MAIN)
    assert "struct UiRuntimeStats" in source
    assert "esp_timer_get_time()" in source
    assert "recordUiRefreshRuntime" in source
    assert '"[ui-runtime] refreshes=%lu forced=%lu timed=%lu avg_us=%lu max_us=%lu "' in source
    assert "max_lock_wait_us=%lu" in source
    assert "serviceRuntimeTelemetry();" in source


def test_stage_gh_periodic_probe_is_bounded_and_dev_only():
    source = read(MAIN)
    block = source[source.index("void logDisplayRuntimeConfig();") : source.index("void clearSetupQrCanvas")]
    assert "if (lvgl_port_lock(25))" in block
    assert 'telemetry LVGL lock timeout after 25ms' in block
    assert "#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)" in block
    assert "void serviceRuntimeTelemetry() {}" in block


def test_stage_gh_does_not_change_display_or_allocator_policy():
    source = read(MAIN)
    forbidden = (
        "#define LVGL_PORT_AVOID_TEARING_MODE",
        "#define LVGL_PORT_DISP_BUFFER_NUM",
        "#define LVGL_PORT_FULL_REFRESH",
        "#define LV_MEM_SIZE",
    )
    for token in forbidden:
        assert token not in source


def test_capacity_and_virtual_pools_stay_intact():
    source = read(MAIN)
    assert "constexpr size_t kMaxSensors = 32;" in source
    assert "constexpr size_t kMaxInfrastructure = 32;" in source
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
    assert "AllSensorRow allRows[espplants_all_virtual_list::kPoolSize];" in source
    assert "InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];" in source
