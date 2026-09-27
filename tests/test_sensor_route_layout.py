"""Check the route footer preserves the locked 800x480 plant-card layout."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_warning_positions_and_route_footer_reuse_existing_lvgl_object():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")

    # Home warning is intentionally enlarged and moved up to leave room for the
    # small bottom-most sensor-details affordance. Plant-detail geometry is unchanged.
    assert "lv_obj_set_pos(homeWarning, 24, 214);" in source
    assert 'lv_label_set_text(hint, "TAP CARD FOR SENSOR DETAILS");' in source
    assert "lv_obj_set_pos(hint, 24, 288);" in source
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

    # Home featured card: 500x334 at screen (14,76), inherited pad=24.
    # Conservative WATER ME height is 60 px at Montserrat 32 plus padding;
    # hint height is 16 px. Both stay inside the padded card, and there is
    # visible separation before the bottom-most hint.
    assert 24 + 214 + 60 <= 334
    assert 24 + 288 + 16 <= 334
    assert 214 + 60 + 10 <= 288

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

def test_advanced_zigbee_layout_has_explicit_columns_and_bottom_clearance():
    source = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    advanced = source.split("void buildAdvanced(", 1)[1].split("void buildNav", 1)[0]

    # The Advanced card is 772x334 at screen (14,76). Explicit zero padding makes
    # every child bound deterministic instead of relying on inherited theme padding.
    assert "lv_obj_set_style_pad_all(p, 0, 0);" in advanced

    # Headers are independent labels aligned to the row columns.
    for text in (
        'lv_obj_set_pos(repeaterHead, 20, 76);',
        'lv_obj_set_pos(statusHead, 448, 76);',
        'lv_obj_set_pos(signalHead, 588, 76);',
        'lv_obj_set_size(advancedList, 752, 150);',
        'lv_obj_set_pos(advancedDetail, 18, 263);',
        'lv_obj_set_width(advancedDetail, 440);',
        'lv_obj_set_size(advancedRenameButton, 126, 42);',
        'lv_obj_set_pos(advancedRenameButton, 478, 255);',
        'lv_obj_set_size(advancedRemoveButton, 126, 42);',
        'lv_obj_set_pos(advancedRemoveButton, 616, 255);',
    ):
        assert text in advanced

    card_w, card_h = 772, 334
    # Child rectangles: x, y, w, h. Text headers use conservative 18 px height.
    rects = {
        "back": (530, 12, 92, 42),
        "add": (632, 12, 118, 42),
        "repeater_head": (20, 76, 390, 18),
        "status_head": (448, 76, 110, 18),
        "signal_head": (588, 76, 145, 18),
        "list": (10, 96, 752, 150),
        "detail": (18, 263, 440, 18),
        "rename": (478, 255, 126, 42),
        "remove": (616, 255, 126, 42),
    }
    for name, (x, y, w, h) in rects.items():
        assert x >= 0 and y >= 0, name
        assert x + w <= card_w, name
        assert y + h <= card_h, name

    # List clears the management row; management controls clear each other.
    assert 96 + 150 < 255
    assert 18 + 440 < 478
    assert 478 + 126 < 616

    # Card ends at screen y=410 and the locked bottom navigation starts at y=422.
    assert 66 + 10 + card_h == 410
    assert 410 < 422
    # Lowest management control ends at screen y=373, leaving 49 px before nav.
    assert 66 + 10 + 255 + 42 == 373
    assert 373 < 422

