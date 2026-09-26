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


def test_centered_warning_and_route_bounds():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    badge_width = font_width(20, "WATER ME!") + 28
    assert badge_width == 146
    # LVGL large-display card padding = 24; card origin on screen = (14,76).
    for card_width, route_x, route_width, badge in (
        (500, 312, 140, "homeWarning"), (772, 500, 224, "detailWarning")
    ):
        content_width = card_width - 48
        badge_right = (content_width + badge_width) // 2
        assert badge_right < route_x
        assert route_x + route_width <= content_width
        assert 24 + 272 + 22 + 16 <= 334  # badge, including vertical padding
        assert 24 + 292 + 15 <= 334       # route fixed height
        assert 76 + 334 < 422             # navigation starts at 422
        assert f"lv_obj_align({badge}, LV_ALIGN_TOP_MID, 0, 272);" in source
    assert "homeRoute = routeLabel(featured, 312, 292, 140);" in source
    assert "detailRoute = routeLabel(p, 500, 292, 224);" in source
    assert font_width(12, "VIA: KITCHEN REPEATER") <= 224
    assert "lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);" in source


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
