from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_h2_release_identity_survives_linker_gc():
    receiver = read("firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp")
    assert "__attribute__((used, retain)) static const char kFirmwareIdentity[]" in receiver
    assert 'ESP_PLANTS_H2_BINARY_MARKER "\0" ESP_PLANTS_H2_BINARY_BUILD_ID' in receiver
    assert 'ESP_PLANTS_H2_BINARY_MARKER "ESP-PLANTS-H2-DISTRIBUTION-BUILD"' in receiver
    assert "ESP_PLANTS_H2_BUILD_ID" in receiver


def test_alpha23_bridge_remains_an_explicit_distribution_build():
    pio = read("firmware/m5-h2-zigbee/platformio.ini")
    bridge = pio.split("[env:m5_gateway_h2_release_bridge_alpha23]", 1)[1]
    assert "-DESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23=1" in bridge
    assert "-DESP_PLANTS_DISTRIBUTION_BUILD=1" in bridge


def test_bridge_version_and_current_h2_version_remain_independent():
    header = read("firmware/m5-h2-zigbee/include/build_version.h")
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.23"' in header
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.25"' in header


def test_release_packager_still_requires_marker_and_exact_build_id():
    packager = read("firmware/waveshare-hub/scripts/build_plants_ota.py")
    assert "H2_DISTRIBUTION_MARKER not in data" in packager
    assert "build_id.encode() not in data" in packager
    assert "H2 firmware lacks distribution/build identity" in packager
