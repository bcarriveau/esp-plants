"""Check the route footer preserves the locked 800x480 plant-card layout."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_warning_positions_and_route_footer_reuse_existing_lvgl_object():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")

    # Alpha.38 warning geometry stays untouched.
    assert "lv_obj_set_pos(homeWarning, 24, 278);" in source
    assert "lv_obj_set_pos(detailWarning, 600, 272);" in source
    assert "lv_obj_align(homeWarning, LV_ALIGN_TOP_MID" not in source
    assert "lv_obj_align(detailWarning, LV_ALIGN_TOP_MID" not in source

    # The alpha.39/40 extra LVGL route objects are gone.  Route status reuses
    # the existing detailUpdated footer instead of growing the object tree.
    assert "homeRoute" not in source
    assert "detailRoute" not in source
    assert "routeLabel(" not in source
    assert "lv_obj_set_width(detailUpdated, 540);" in source
    assert "lv_label_set_long_mode(detailUpdated, LV_LABEL_LONG_CLIP);" in source
    assert '"%s  |  %s"' in source
    assert "sensor_route_view::format(s.route, infrastructure" in source

    # Parent card: 772x334 at screen (14,76), inherited pad=24.
    # detailUpdated child x=24 width=540 => content right=564; WATER ME starts x=600.
    # detailUpdated y=276 and WATER ME y=272 remain within card; nav begins at y=422.
    assert 24 + 540 < 600
    assert 24 + 276 + 18 <= 334
    assert 76 + 334 < 422


def test_topology_refresh_cannot_refresh_measurements_or_pair_sensors():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    handler = source.split("void handleSensorReport(", 1)[1].split("void clearRoutes", 1)[0]
    branch = handler.split("if (frame.flags & plantlink::FlagRouteOnly)", 1)[1].split("acceptPairingSensor", 1)[0]
    assert "report.fieldFlags == 0" in branch
    assert "s->route.update" in branch and "return;" in branch
    assert "&& s->route.update(report)" in branch
    assert "lastSeenMs" not in branch and "seenThisBoot = true" not in branch
    for record in ("PersistedPlant", "PersistedInfrastructure"):
        fields = source.split(f"struct {record} {{", 1)[1].split("};", 1)[0]
        assert "route" not in fields.lower()
