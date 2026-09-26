from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"
RECEIVER = ROOT / "firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp"
PIO = ROOT / "firmware/m5-h2-zigbee/platformio.ini"


def test_bridge_header_has_explicit_alpha23_build_identity():
    source = HEADER.read_text(encoding="utf-8")
    bridge = source.split("#ifdef ESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23", 1)[1].split("#else", 1)[0]
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.23"' in bridge
    assert '#define ESP_PLANTS_H2_BUILD_ID "ESPPLANTS-H2-0.2.0-alpha.23"' in bridge


def test_normal_h2_identity_is_alpha25():
    source = HEADER.read_text(encoding="utf-8")
    normal = source.split("#else", 1)[1].split("#endif", 1)[0]
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.25"' in normal
    assert '#define ESP_PLANTS_H2_BUILD_ID "ESPPLANTS-H2-0.2.0-alpha.25"' in normal


def test_bridge_environment_still_selects_compatibility_variant():
    source = PIO.read_text(encoding="utf-8")
    bridge = source.split("[env:m5_gateway_h2_release_bridge_alpha23]", 1)[1]
    assert "-DESP_PLANTS_DISTRIBUTION_BUILD=1" in bridge
    assert "-DESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23=1" in bridge


def test_binary_identity_is_contiguous_retained_release_data():
    source = RECEIVER.read_text(encoding="utf-8")
    assert "__attribute__((used, retain)) static const char kFirmwareIdentity[]" in source
    assert 'ESP_PLANTS_H2_BINARY_MARKER "\0" ESP_PLANTS_H2_BINARY_BUILD_ID' in source
    assert '#define ESP_PLANTS_H2_BINARY_MARKER "ESP-PLANTS-H2-DISTRIBUTION-BUILD"' in source
    assert '#define ESP_PLANTS_H2_BINARY_BUILD_ID "ESPPLANTS-H2-0.2.0-alpha.23"' in source
