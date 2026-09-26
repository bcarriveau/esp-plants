from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RECEIVER = ROOT / "firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp"
HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"


def source() -> str:
    return RECEIVER.read_text(encoding="utf-8")


def test_identity_scan_is_binary_safe_not_c_string_search():
    text = source()
    start = text.index("bool containsBytes(")
    end = text.index("bool consumeOtaAuthorization(", start)
    scanner = text[start:end]
    assert "memcmp(" in scanner
    assert "strstr(" not in scanner
    assert "uint8_t combined" in scanner


def test_identity_scan_keeps_tail_for_cross_chunk_matches():
    text = source()
    scanner = text[text.index("void observeIdentity(") : text.index("bool consumeOtaAuthorization(")]
    assert "ota.tailLen" in scanner
    assert "memcpy(combined,ota.tail,keep)" in scanner
    assert "combined+total-ota.tailLen" in scanner


def test_h2_current_release_is_derived_and_bridge_stays_alpha23():
    header = HEADER.read_text(encoding="utf-8")
    matches = re.findall(r'^#define ESP_PLANTS_H2_VERSION "([^"]+)"$', header, re.MULTILINE)
    assert len(matches) >= 2
    assert matches[0] == "0.2.0-alpha.23"
    assert matches[-1] != matches[0]
    assert '#define ESP_PLANTS_H2_BUILD_ID "ESPPLANTS-H2-" ESP_PLANTS_H2_VERSION' in header
