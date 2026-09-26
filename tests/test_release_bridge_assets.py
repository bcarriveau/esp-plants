from pathlib import Path
import importlib.util
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"

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
    repo = tmp_path / "repo"
    ws_header = repo / "firmware/waveshare-hub/include/build_version.h"
    h2_header = repo / "firmware/m5-h2-zigbee/include/build_version.h"
    ws_bin = repo / "firmware/waveshare-hub/.pio/build/waveshare_s3_touch_lcd_7_release/firmware.bin"
    h2_bin = repo / "firmware/m5-h2-zigbee/.pio/build/m5_gateway_h2_release/firmware.bin"
    bridge_bin = repo / "firmware/m5-h2-zigbee/.pio/build/m5_gateway_h2_release_bridge_alpha23/firmware.bin"
    for path in (ws_header, h2_header, ws_bin, h2_bin, bridge_bin):
        path.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy(ROOT / "firmware/waveshare-hub/include/build_version.h", ws_header)
    shutil.copy(ROOT / "firmware/m5-h2-zigbee/include/build_version.h", h2_header)

    ws_bin.write_bytes(
        fake_image(
            "ESPPLANTS-WAVESHARE-0.2.0-alpha.36",
            b"ESP-PLANTS-DISTRIBUTION-BUILD",
            esp32s3=True,
        )
    )
    h2_bin.write_bytes(
        fake_image("ESPPLANTS-H2-0.2.0-alpha.24", b"ESP-PLANTS-H2-DISTRIBUTION-BUILD")
    )
    bridge_bin.write_bytes(
        fake_image("ESPPLANTS-H2-0.2.0-alpha.23", b"ESP-PLANTS-H2-DISTRIBUTION-BUILD")
    )

    release = repo / "release"
    ota.write_release_assets(
        ws_bin,
        ws_header,
        release,
        bridge_bin,
        "0.2.0-alpha.23",
    )

    expected = {
        "esp-plants-waveshare-0.2.0-alpha.36.plantsota",
        "esp-plants-waveshare.manifest.json",
        "esp-plants-h2-0.2.0-alpha.24.bin",
        "esp-plants-h2-0.2.0-alpha.24.bin.sha256",
        "esp-plants-h2-0.2.0-alpha.23.bin",
        "esp-plants-h2-0.2.0-alpha.23.bin.sha256",
    }
    assert expected.issubset({path.name for path in release.iterdir()})

    manifest = json.loads((release / "esp-plants-waveshare.manifest.json").read_text())
    assert manifest["version"] == "0.2.0-alpha.36"
    assert manifest["hardware"] == "waveshare-esp32-s3-touch-lcd-7"
    assert manifest["h2"]["version"] == "0.2.0-alpha.24"
    assert manifest["h2"]["asset"] == "esp-plants-h2-0.2.0-alpha.24.bin"
    assert "0.2.0-alpha.23" not in json.dumps(manifest)
