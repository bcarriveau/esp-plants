from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def test_v2_has_single_identity_and_no_bridge_target():
    header = (ROOT / "firmware/m5-h2-zigbee/include/build_version.h").read_text()
    pio = (ROOT / "firmware/m5-h2-zigbee/platformio.ini").read_text()
    assert len(re.findall(r'^#define ESP_PLANTS_H2_VERSION ', header, re.M)) == 1
    assert "COMPAT_BRIDGE" not in header
    assert "release_bridge" not in pio
    assert '#define ESP_PLANTS_H2_BUILD_ID "ESPPLANTS-H2-" ESP_PLANTS_H2_VERSION' in header

def test_binary_identity_is_contiguous_retained_release_data():
    source = (ROOT / "firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp").read_text()
    assert "__attribute__((used, retain)) static const char kFirmwareIdentity[]" in source
    assert 'ESP_PLANTS_H2_BINARY_MARKER "\\0" ESP_PLANTS_H2_BINARY_BUILD_ID' in source
    assert 'ESP_PLANTS_H2_BINARY_BUILD_ID ESP_PLANTS_H2_BUILD_ID' in source
    assert "COMPAT_BRIDGE" not in source
