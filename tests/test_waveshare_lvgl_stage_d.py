from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
VIRTUAL = ROOT / "firmware/waveshare-hub/include/all_virtual_list.h"
PIO = ROOT / "firmware/waveshare-hub/platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_block(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_stage_d_identity_and_platformio_are_preserved():
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.47"
    source = read(ROOT / "firmware/waveshare-hub/include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.47"' in source
    pio = read(PIO)
    assert "upload_port = COM11" in pio
    assert "lvgl/lvgl@8.3.11" in pio


def test_all_list_uses_fixed_seven_row_pool_and_virtual_content():
    source = read(MAIN)
    build = function_block(source, "void buildAll(lv_obj_t *screen)", "void buildPlant")
    assert "AllSensorRow allRows[espplants_all_virtual_list::kPoolSize];" in source
    assert "constexpr size_t kPoolSize = 7;" in read(VIRTUAL)
    assert "allVirtualContent = lv_obj_create(allList);" in build
    assert "lv_obj_set_size(allVirtualContent, 742," in build
    assert "lv_obj_set_flex_flow(allList" not in build
    assert "for (size_t i = 0; i < espplants_all_virtual_list::kPoolSize; ++i)" in build
    assert "lv_obj_set_pos(row.box, 0, 0);" in build


def test_all_virtual_geometry_preserves_locked_layout():
    source = read(MAIN)
    build = function_block(source, "void buildAll(lv_obj_t *screen)", "void buildPlant")
    for required in (
        "lv_obj_set_pos(allPage, 0, 66);",
        "lv_obj_set_size(allPage, 800, 356);",
        "lv_obj_t *p = card(allPage, 14, 10, 772, 334);",
        "lv_obj_set_pos(allList, 10, 72);",
        "lv_obj_set_size(allList, 752, espplants_all_virtual_list::kViewportHeight);",
        "lv_obj_set_size(row.box, 742, espplants_all_virtual_list::kRowHeight);",
        "lv_obj_set_pos(row.name, 4, 9);",
        "lv_obj_set_pos(row.moisture, 305, 8);",
        "lv_obj_set_pos(row.battery, 435, 9);",
        "lv_obj_set_pos(row.updated, 565, 10);",
    ):
        assert required in build


def test_all_virtual_content_height_and_scroll_binding_are_explicit():
    source = read(MAIN)
    helper = read(VIRTUAL)
    assert "logicalCount * static_cast<size_t>(kStride)" in helper
    assert "- kRowGap" in helper
    assert "firstVisible > 0 ? firstVisible - 1 : 0" in helper
    assert "clampScrollY" in helper
    refresh = function_block(source, "void refreshAllVirtualList", "void allListScrollEvent")
    assert "buildSortedSlots(logicalSlots)" in refresh
    assert "lv_obj_set_height(allVirtualContent" in refresh
    assert "lv_obj_get_scroll_y(allList)" in refresh
    assert "lv_obj_scroll_to_y(allList, clamped, LV_ANIM_OFF)" in refresh
    assert "firstPoolLogicalIndex" in refresh
    assert "allFirstLogicalIndex" in refresh
    assert "lv_obj_set_pos(row.box, 0," in refresh


def test_all_row_click_identity_comes_from_reusable_row_binding():
    source = read(MAIN)
    event = function_block(source, "void allRowEvent", "void featuredEvent")
    assert "static_cast<AllSensorRow *>(lv_event_get_user_data(event))" in event
    assert "row->boundSlot" in event
    assert "reinterpret_cast<intptr_t>(lv_event_get_user_data(event))" not in event
    build = function_block(source, "void buildAll(lv_obj_t *screen)", "void buildPlant")
    assert "LV_EVENT_CLICKED, &row" in build


def test_all_rows_preserve_columns_freshness_and_thirsty_highlight():
    source = read(MAIN)
    refresh = function_block(source, "void refreshAllVirtualList", "void allListScrollEvent")
    assert "label(row.name, sensors[slot].name)" in refresh
    assert "hasFreshMoisture(sensors[slot])" in refresh
    assert "plantlink::SensorHasBattery" in refresh
    assert "formatLastReport(sensors[slot]" in refresh
    assert "plantlink::SensorHasWaterWarning" in refresh
    assert "thirsty ? 0x3A2723 : 0x1D2922" in refresh


def test_all_logical_source_capacity_sort_and_current_boot_freshness_are_unchanged():
    source = read(MAIN)
    assert "constexpr size_t kMaxSensors = 32;" in source
    assert "PlantSensor sensors[kMaxSensors];" in source
    assert "const size_t logicalCount = buildSortedSlots(logicalSlots);" in source
    assert "sensor.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture" in source
    assert 'snprintf(out, size, "WAITING")' in source


def test_stage_d_preserves_home_and_advanced_virtual_pools():
    source = read(MAIN)
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
    assert "InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];" in source
    assert "constexpr size_t kPoolSize = 7;" in read(ROOT / "firmware/waveshare-hub/include/home_virtual_list.h")
    assert "constexpr size_t kPoolSize = 5;" in read(ROOT / "firmware/waveshare-hub/include/advanced_virtual_list.h")
