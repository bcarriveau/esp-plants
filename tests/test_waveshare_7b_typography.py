from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
SEVEN = ROOT / "firmware/waveshare-hub/esp_plants.eez-project"
SEVEN_B = ROOT / "firmware/waveshare-hub/esp_plants_7b.eez-project"


def font_points(value: str) -> int:
    return int(value.rsplit("_", 1)[1])


def collect_fonts(node, path="", in_keyboard=False, out=None):
    if out is None:
        out = {}
    if isinstance(node, dict):
        in_keyboard = in_keyboard or node.get("identifier") == "rename_keyboard"
        for key, value in node.items():
            here = f"{path}.{key}" if path else key
            if key == "text_font" and isinstance(value, str):
                out[here] = (value, in_keyboard)
            else:
                collect_fonts(value, here, in_keyboard, out)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            collect_fonts(value, f"{path}[{index}]", in_keyboard, out)
    return out


def test_7b_ui_fonts_are_larger_but_keyboard_is_unchanged():
    seven = collect_fonts(json.loads(SEVEN.read_text(encoding="utf-8")))
    seven_b = collect_fonts(json.loads(SEVEN_B.read_text(encoding="utf-8")))
    assert seven.keys() == seven_b.keys()

    compared = 0
    keyboard = 0
    for path, (font7, is_keyboard) in seven.items():
        font7b, is_keyboard_7b = seven_b[path]
        assert is_keyboard == is_keyboard_7b
        if is_keyboard:
            keyboard += 1
            assert font7b == font7
        else:
            compared += 1
            assert font_points(font7b) > font_points(font7), (path, font7, font7b)

    assert compared >= 100
    assert keyboard == 1


def test_7b_generated_ui_and_font_config_match_new_sizes():
    screens = (ROOT / "firmware/waveshare-hub/src/ui_7b/screens.c").read_text(encoding="utf-8")
    lv_conf = (ROOT / "firmware/waveshare-hub/include/lv_conf.h").read_text(encoding="utf-8")
    assert "&lv_font_montserrat_48" in screens
    assert "&lv_font_montserrat_36" in screens
    assert "lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_ITEMS | LV_STATE_DEFAULT);" in screens
    assert "#define LV_FONT_MONTSERRAT_36 1" in lv_conf
    assert "#define LV_FONT_MONTSERRAT_48 1" in lv_conf
