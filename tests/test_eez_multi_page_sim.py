import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware" / "waveshare-hub"

PAGE_NAMES = ["Home", "All Sensors", "Plant Detail", "Settings", "Advanced Zigbee"]
PAGE_CONTENT = ["home_page", "all_page", "plant_page", "settings_page", "advanced_page"]
SCREEN_ROOTS = ["home", "all_sensors", "plant_detail", "settings", "advanced_zigbee"]
SHARED = ["header", "nav_bar", "rename_modal", "pair_modal", "update_modal"]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_both_eez_projects_have_five_real_editable_pages():
    for name, width, height, content_y, content_h in [
        ("esp_plants.eez-project", 800, 480, 66, 356),
        ("esp_plants_7b.eez-project", 1024, 600, 83, 445),
    ]:
        project = json.loads(read(HUB / name))
        pages = project["userPages"]
        assert [page["name"] for page in pages] == PAGE_NAMES
        assert all(page["createAtStart"] is True for page in pages)
        assert all(page["deleteOnScreenUnload"] is False for page in pages)

        for page, content_id in zip(pages, PAGE_CONTENT):
            assert (page["width"], page["height"]) == (width, height)
            assert len(page["components"]) == 1
            screen = page["components"][0]
            assert screen["type"] == "LVGLScreenWidget"
            children = screen["children"]
            content = next(child for child in children if child.get("identifier") == content_id)
            assert (content["left"], content["top"], content["width"], content["height"]) == (
                0, content_y, width, content_h
            )
            assert content.get("hiddenFlag") is not True

        home_children = [c.get("identifier") for c in pages[0]["components"][0]["children"]]
        assert home_children == ["home_page", *SHARED]
        for page in pages[1:]:
            assert len(page["components"][0]["children"]) == 1


def test_generated_ui_has_five_screen_roots_and_real_screen_navigation():
    for folder in ["ui", "ui_7b"]:
        header = read(HUB / "src" / folder / "screens.h")
        screens = read(HUB / "src" / folder / "screens.c")
        ui = read(HUB / "src" / folder / "ui.c")

        for index, root in enumerate(SCREEN_ROOTS, 1):
            assert f"lv_obj_t *{root};" in header
            assert f"SCREEN_ID_{root.upper()} = {index}" in header
            assert f"void create_screen_{root}();" in header
            assert f"void create_screen_{root}()" in screens
            assert f"objects.{root} = obj;" in screens

        assert "screen_index < 5" in screens
        assert screens.count("create_screen_home();") == 1
        assert screens.count("create_screen_all_sensors();") == 1
        assert screens.count("create_screen_plant_detail();") == 1
        assert screens.count("create_screen_settings();") == 1
        assert screens.count("create_screen_advanced_zigbee();") == 1

        assert "static enum ScreensEnum simScreenId(SimPage page)" in ui
        assert "case SIM_PAGE_ALL: return SCREEN_ID_ALL_SENSORS;" in ui
        assert "case SIM_PAGE_PLANT: return SCREEN_ID_PLANT_DETAIL;" in ui
        assert "case SIM_PAGE_SETTINGS: return SCREEN_ID_SETTINGS;" in ui
        assert "case SIM_PAGE_ADVANCED: return SCREEN_ID_ADVANCED_ZIGBEE;" in ui
        assert "loadScreen(simScreenId(page));" in ui
        assert "simSetHidden(objects.all_page" not in ui


def test_full_sim_starts_populated_and_previews_every_runtime_page():
    for folder in ["ui", "ui_7b"]:
        ui = read(HUB / "src" / folder / "ui.c")
        assert "#define SIM_SENSOR_CAPACITY 12" in ui
        assert "#define SIM_INFRASTRUCTURE_CAPACITY 6" in ui
        assert "simCreateRows();" in ui
        assert "simCreateInfrastructureRows();" in ui
        assert "simUpdateHome();" in ui
        assert "simUpdateAll();" in ui
        assert "simUpdatePlant();" in ui
        assert "simUpdateSettings();" in ui
        assert "simUpdateAdvanced();" in ui
        assert '"MONSTERA"' in ui
        assert '"SNAKE PLANT"' in ui
        assert '"LIVING ROOM ROUTER"' in ui
        assert '"PAGE PREVIEW"' in ui
        for page in ["SIM_PAGE_HOME", "SIM_PAGE_ALL", "SIM_PAGE_PLANT", "SIM_PAGE_SETTINGS", "SIM_PAGE_ADVANCED"]:
            assert page in ui


def test_shared_chrome_is_single_instance_and_survives_screen_switches():
    main = read(HUB / "src" / "main.cpp")
    assert "lv_obj_t *screenForPage(Page page)" in main
    assert "return objects.all_sensors;" in main
    assert "return objects.plant_detail;" in main
    assert "return objects.settings;" in main
    assert "return objects.advanced_zigbee;" in main
    assert "void attachSharedUiLayer()" in main
    assert "lv_obj_set_parent(objects.header, top);" in main
    assert "lv_obj_set_parent(objects.nav_bar, top);" in main
    assert "lv_obj_set_parent(objects.update_modal, top);" in main
    assert "create_screens();" in main
    assert "buildAll(objects.all_sensors);" in main
    assert "buildPlant(objects.plant_detail);" in main
    assert "buildSettings(objects.settings);" in main
    assert "buildAdvanced(objects.advanced_zigbee);" in main
    assert "lv_obj_add_flag(allPage, LV_OBJ_FLAG_HIDDEN)" not in main


def test_appearance_rebuild_deletes_all_five_screens_before_recreating():
    main = read(HUB / "src" / "main.cpp")
    for root in SCREEN_ROOTS:
        assert f"if (objects.{root}) lv_obj_del(objects.{root});" in main
    appearance = main[main.index("void applyAppearanceAsync"): main.index("void appearanceEvent")]
    assert "lv_scr_load(transition);" in appearance
    assert "deleteGeneratedUiTree();" in appearance
    assert "buildUi(restorePage);" in appearance
    assert appearance.index("deleteGeneratedUiTree();") < appearance.index("buildUi(restorePage);")


def test_eez_ui_templates_match_generated_full_sim_sources():
    for project_name, folder in [
        ("esp_plants.eez-project", "ui"),
        ("esp_plants_7b.eez-project", "ui_7b"),
    ]:
        project = json.loads(read(HUB / project_name))
        template = next(f["template"] for f in project["settings"]["build"]["files"] if f["fileName"] == "ui.c")
        assert template == read(HUB / "src" / folder / "ui.c")


def test_platformio_keeps_bill_waveshare_7_upload_port():
    pio = read(HUB / "platformio.ini")
    section = pio.split("[env:waveshare_s3_touch_lcd_7]", 1)[1].split("[env:", 1)[0]
    assert re.search(r"(?m)^upload_port\s*=\s*COM11\s*$", section)
