from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
BUILD = ROOT / "firmware/waveshare-hub/include/build_version.h"
LV_CONF = ROOT / "firmware/waveshare-hub/include/lv_conf.h"
PIO = ROOT / "firmware/waveshare-hub/platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_block(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_waveshare_identity_and_allocator_policy_are_intentional():
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.49"
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.49"' in read(BUILD)
    lv_conf = read(LV_CONF)
    assert "#define LV_MEM_CUSTOM 0" in lv_conf
    assert "#define LV_MEM_SIZE (128U * 1024U)" in lv_conf
    assert "upload_port = COM11" in read(PIO)


def test_common_card_shadow_is_removed_without_geometry_changes():
    source = read(MAIN)
    card = function_block(source, "lv_obj_t *card(", "void refreshUi(")
    assert "lv_obj_set_style_shadow_width" not in card
    assert "lv_obj_set_style_shadow_opa" not in card
    for required in (
        "lv_obj_set_pos(obj, x, y);",
        "lv_obj_set_size(obj, w, h);",
        "lv_obj_set_style_radius(obj, 20, 0);",
        "lv_obj_set_style_bg_color(obj, lv_color_hex(0x18231D), 0);",
    ):
        assert required in card


def test_general_label_helper_skips_unchanged_text():
    source = read(MAIN)
    helper = function_block(source, "void label(lv_obj_t *obj, const char *text)", "void clearSetupQrCanvas")
    assert "lv_label_get_text(obj)" in helper
    assert "strcmp(current, text) == 0" in helper
    assert helper.index("strcmp(current, text) == 0") < helper.index("lv_label_set_text(obj, text)")


def test_lvgl_allocator_telemetry_uses_lvgl_monitor_and_required_fields():
    source = read(MAIN)
    telemetry = function_block(source, "#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)", "void clearSetupQrCanvas")
    assert "lv_mem_monitor(&monitor);" in telemetry
    for field in (
        "monitor.total_size", "monitor.free_size", "monitor.free_biggest_size",
        "monitor.free_cnt", "monitor.used_cnt", "monitor.max_used",
        "monitor.frag_pct", "monitor.used_pct",
    ):
        assert field in telemetry
    assert '"[lvgl-mem] %s total=%lu free=%lu used=%lu biggest=%lu free_cnt=%lu "' in telemetry
    for transition in (
        'logLvglMemory("build-ui")', 'logLvglMemory("page-home")',
        'logLvglMemory("page-all-sensors")', 'logLvglMemory("page-plant-detail")',
        'logLvglMemory("page-settings")', 'logLvglMemory("page-advanced-zigbee")',
        'logLvglMemory("rename-plant-open")', 'logLvglMemory("pairing-ui-open")',
        'logLvglMemory("network-updates-open")',
    ):
        assert transition in source


def test_sorted_sensor_lists_do_not_reorder_physical_rows_every_refresh():
    source = read(MAIN)
    refresh = function_block(source, "void refreshUi(bool force, bool alreadyInLvglContext)", "void handleNetworkStatus")
    assert "lv_obj_move_to_index(" not in refresh
    assert "refreshHomeVirtualList(true);" in refresh
    assert "refreshAllVirtualList(true);" in refresh


def test_refresh_is_page_local_and_modals_are_gated():
    source = read(MAIN)
    refresh = function_block(source, "void refreshUi(bool force, bool alreadyInLvglContext)", "void handleNetworkStatus")
    for page in ("Home", "All", "Plant", "Settings", "Advanced"):
        assert f"case Page::{page}:" in refresh
    assert "if (updateModalOpen && (force || dirty.update || intervalElapsed))" in refresh
    assert "pairDialogState != PairDialogState::Hidden &&" in refresh
    assert "refreshPairDialog();" in refresh
    show = function_block(source, "void showPage(Page page)", "void navEvent")
    assert "refreshUi(true, true);" in show


def test_stage_a_allocator_and_refresh_changes_remain_after_virtualization():
    source = read(MAIN)
    assert "AllSensorRow allRows[espplants_all_virtual_list::kPoolSize];" in source
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
