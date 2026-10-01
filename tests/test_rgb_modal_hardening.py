import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware" / "waveshare-hub"
MAIN = (HUB / "src" / "main.cpp").read_text()
DRIVER_H = (HUB / "lib" / "Waveshare_ST7262_LVGL" / "src" / "Waveshare_ST7262_LVGL.h").read_text()
DRIVER_CPP = (HUB / "lib" / "Waveshare_ST7262_LVGL" / "src" / "Waveshare_ST7262_LVGL.cpp").read_text()
FLASH_CPP = (HUB / "src" / "runtime_flash_guard.cpp").read_text()
PLATFORMIO = (HUB / "platformio.ini").read_text()


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    in_string = False
    escaped = False
    for i in range(brace, len(source)):
        ch = source[i]
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[brace : i + 1]
    raise AssertionError(f"unterminated function {signature}")


def find_identifier(node, identifier):
    if isinstance(node, dict):
        if node.get("identifier") == identifier:
            return node
        for value in node.values():
            result = find_identifier(value, identifier)
            if result is not None:
                return result
    elif isinstance(node, list):
        for value in node:
            result = find_identifier(value, identifier)
            if result is not None:
                return result
    return None


def assert_generated_modal_clickable(source: str, identifier: str):
    marker = f"// {identifier}"
    start = source.index(marker)
    clear = source.index("lv_obj_clear_flag(obj,", start)
    line = source[clear : source.index(";", clear)]
    assert "LV_OBJ_FLAG_CLICKABLE" not in line
    assert "LV_OBJ_FLAG_SCROLLABLE" in line


def test_ports_and_runtime_nvs_guard_are_preserved():
    assert "upload_port = COM11" in PLATFORMIO
    assert "upload_port = COM13" in PLATFORMIO
    assert "-Wl,--wrap=nvs_commit" in PLATFORMIO
    assert "-DCONFIG_SPIRAM" not in PLATFORMIO
    assert "__wrap_nvs_commit" in FLASH_CPP
    assert "__real_nvs_commit" in FLASH_CPP
    assert "markDisplayReady" in FLASH_CPP
    assert "restart_rgb_panel_scan()" in FLASH_CPP
    assert "#ifdef ESP_PLANTS_WAVESHARE_7B" in FLASH_CPP


def test_7b_only_uses_double_buffer_direct_mode():
    expected = """#ifdef ESP_PLANTS_WAVESHARE_7B\n#define LVGL_PORT_AVOID_TEARING_MODE (3)\n#else\n#define LVGL_PORT_AVOID_TEARING_MODE (2)\n#endif"""
    assert expected in DRIVER_H
    assert "#define LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE (LVGL_PORT_DISP_WIDTH * 10)" in DRIVER_H
    assert "CONFIG_SPIRAM_FETCH_INSTRUCTIONS" in DRIVER_CPP
    assert "CONFIG_SPIRAM_RODATA" in DRIVER_CPP
    assert "CONFIG_ESP32S3_DATA_CACHE_LINE_64B" in DRIVER_CPP


def test_ui_callbacks_defer_preferences_writes():
    for signature in [
        "void brightnessEvent(",
        "void unitEvent(",
        "void saveRename()",
        "void renameKeyboardEvent(",
        "void saveMoisturePreferenceEvent(",
    ]:
        body = function_body(MAIN, signature)
        assert "preferences.put" not in body
        assert "preferences.remove" not in body
    assert "serviceDeferredPersistence()" in MAIN
    assert "kUiPersistenceDelayMs = 1500" in MAIN
    assert "serviceBrightnessPersistence" not in MAIN


def test_runtime_display_recovery_is_serviced_after_display_ready_and_ota_idle():
    setup = function_body(MAIN, "void setup()")
    loop = function_body(MAIN, "void loop()")
    assert setup.index("lcd_init();") < setup.index("espplants_runtime_flash::markDisplayReady();")
    assert "serviceDeferredPersistence();" in loop
    assert 'requestDisplayRecovery("OTA worker idle")' in loop
    assert loop.count("espplants_runtime_flash::serviceDisplayRecovery();") >= 2


def test_eez_modal_roots_block_touches_and_forget_has_fullscreen_scrim():
    for name, width, height in [
        ("esp_plants.eez-project", 800, 480),
        ("esp_plants_7b.eez-project", 1024, 600),
    ]:
        project = json.loads((HUB / name).read_text())
        for identifier in [
            "rename_modal",
            "pair_modal",
            "update_modal",
            "release_notes_modal",
            "wifi_forget_confirm",
        ]:
            obj = find_identifier(project, identifier)
            assert obj is not None
            assert obj.get("clickableFlag") is True
            assert "CLICKABLE" in obj.get("widgetFlags", "").split("|")
            assert "SCROLLABLE" not in obj.get("widgetFlags", "").split("|")

        update = find_identifier(project, "update_modal")
        scrim = find_identifier(project, "wifi_forget_scrim")
        confirm = find_identifier(project, "wifi_forget_confirm")
        assert scrim is not None and confirm is not None
        assert (scrim["left"], scrim["top"], scrim["width"], scrim["height"]) == (0, 0, width, height)
        assert scrim.get("hiddenFlag") is True
        assert scrim.get("clickableFlag") is True
        assert scrim["localStyles"]["definition"]["MAIN"]["DEFAULT"]["bg_opa"] == 0
        child_ids = [child.get("identifier") for child in update["children"]]
        assert child_ids.index("wifi_forget_scrim") < child_ids.index("wifi_forget_confirm")


def test_generated_ui_matches_modal_hardening_and_simulator_stack():
    for subdir, width, height in [("ui", 800, 480), ("ui_7b", 1024, 600)]:
        screens = (HUB / "src" / subdir / "screens.c").read_text()
        header = (HUB / "src" / subdir / "screens.h").read_text()
        sim = (HUB / "src" / subdir / "ui.c").read_text()
        for identifier in ["rename_modal", "pair_modal", "update_modal", "release_notes_modal", "wifi_forget_confirm"]:
            assert_generated_modal_clickable(screens, identifier)
        assert "objects.wifi_forget_scrim = obj;" in screens
        assert f"lv_obj_set_size(obj, {width}, {height});" in screens[screens.index("// wifi_forget_scrim"):screens.index("// wifi_forget_confirm")]
        assert "lv_obj_set_style_bg_opa(obj, 0" in screens[screens.index("// wifi_forget_scrim"):screens.index("// wifi_forget_confirm")]
        assert "lv_obj_t *wifi_forget_scrim;" in header
        assert "lv_obj_t *settings_brightness_button;" in header
        assert "simOpenModal(objects.wifi_forget_scrim);" in sim
        assert "simSetHidden(objects.wifi_forget_scrim, true);" in sim
        assert "simCloseForgetConfirmEvent" in sim


def test_firmware_forget_confirmation_raises_and_hides_scrim_with_card():
    ask = function_body(MAIN, "void wifiForgetAskEvent(")
    cancel = function_body(MAIN, "void wifiForgetCancelEvent(")
    confirm = function_body(MAIN, "void wifiForgetConfirmEvent(")
    close = function_body(MAIN, "void closeUpdateEvent(")
    assert ask.index("lv_obj_move_foreground(wifiForgetScrim)") < ask.index("lv_obj_move_foreground(wifiForgetConfirm)")
    for body in [cancel, confirm, close]:
        assert "wifiForgetScrim" in body and "LV_OBJ_FLAG_HIDDEN" in body
