from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
VIRTUAL = ROOT / "firmware/waveshare-hub/include/advanced_virtual_list.h"
PIO = ROOT / "firmware/waveshare-hub/platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_block(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_stage_b_identity_and_platformio_are_preserved():
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.45"
    source = read(ROOT / "firmware/waveshare-hub/include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.45"' in source
    pio = read(PIO)
    assert "upload_port = COM11" in pio
    assert "lvgl/lvgl@8.3.11" in pio


def test_advanced_list_uses_fixed_five_row_pool_and_virtual_content():
    source = read(MAIN)
    build = function_block(source, "void buildAdvanced(lv_obj_t *screen)", "void buildNav")
    assert "InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];" in source
    assert "constexpr size_t kPoolSize = 5;" in read(VIRTUAL)
    assert "advancedVirtualContent = lv_obj_create(advancedList);" in build
    assert "lv_obj_set_size(advancedVirtualContent, 742," in build
    assert "lv_obj_set_flex_flow(advancedList" not in build
    assert "for (size_t i = 0; i < espplants_advanced_virtual_list::kPoolSize; ++i)" in build
    assert "lv_obj_set_pos(row.box, 0, 0);" in build


def test_virtual_geometry_preserves_locked_advanced_layout():
    source = read(MAIN)
    build = function_block(source, "void buildAdvanced(lv_obj_t *screen)", "void buildNav")
    for required in (
        "lv_obj_set_pos(advancedPage, 0, 66);",
        "lv_obj_set_size(advancedPage, 800, 356);",
        "lv_obj_t *p = card(advancedPage, 14, 10, 772, 334);",
        "lv_obj_set_pos(advancedList, 10, 96);",
        "lv_obj_set_size(advancedList, 752, 150);",
        "lv_obj_set_size(row.box, 742, 50);",
        "lv_obj_set_pos(advancedDetail, 18, 263);",
        "lv_obj_set_pos(advancedRenameButton, 478, 255);",
        "lv_obj_set_pos(advancedRemoveButton, 616, 255);",
    ):
        assert required in build


def test_virtual_content_height_and_scroll_binding_are_explicit():
    source = read(MAIN)
    helper = read(VIRTUAL)
    assert "logicalCount * static_cast<size_t>(kStride)" in helper
    assert "- kRowGap" in helper
    assert "firstVisible > 0 ? firstVisible - 1 : 0" in helper
    assert "clampScrollY" in helper
    refresh = function_block(source, "void refreshAdvancedVirtualList", "void advancedListScrollEvent")
    assert "buildInfrastructureSlots(logicalSlots)" in refresh
    assert "lv_obj_set_height(advancedVirtualContent" in refresh
    assert "lv_obj_get_scroll_y(advancedList)" in refresh
    assert "lv_obj_scroll_to_y(advancedList, clamped, LV_ANIM_OFF)" in refresh
    assert "firstPoolLogicalIndex" in refresh
    assert "advancedFirstLogicalIndex" in refresh
    assert "lv_obj_set_pos(row.box, 0," in refresh


def test_row_click_identity_comes_from_reusable_row_binding():
    source = read(MAIN)
    event = function_block(source, "void infrastructureRowEvent", "void infrastructureRenameEvent")
    assert "static_cast<InfrastructureRow *>(lv_event_get_user_data(event))" in event
    assert "row->boundSlot" in event
    assert "reinterpret_cast<intptr_t>(lv_event_get_user_data(event))" not in event
    build = function_block(source, "void buildAdvanced(lv_obj_t *screen)", "void buildNav")
    assert "LV_EVENT_CLICKED, &row" in build


def test_virtual_rows_use_low_churn_label_storage():
    source = read(MAIN)
    row_struct = source.split("struct InfrastructureRow", 1)[1].split("plantlink::Decoder", 1)[0]
    assert "char nameText[kInfrastructureNameBytes]" in row_struct
    assert "char statusText[8]" in row_struct
    assert "char signalText[16]" in row_struct
    assert "lv_label_set_text_static" in source
    assert "staticRowLabel(row.name, row.nameText" in source
    helper = function_block(source, "void staticRowLabel", "#if !defined")
    assert "strcmp(storage, text) == 0" in helper
    assert "lv_label_set_text_static(obj, storage)" in helper


def test_advanced_data_source_capacity_and_management_are_unchanged():
    source = read(MAIN)
    assert "constexpr size_t kMaxInfrastructure = 32;" in source
    assert "InfrastructureNode infrastructure[kMaxInfrastructure];" in source
    assert "saveInfrastructureSlot(static_cast<size_t>(selectedInfrastructure));" in source
    assert "showInfrastructureRemoveConfirm(selectedInfrastructure);" in source
    assert "openInfrastructureRename(selectedInfrastructure);" in source


def test_home_and_all_virtualization_are_not_part_of_stage_b():
    source = read(MAIN)
    assert "PlantListRow rows[kMaxSensors];" in source
    assert "AllSensorRow allRows[kMaxSensors];" in source
