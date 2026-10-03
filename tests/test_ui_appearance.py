import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware/waveshare-hub"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def walk_identifier(value, identifier):
    if isinstance(value, dict):
        if value.get("identifier") == identifier:
            return value
        for child in value.values():
            found = walk_identifier(child, identifier)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = walk_identifier(child, identifier)
            if found is not None:
                return found
    return None


def build_template(project, name):
    for entry in project["settings"]["build"]["files"]:
        if entry.get("fileName") == name:
            return entry["template"]
    raise AssertionError(f"missing {name} template")


def test_runtime_appearance_is_persisted_with_hardware_specific_defaults():
    main = read(HUB / "src/main.cpp")
    assert "constexpr bool kDefaultLightAppearance = true;" in main
    assert "constexpr bool kDefaultLightAppearance = false;" in main
    assert 'preferences.getBool("ui_light", kDefaultLightAppearance)' in main
    assert 'preferences.putBool("ui_light", espplants_ui_is_light())' in main
    assert "kPersistAppearance = 1U << 4" in main
    assert 'light ? "LIGHT" : "DARK"' in main
    assert "lv_async_call(applyAppearanceAsync, nullptr);" in main
    assert "lv_obj_t *transition = lv_obj_create(nullptr);" in main
    assert "if (oldScreen && oldScreen != transition) lv_obj_del(oldScreen);" in main
    assert main.index("lv_obj_del(oldScreen)") < main.index("buildUi(restorePage);")
    assert "buildUi(restorePage);" in main


def test_shared_palette_has_card_semantics_and_bidirectional_mapping():
    header = read(HUB / "src/ui_appearance.h")
    source = read(HUB / "src/ui_appearance.cpp")
    assert "ESP_PLANTS_UI_CARD_DARK_TOKEN" in header
    assert "ESP_PLANTS_UI_CARD_LIGHT_TOKEN" in header
    assert "render_light ? 0xF3F8F4U : 0x18231DU" in header
    assert "espplants_ui_dark_to_light_hex" in header
    assert "espplants_ui_light_to_dark_hex" in header
    assert "espplants_ui_resolve_source_hex" in header
    assert "bool lightAppearance = false;" in source
    assert "espplants_ui_source_color" in source


def test_both_eez_projects_expose_appearance_control_without_moving_existing_layout():
    configs = [
        ("esp_plants.eez-project", 800, 480, "#18231C", 242, 296, 110, 30),
        ("esp_plants_7b.eez-project", 1024, 600, "#F3F8F3", 310, 370, 141, 38),
    ]
    card_ids = {
        "home_featured_card",
        "your_plants_card",
        "all_card",
        "plant_card",
        "settings_system_card",
        "settings_setup_card",
        "advanced_card",
        "update_network_card",
        "update_software_card",
    }
    for name, width, height, card_token, x, y, w, h in configs:
        project = json.loads(read(HUB / name))
        assert project["settings"]["general"]["displayWidth"] == width
        assert project["settings"]["general"]["displayHeight"] == height
        button = walk_identifier(project, "settings_appearance_button")
        label = walk_identifier(project, "settings_appearance")
        caption = walk_identifier(project, "settings_appearance_caption")
        assert button is not None and label is not None and caption is not None
        assert (button["left"], button["top"], button["width"], button["height"]) == (x, y, w, h)
        for card_id in card_ids:
            card = walk_identifier(project, card_id)
            assert card is not None
            assert card["localStyles"]["definition"]["MAIN"]["DEFAULT"]["bg_color"] == card_token


def test_generated_ui_routes_static_colors_through_runtime_palette_and_wires_button():
    pairs = [
        ("ui", "0"),
        ("ui_7b", "1"),
    ]
    for folder, source_light in pairs:
        screens = read(HUB / f"src/{folder}/screens.c")
        header = read(HUB / f"src/{folder}/screens.h")
        ui = read(HUB / f"src/{folder}/ui.c")
        assert '#include "../ui_appearance.h"' in screens
        assert f"#define ESP_PLANTS_UI_EEZ_SOURCE_LIGHT {source_light}" in screens
        assert "espplants_ui_source_color" in screens
        assert "settings_appearance_button" in screens
        assert "settings_appearance" in header
        assert "simAppearanceEvent" in ui
        assert "settings_appearance_button, simAppearanceEvent" in ui
        assert 'simLightAppearance ? "LIGHT" : "DARK"' in ui
        assert ui.count("static void simUpdateViews(void);") == 1


def test_eez_templates_match_generated_runtime_palette_bridge():
    for name, source_light in [("esp_plants.eez-project", "0"), ("esp_plants_7b.eez-project", "1")]:
        project = json.loads(read(HUB / name))
        screens = build_template(project, "screens.c")
        ui = build_template(project, "ui.c")
        assert '#include "../ui_appearance.h"' in screens
        assert f"#define ESP_PLANTS_UI_EEZ_SOURCE_LIGHT {source_light}" in screens
        assert "espplants_ui_source_color" in screens
        assert f"#define ESP_PLANTS_UI_EEZ_SOURCE_LIGHT {source_light}" in ui
        assert "simAppearanceEvent" in ui
        assert ui.count("static void simUpdateViews(void);") == 1


def test_platformio_preserves_bill_7_upload_port():
    platformio = read(HUB / "platformio.ini")
    seven_env = platformio.split("[env:waveshare_s3_touch_lcd_7]", 1)[1].split("[env:", 1)[0]
    assert re.search(r"(?m)^upload_port\s*=\s*COM11\s*$", seven_env)
