from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware/waveshare-hub/scripts/build_plants_ota.py"
WS_HEADER = ROOT / "firmware/waveshare-hub/include/build_version.h"
H2_HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def load_packager():
    spec = importlib.util.spec_from_file_location("build_plants_ota_current", SCRIPT)
    ota = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(ota)
    return ota


def test_current_waveshare_and_h2_release_identities_are_independent():
    ota = load_packager()
    waveshare = ota.read_build_identity(WS_HEADER)
    h2 = ota.read_h2_build_identity(H2_HEADER)
    assert read("VERSION").strip() == waveshare.version
    assert waveshare.product == "esp-plants-waveshare"
    assert h2.product == "esp-plants-h2"
    assert waveshare.build_id == f"ESPPLANTS-WAVESHARE-{waveshare.version}"
    assert h2.build_id == f"ESPPLANTS-H2-{h2.version}"


def test_release_generator_cannot_misidentify_regular_7_as_7b():
    ota = load_packager()
    identity = ota.read_build_identity(WS_HEADER)
    assert identity.hardware == "waveshare-esp32-s3-touch-lcd-7"


def test_historical_incompatible_manifest_does_not_replace_live_status():
    source = read("firmware/waveshare-hub/src/update_service.cpp")
    parse_start = source.index("bool parseManifest(")
    scan_start = source.index("bool checkGithubRelease()", parse_start)
    parser = source[parse_start:scan_start]

    assert "bool &identityMismatch" in parser
    assert "identityMismatch = true;" in parser
    assert "Skipping historical release %s: incompatible product/hardware/channel/updater" in parser
    assert 'setStatus("Release manifest is incompatible with this ESP PLANTS display")' not in parser

    assert "if (!identityMismatch) manifestRejected = true;" in source
    assert 'setStatus("Published Waveshare release manifest was rejected; nothing installed")' in source
    assert 'setStatus("Up to date: v%s", ESP_PLANTS_WAVESHARE_VERSION)' in source
