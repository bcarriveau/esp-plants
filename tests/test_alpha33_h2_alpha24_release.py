from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_current_waveshare_release_keeps_h2_alpha25_independent():
    script_path = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"
    spec = importlib.util.spec_from_file_location("build_plants_ota_current", script_path)
    ota = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(ota)
    waveshare = ota.read_build_identity(
        ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h"
    )
    h2 = ota.read_h2_build_identity(
        ROOT / "firmware" / "m5-h2-zigbee" / "include" / "build_version.h"
    )
    assert read("VERSION").strip() == waveshare.version
    assert h2.version == "0.2.0-alpha.25"
    assert waveshare.version != h2.version


def test_release_generator_cannot_misidentify_regular_7_as_7b():
    script_path = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"
    spec = importlib.util.spec_from_file_location("build_plants_ota_identity", script_path)
    ota = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(ota)
    identity = ota.read_build_identity(
        ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h"
    )
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
