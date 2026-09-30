from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT / "firmware" / "waveshare-hub"


def _project(name):
    return json.loads((WS / name).read_text(encoding="utf-8"))


def _find_identifier(node, identifier):
    if isinstance(node, dict):
        if node.get("identifier") == identifier:
            return node
        for value in node.values():
            found = _find_identifier(value, identifier)
            if found is not None:
                return found
    elif isinstance(node, list):
        for value in node:
            found = _find_identifier(value, identifier)
            if found is not None:
                return found
    return None


def test_save_button_exists_in_both_eez_projects():
    seven = _find_identifier(_project("esp_plants.eez-project"), "detail_preference_save_button")
    seven_b = _find_identifier(_project("esp_plants_7b.eez-project"), "detail_preference_save_button")
    assert seven is not None
    assert seven_b is not None
    assert (seven["left"], seven["top"], seven["width"], seven["height"]) == (650, 252, 92, 34)
    assert (seven_b["left"], seven_b["top"], seven_b["width"], seven_b["height"]) == (832, 315, 118, 43)


def test_generated_ui_exports_save_button_for_both_variants():
    for rel in ("src/ui/screens.h", "src/ui_7b/screens.h"):
        text = (WS / rel).read_text(encoding="utf-8")
        assert "detail_preference_save_button" in text
        assert "detail_preference_save_label" in text


def test_slider_does_not_persist_until_save_clicked():
    text = (WS / "src/main.cpp").read_text(encoding="utf-8")
    slider_start = text.index("void moisturePreferenceEvent")
    save_start = text.index("void saveMoisturePreferenceEvent")
    slider_body = text[slider_start:save_start]
    save_body = text[save_start:text.index("void buildPlant", save_start)]
    assert "saveMoisturePreference(" not in slider_body
    assert "sensor.moisturePreference =" not in slider_body
    assert "saveMoisturePreference(" in save_body
    assert "sensor.moisturePreference = next;" in save_body
    assert "updateMoisturePreferenceSaveState(next != sensor.moisturePreference);" in slider_body


def test_unsaved_preference_is_discarded_when_leaving_plant_page():
    text = (WS / "src/main.cpp").read_text(encoding="utf-8")
    show = text[text.index("void showPage(Page page)"):text.index("void navEvent", text.index("void showPage(Page page)"))]
    assert "previousPage == Page::Plant && page != Page::Plant" in show
    assert "detailPreferencePendingDirty = false" in show
    assert "detailPreferencePendingSensor = -1" in show
