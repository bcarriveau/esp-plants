import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware/waveshare-hub"
PACKAGER = HUB / "scripts/build_plants_ota.py"

spec = importlib.util.spec_from_file_location("build_plants_ota_7b_test", PACKAGER)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def identifiers(project: dict) -> list[str]:
    found = []

    def walk(value):
        if isinstance(value, dict):
            identifier = value.get("identifier")
            if identifier:
                found.append(identifier)
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    walk(project)
    return found


def test_7b_eez_project_is_separate_1024x600_and_preserves_identifiers():
    seven_path = HUB / "esp_plants.eez-project"
    seven_b_path = HUB / "esp_plants_7b.eez-project"
    seven = json.loads(read(seven_path))
    seven_b = json.loads(read(seven_b_path))
    general = seven_b["settings"]["general"]
    build = seven_b["settings"]["build"]
    assert general["displayWidth"] == 1024
    assert general["displayHeight"] == 600
    assert build["destinationFolder"].replace("\\", "/") == "src/ui_7b"
    assert identifiers(seven_b) == identifiers(seven)
    assert len(identifiers(seven_b)) >= 100
    assert (HUB / "src/ui_7b/screens.c").is_file()
    assert (HUB / "src/ui_7b/screens.h").is_file()
    assert (HUB / "src/ui_7b/ui.c").is_file()


def test_platformio_has_release_and_diag_7b_without_changing_com11():
    pio = read(HUB / "platformio.ini")
    assert "upload_port = COM11" in pio
    assert "[env:waveshare_s3_touch_lcd_7b_release]" in pio
    assert "extends = env:waveshare_s3_touch_lcd_7b" in pio
    assert "post:scripts/build_plants_ota.py" in pio
    assert "-DESP_PLANTS_DISTRIBUTION_BUILD=1" in pio
    assert "[env:waveshare_s3_touch_lcd_7b_diag]" in pio
    assert "-DESP_PLANTS_DIAGNOSTIC_PANIC=1" in pio
    assert "-<ui_7b/>" in pio
    assert "-<ui/>" in pio


def test_build_header_and_packager_select_hardware_per_variant():
    header = read(HUB / "include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID "waveshare-esp32-s3-touch-lcd-7b"' in header
    assert "ESP_PLANTS_WAVESHARE_HARDWARE_ID" in header
    seven = ota.read_build_identity(HUB / "include/build_version.h", "7")
    seven_b = ota.read_build_identity(HUB / "include/build_version.h", "7b")
    assert seven.hardware == "waveshare-esp32-s3-touch-lcd-7"
    assert seven_b.hardware == "waveshare-esp32-s3-touch-lcd-7b"
    assert ota.package_asset_name(seven) != ota.package_asset_name(seven_b)


def test_release_cmd_builds_and_packages_both_variants():
    script = read(ROOT / "tools/make-waveshare-release.cmd")
    assert "waveshare_s3_touch_lcd_7_release" in script
    assert "waveshare_s3_touch_lcd_7b_release" in script
    assert "--variant 7" in script
    assert "--variant 7b" in script
    assert "--combine-existing" in script
    assert "esp-plants-waveshare-7-%RELEASE_VERSION%.plantsota" in script
    assert "esp-plants-waveshare-7b-%RELEASE_VERSION%.plantsota" in script


def test_update_client_selects_current_hardware_from_schema2_variants():
    policy = read(HUB / "include/update_policy.h")
    service = read(HUB / "src/update_service.cpp")
    installer = read(HUB / "src/plants_ota_installer.cpp")
    assert "kManifestSchema = 1U" in policy
    assert "kLegacyManifestSchema" not in policy
    assert 'doc["variants"]' in service
    assert "ESP_PLANTS_WAVESHARE_HARDWARE_ID" in service
    assert "ESP_PLANTS_WAVESHARE_HARDWARE_ID" in installer
    assert "WAVESHARE-ESP32-S3-LCD-7" not in installer


def test_lvgl_init_guard_and_7b_backlight_protocol_are_explicit():
    port = read(HUB / "lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp")
    board = read(HUB / "include/waveshare_panel_board_7b.h")
    assert "if (!lv_is_initialized())" in port
    assert "lv_init();" in port
    assert "ESP_PLANTS_7B_IO_PWM_REG" in port
    assert "ESP_PLANTS_7B_BACKLIGHT_IO" in port
    assert "Wire.begin" in port
    assert "ESP_PLANTS_7B_IO_EXTENSION_ADDR (0x24)" in board
    assert "ESP_PLANTS_7B_IO_PWM_REG (0x05)" in board
    assert "ESP_PLANTS_7B_BACKLIGHT_IO (2)" in board
