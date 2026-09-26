from pathlib import Path
import importlib.util
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"
WS_HEADER = ROOT / "firmware/waveshare-hub/include/build_version.h"
H2_HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"
BRIDGE_VERSION = "0.2.0-alpha.23"

spec = importlib.util.spec_from_file_location("build_plants_ota_bridge_test", SCRIPT)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


def fake_image(build_id: str, marker: bytes, esp32s3: bool = False) -> bytes:
    data = bytearray(b"\0" * (70 * 1024))
    data[0] = 0xE9
    if esp32s3:
        data[12:14] = (9).to_bytes(2, "little")
    identity = marker + b"\0" + build_id.encode("ascii") + b"\0"
    data[1024 : 1024 + len(identity)] = identity
    return bytes(data)


def test_transition_packager_emits_bridge_and_current_h2_assets(tmp_path: Path):
    current_ws = ota.read_build_identity(WS_HEADER)
    current_h2 = ota.read_h2_build_identity(H2_HEADER)

    repo = tmp_path / "repo"
    ws_header = repo / "firmware/waveshare-hub/include/build_version.h"
    h2_header = repo / "firmware/m5-h2-zigbee/include/build_version.h"
    ws_bin = repo / "firmware/waveshare-hub/.pio/build/waveshare_s3_touch_lcd_7_release/firmware.bin"
    h2_bin = repo / "firmware/m5-h2-zigbee/.pio/build/m5_gateway_h2_release/firmware.bin"
    bridge_bin = repo / "firmware/m5-h2-zigbee/.pio/build/m5_gateway_h2_release_bridge_alpha23/firmware.bin"
    for path in (ws_header, h2_header, ws_bin, h2_bin, bridge_bin):
        path.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy(WS_HEADER, ws_header)
    shutil.copy(H2_HEADER, h2_header)

    ws_bin.write_bytes(
        fake_image(
            current_ws.build_id,
            b"ESP-PLANTS-DISTRIBUTION-BUILD",
            esp32s3=True,
        )
    )
    h2_bin.write_bytes(
        fake_image(current_h2.build_id, b"ESP-PLANTS-H2-DISTRIBUTION-BUILD")
    )
    bridge_bin.write_bytes(
        fake_image(
            f"ESPPLANTS-H2-{BRIDGE_VERSION}",
            b"ESP-PLANTS-H2-DISTRIBUTION-BUILD",
        )
    )

    release = repo / "release"
    ota.write_release_assets(
        ws_bin,
        ws_header,
        release,
        bridge_bin,
        BRIDGE_VERSION,
    )

    expected = {
        f"esp-plants-waveshare-{current_ws.version}.plantsota",
        "esp-plants-waveshare.manifest.json",
        f"esp-plants-h2-{current_h2.version}.bin",
        f"esp-plants-h2-{current_h2.version}.bin.sha256",
        f"esp-plants-h2-{BRIDGE_VERSION}.bin",
        f"esp-plants-h2-{BRIDGE_VERSION}.bin.sha256",
    }
    assert expected.issubset({path.name for path in release.iterdir()})

    manifest = json.loads((release / "esp-plants-waveshare.manifest.json").read_text())
    assert manifest["version"] == current_ws.version
    assert manifest["hardware"] == "waveshare-esp32-s3-touch-lcd-7"
    assert manifest["h2"]["version"] == current_h2.version
    assert manifest["h2"]["asset"] == f"esp-plants-h2-{current_h2.version}.bin"
    assert BRIDGE_VERSION not in json.dumps(manifest)
