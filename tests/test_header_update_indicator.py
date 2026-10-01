import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware" / "waveshare-hub"
MAIN = (HUB / "src" / "main.cpp").read_text(encoding="utf-8")


def find_identifier(node, identifier):
    if isinstance(node, dict):
        if node.get("identifier") == identifier:
            return node
        for value in node.values():
            found = find_identifier(value, identifier)
            if found is not None:
                return found
    elif isinstance(node, list):
        for value in node:
            found = find_identifier(value, identifier)
            if found is not None:
                return found
    return None


def project(name):
    return json.loads((HUB / name).read_text(encoding="utf-8"))


def test_radar_style_header_update_indicator_7():
    button = find_identifier(project("esp_plants.eez-project"), "header_update_button")
    assert button is not None
    assert (button["left"], button["top"], button["width"], button["height"]) == (649, 7, 27, 30)
    assert button["hiddenFlag"] is True
    style = button["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    assert style["bg_color"] == "#12583A"
    assert style["border_color"] == "#3FFF9B"
    assert style["border_width"] == 1
    assert style["radius"] == 6
    assert style["shadow_width"] == 0
    assert all(style[key] == 0 for key in ("pad_left", "pad_right", "pad_top", "pad_bottom"))
    icon = button["children"][0]
    assert icon["text"] == "\uf019"
    icon_style = icon["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    assert icon_style["text_font"] == "MONTSERRAT_16"
    assert icon_style["text_color"] == "#B4FFCD"


def test_radar_style_header_update_indicator_7b():
    button = find_identifier(project("esp_plants_7b.eez-project"), "header_update_button")
    assert button is not None
    assert (button["left"], button["top"], button["width"], button["height"]) == (831, 9, 35, 38)
    assert button["hiddenFlag"] is True
    style = button["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    assert style["bg_color"] == "#12583A"
    assert style["border_color"] == "#3FFF9B"
    assert style["border_width"] == 1
    assert style["radius"] == 8
    icon = button["children"][0]
    assert icon["text"] == "\uf019"
    icon_style = icon["localStyles"]["definition"]["MAIN"]["DEFAULT"]
    assert icon_style["text_font"] == "MONTSERRAT_20"
    assert icon_style["text_color"] == "#B4FFCD"


def test_generated_ui_matches_eez_indicator():
    seven = (HUB / "src" / "ui" / "screens.c").read_text(encoding="utf-8")
    seven_b = (HUB / "src" / "ui_7b" / "screens.c").read_text(encoding="utf-8")
    for source, geometry, font, radius in (
        (seven, "lv_obj_set_pos(obj, 649, 7);", "lv_font_montserrat_16", "lv_obj_set_style_radius(obj, 6"),
        (seven_b, "lv_obj_set_pos(obj, 831, 9);", "lv_font_montserrat_20", "lv_obj_set_style_radius(obj, 8"),
    ):
        anchor = source.index("// header_update_button")
        end = source.index("// header_count", anchor)
        block = source[anchor:end]
        assert geometry in block
        assert "lv_color_hex(0x12583a)" in block
        assert "lv_color_hex(0x3fff9b)" in block
        assert "lv_color_hex(0xb4ffcd)" in block
        assert font in block
        assert radius in block
        assert 'lv_label_set_text_static(obj, "\\xEF\\x80\\x99");' in block
        assert '"UPDATE"' not in block


def test_existing_update_behavior_and_click_target_are_preserved():
    assert "headerUpdateButton = objects.header_update_button;" in MAIN
    assert "lv_obj_add_event_cb(headerUpdateButton, openUpdateEvent, LV_EVENT_CLICKED, nullptr);" in MAIN
    assert "lv_obj_add_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);" in MAIN
    assert "if (espplants_update::updateAvailable())" in MAIN
    assert "lv_obj_clear_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);" in MAIN
