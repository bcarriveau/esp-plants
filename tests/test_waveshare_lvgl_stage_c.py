from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
VIRTUAL = ROOT / "firmware/waveshare-hub/include/home_virtual_list.h"
PIO = ROOT / "firmware/waveshare-hub/platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_block(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_stage_c_identity_and_platformio_are_preserved():
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.47"
    source = read(ROOT / "firmware/waveshare-hub/include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.47"' in source
    pio = read(PIO)
    assert "upload_port = COM11" in pio
    assert "lvgl/lvgl@8.3.11" in pio


def test_home_list_uses_fixed_seven_row_pool_and_virtual_content():
    source = read(MAIN)
    build = function_block(source, "void buildHome(lv_obj_t *screen)", "void buildAll")
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
    assert "constexpr size_t kPoolSize = 7;" in read(VIRTUAL)
    assert "homeVirtualContent = lv_obj_create(homeList);" in build
    assert "lv_obj_set_size(homeVirtualContent, 228," in build
    assert "lv_obj_set_flex_flow(homeList" not in build
    assert "for (size_t i = 0; i < espplants_home_virtual_list::kPoolSize; ++i)" in build
    assert "lv_obj_set_pos(row.box, 0, 0);" in build


def test_home_virtual_geometry_preserves_locked_layout():
    source = read(MAIN)
    build = function_block(source, "void buildHome(lv_obj_t *screen)", "void buildAll")
    for required in (
        "lv_obj_set_pos(homePage, 0, 66);",
        "lv_obj_set_size(homePage, 800, 356);",
        "lv_obj_t *featured = card(homePage, 14, 10, 500, 334);",
        "lv_obj_t *listCard = card(homePage, 528, 10, 258, 334);",
        "lv_obj_set_pos(homeList, 0, 42);",
        "lv_obj_set_size(homeList, 234, espplants_home_virtual_list::kViewportHeight);",
        "lv_obj_set_size(row.box, 228, espplants_home_virtual_list::kRowHeight);",
        "lv_obj_set_pos(row.bar, 2, 30);",
        "lv_obj_set_size(row.bar, 208, 9);",
    ):
        assert required in build


def test_home_virtual_content_height_and_scroll_binding_are_explicit():
    source = read(MAIN)
    helper = read(VIRTUAL)
    assert "logicalCount * static_cast<size_t>(kStride)" in helper
    assert "- kRowGap" in helper
    assert "firstVisible > 0 ? firstVisible - 1 : 0" in helper
    assert "clampScrollY" in helper
    refresh = function_block(source, "void refreshHomeVirtualList", "void homeListScrollEvent")
    assert "buildSortedSlots(logicalSlots)" in refresh
    assert "lv_obj_set_height(homeVirtualContent" in refresh
    assert "lv_obj_get_scroll_y(homeList)" in refresh
    assert "lv_obj_scroll_to_y(homeList, clamped, LV_ANIM_OFF)" in refresh
    assert "firstPoolLogicalIndex" in refresh
    assert "homeFirstLogicalIndex" in refresh
    assert "lv_obj_set_pos(row.box, 0," in refresh


def test_home_row_click_identity_comes_from_reusable_row_binding():
    source = read(MAIN)
    event = function_block(source, "void rowEvent", "void allRowEvent")
    assert "static_cast<PlantListRow *>(lv_event_get_user_data(event))" in event
    assert "row->boundSlot" in event
    assert "reinterpret_cast<intptr_t>(lv_event_get_user_data(event))" not in event
    build = function_block(source, "void buildHome(lv_obj_t *screen)", "void buildAll")
    assert "LV_EVENT_CLICKED, &row" in build


def test_home_rows_preserve_name_moisture_bar_and_featured_highlight():
    source = read(MAIN)
    refresh = function_block(source, "void refreshHomeVirtualList", "void homeListScrollEvent")
    assert "staticRowLabel(row.name, row.nameText" in refresh
    assert "hasFreshMoisture(sensors[slot])" in refresh
    assert "lv_bar_set_value(row.bar, sensors[slot].soilMoisturePct, LV_ANIM_OFF)" in refresh
    assert "lv_bar_set_value(row.bar, 0, LV_ANIM_OFF)" in refresh
    assert "homeSensor == static_cast<int>(slot) ? 0x1E3529 : 0x1D2922" in refresh


def test_home_logical_source_capacity_sort_and_freshness_are_unchanged():
    source = read(MAIN)
    assert "constexpr size_t kMaxSensors = 32;" in source
    assert "PlantSensor sensors[kMaxSensors];" in source
    assert "const size_t logicalCount = buildSortedSlots(logicalSlots);" in source
    assert "sensor.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture" in source
    assert 'snprintf(out, size, "WAITING")' in source


def test_stage_c_keeps_home_and_advanced_virtual_pools_intact():
    source = read(MAIN)
    assert "PlantListRow rows[espplants_home_virtual_list::kPoolSize];" in source
    assert "InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];" in source
    assert "constexpr size_t kPoolSize = 7;" in read(VIRTUAL)
    assert "constexpr size_t kPoolSize = 5;" in read(ROOT / "firmware/waveshare-hub/include/advanced_virtual_list.h")
