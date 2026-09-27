from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
BUILD = ROOT / "firmware/waveshare-hub/include/build_version.h"
VERSION = ROOT / "VERSION"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_stage_e_or_later_identity():
    version = VERSION.read_text(encoding="utf-8").strip()
    assert version.startswith("0.2.0-alpha.")
    alpha = int(version.rsplit(".", 1)[1])
    assert alpha >= 48
    assert f'#define ESP_PLANTS_WAVESHARE_VERSION "{version}"' in read(BUILD)


def test_broad_ui_dirty_flag_is_removed():
    source = read(MAIN)
    assert "bool uiDirty" not in source
    assert "uiDirty =" not in source
    for field in (
        "header", "sensorOrder", "home", "all", "plant", "settings",
        "advanced", "update", "pair",
    ):
        assert f"bool {field} = true;" in source


def test_sort_cache_only_rebuilds_when_dirty():
    source = read(MAIN)
    assert "void refreshSortedSensorSlots()" in source
    assert "if (!dirty.sensorOrder) return;" in source
    assert "sortedSensorCount = buildSortedSlots(sortedSensorSlots);" in source
    assert "dirty.sensorOrder = false;" in source
    assert "const size_t logicalCount = sortedSensorCount;" in source


def test_non_sorting_sensor_updates_do_not_force_sort():
    source = read(MAIN)
    assert "const bool orderMayChange =" in source
    assert "!wasSeenThisBoot ||" in source
    assert "previousSoilMoisture != report.soilMoisturePct" in source
    assert "markSensorValuesDirty(orderMayChange);" in source
    assert "if (orderMayChange) dirty.sensorOrder = true;" in source


def test_one_second_refresh_is_limited_to_time_dependent_visible_work():
    source = read(MAIN)
    assert "const bool timedPageWork = intervalElapsed &&" in source
    assert "currentPage == Page::All || currentPage == Page::Plant" in source
    assert "const bool timedModalWork = intervalElapsed &&" in source
    assert "updateModalOpen || pairDialogState != PairDialogState::Hidden" in source
    assert "if (!force && !dirty.home) break;" in source
    assert "if (!force && !dirty.settings) break;" in source
    assert "if (!force && !dirty.advanced) break;" in source


def test_page_entry_still_forces_immediate_refresh():
    source = read(MAIN)
    show_page = source[source.index("void showPage(Page page)"):source.index("void showInfrastructureRemoveConfirm", source.index("void showPage(Page page)"))]
    assert "refreshUi(true, true);" in show_page


def test_stage_e_does_not_change_capacity_or_lvgl_pool_policy():
    source = read(MAIN)
    assert "constexpr size_t kMaxSensors = 32;" in source
    assert "constexpr size_t kMaxInfrastructure = 32;" in source
    conf = read(ROOT / "firmware/waveshare-hub/include/lv_conf.h") if (ROOT / "firmware/waveshare-hub/include/lv_conf.h").exists() else ""
    if conf:
        assert "#define LV_MEM_SIZE (128U * 1024U)" in conf
