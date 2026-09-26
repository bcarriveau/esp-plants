"""Check actual bundled LVGL font metrics against the locked 800x480 cards."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "firmware/waveshare-hub/.pio/libdeps/waveshare_s3_touch_lcd_7/lvgl/src/font"


def font_width(px, text):
    source = (FONT_DIR / f"lv_font_montserrat_{px}.c").read_text(encoding="utf-8")
    advances = list(map(int, re.findall(r"\.adv_w = (\d+)", source)))

    def array(name):
        body = source.split(name + "[] = {", 1)[1].split("}", 1)[0]
        return list(map(int, re.findall(r"-?\d+", body)))

    left = array("kern_left_class_mapping")
    right = array("kern_right_class_mapping")
    pairs = array("kern_class_values")
    count = int(re.search(r"\.right_class_cnt\s*=\s*(\d+)", source)[1])
    scale = int(re.search(r"\.kern_scale\s*=\s*(\d+)", source)[1])
    total = 0
    for index, char in enumerate(text):
        glyph = ord(char) - 31  # cmap format0: ASCII 32 starts at glyph 1
        following = ord(text[index + 1]) - 31 if index + 1 < len(text) else 0
        kern = pairs[(left[glyph] - 1) * count + right[following] - 1] if left[glyph] and right[following] else 0
        total += (advances[glyph] + ((kern * scale) >> 4) + 8) >> 4
    return total


def test_warning_positions_are_restored_and_route_bounds_are_separate():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    badge_width = font_width(20, "WATER ME!") + 28
    assert badge_width == 146

    # Existing alpha.38 warning geometry is locked. Route labels fit around it.
    assert "lv_obj_set_pos(homeWarning, 24, 278);" in source
    assert "lv_obj_set_pos(detailWarning, 600, 272);" in source
    assert "lv_obj_align(homeWarning, LV_ALIGN_TOP_MID" not in source
    assert "lv_obj_align(detailWarning, LV_ALIGN_TOP_MID" not in source

    home_route_x, home_route_width = 312, 140
    detail_route_x, detail_route_width = 350, 220
    assert 24 + badge_width < home_route_x
    assert detail_route_x + detail_route_width < 600
    assert home_route_x + home_route_width <= 500
    assert detail_route_x + detail_route_width <= 772
    assert 292 + 15 <= 334
    assert 76 + 334 < 422  # card ends before navigation begins

    assert "homeRoute = routeLabel(featured, 312, 292, 140);" in source
    assert "detailRoute = routeLabel(p, 350, 292, 220);" in source
    assert "lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);" in source
    assert "labelIfChanged(homeRoute, routeText);" in source
    assert "labelIfChanged(detailRoute, routeText);" in source
    assert 'label(homeRoute, "");' not in source
    assert 'label(detailRoute, "");' not in source


def test_topology_refresh_cannot_refresh_measurements_or_pair_sensors():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    handler = source.split("void handleSensorReport(", 1)[1].split("void clearRoutes", 1)[0]
    branch = handler.split("if (frame.flags & plantlink::FlagRouteOnly)", 1)[1].split("acceptPairingSensor", 1)[0]
    assert "report.fieldFlags == 0" in branch
    assert "s->route.update" in branch and "return;" in branch
    assert "lastSeenMs" not in branch and "seenThisBoot = true" not in branch
    for record in ("PersistedPlant", "PersistedInfrastructure"):
        fields = source.split(f"struct {record} {{", 1)[1].split("};", 1)[0]
        assert "route" not in fields.lower()
