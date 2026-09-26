from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def current_h2_version() -> str:
    header = read("firmware/m5-h2-zigbee/include/build_version.h")
    matches = re.findall(r'^#define ESP_PLANTS_H2_VERSION "([^"]+)"$', header, re.MULTILINE)
    assert len(matches) == 1
    return matches[-1]


def test_h2_release_identity_survives_linker_gc():
    receiver = read("firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp")
    assert "__attribute__((used, retain)) static const char kFirmwareIdentity[]" in receiver
    assert 'ESP_PLANTS_H2_BINARY_MARKER "\\0" ESP_PLANTS_H2_BINARY_BUILD_ID' in receiver
    assert 'ESP_PLANTS_H2_BINARY_MARKER "ESP-PLANTS-H2-DISTRIBUTION-BUILD"' in receiver
    assert "ESP_PLANTS_H2_BUILD_ID" in receiver


def test_v2_release_has_no_compatibility_identity():
    pio = read("firmware/m5-h2-zigbee/platformio.ini")
    header = read("firmware/m5-h2-zigbee/include/build_version.h")
    assert "release_bridge" not in pio
    assert "COMPAT_BRIDGE" not in header
    assert f'#define ESP_PLANTS_H2_VERSION "{current_h2_version()}"' in header


def test_release_packager_still_requires_marker_and_exact_build_id():
    packager = read("firmware/waveshare-hub/scripts/build_plants_ota.py")
    assert "H2_DISTRIBUTION_MARKER not in data" in packager
    assert "build_id.encode() not in data" in packager
    assert "H2 firmware lacks distribution/build identity" in packager
