from pathlib import Path
import importlib.util
import json

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware/waveshare-hub/scripts/build_plants_ota.py"
WS_HEADER = ROOT / "firmware/waveshare-hub/include/build_version.h"
H2_HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"
BRIDGE_VERSION = "0.2.0-alpha.23"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def load_packager():
    spec = importlib.util.spec_from_file_location("build_plants_ota_current", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


def current_identities():
    ota = load_packager()
    return ota, ota.read_build_identity(WS_HEADER), ota.read_h2_build_identity(H2_HEADER)


def test_release_versions_come_from_authoritative_build_headers():
    _, waveshare, h2 = current_identities()
    assert read("VERSION").strip() == waveshare.version
    assert waveshare.build_id == f"ESPPLANTS-WAVESHARE-{waveshare.version}"
    assert h2.build_id == f"ESPPLANTS-H2-{h2.version}"


def test_alpha23_bridge_is_explicit_and_does_not_change_normal_h2_target():
    header = read("firmware/m5-h2-zigbee/include/build_version.h")
    pio = read("firmware/m5-h2-zigbee/platformio.ini")
    _, _, h2 = current_identities()
    assert "ESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23" in header
    assert f'#define ESP_PLANTS_H2_VERSION "{BRIDGE_VERSION}"' in header
    assert f'#define ESP_PLANTS_H2_VERSION "{h2.version}"' in header
    assert '#define ESP_PLANTS_H2_BUILD_ID "ESPPLANTS-H2-" ESP_PLANTS_H2_VERSION' in header
    assert "[env:m5_gateway_h2_release_bridge_alpha23]" in pio
    bridge = pio.split("[env:m5_gateway_h2_release_bridge_alpha23]", 1)[1]
    assert "-DESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23=1" in bridge
    assert "-DESP_PLANTS_DISTRIBUTION_BUILD=1" in bridge
    assert "default_envs = m5_gateway_h2" in pio


def test_release_manifest_uses_regular_7_and_current_h2_target():
    ota, identity, h2_identity = current_identities()
    assert identity.hardware == "waveshare-esp32-s3-touch-lcd-7"

    metadata = ota.PackageMetadata(
        package_size=1024,
        package_sha256="00" * 32,
        firmware_size=512,
        firmware_sha256="11" * 32,
        build_id=identity.build_id,
    )
    h2 = {
        "product": h2_identity.product,
        "hardware": h2_identity.hardware,
        "version": h2_identity.version,
        "build_id": h2_identity.build_id,
        "protocol": 1,
        "asset": f"esp-plants-h2-{h2_identity.version}.bin",
        "firmware_size": 729680,
        "firmware_sha256": "22" * 32,
    }
    manifest = json.loads(
        ota.create_manifest(
            identity,
            metadata,
            f"esp-plants-waveshare-{identity.version}.plantsota",
            h2,
        )
    )
    assert manifest["hardware"] == "waveshare-esp32-s3-touch-lcd-7"
    assert manifest["h2"]["version"] == h2_identity.version
    assert manifest["h2"]["asset"] == f"esp-plants-h2-{h2_identity.version}.bin"


def test_new_waveshare_uses_manifest_h2_target_not_compiled_h2_header():
    client = read("firmware/waveshare-hub/src/h2_ota_client.cpp")
    release_header = read("firmware/waveshare-hub/include/plants_ota_installer.h")
    assert "release.h2BuildId" in client
    assert "release.h2Asset" in client
    assert "release.h2FirmwareSize" in client
    assert '../../m5-h2-zigbee/include/build_version.h' not in client
    assert "h2Version" in release_header
    assert "h2Asset" in release_header
    assert "h2BuildId" in release_header


def test_same_waveshare_release_uses_directional_h2_state():
    source = read("firmware/waveshare-hub/src/update_service.cpp")
    assert "espplants_h2_ota::targetStateForRelease(bestRelease)" in source
    assert "TargetState::OLDER_THAN_RELEASE" in source
    assert "TargetState::NEWER_THAN_RELEASE" in source
    assert "TargetState::UNKNOWN" in source
    assert "TargetState::DIFFERENT" not in source
    assert "h2OnlyUpdate = true;" in source
    assert 'setStatus("H2 update available: v%s", bestRelease.h2Version);' in source
    assert "downgrade skipped" in source
    assert "automatic H2 update is blocked" in source
    assert '"Downloading and verifying H2 firmware..."' in source
    assert "espplants_h2_ota::updateForRelease(" in source
    assert "matchingH2Found" in source


def test_manifest_h2_metadata_is_strictly_validated():
    source = read("firmware/waveshare-hub/src/update_service.cpp")
    for token in (
        'h2Product != kH2ProductId',
        'h2Hardware != kH2HardwareId',
        'h2Protocol != plantlink::kProtocolVersion',
        'h2BuildId != expectedH2Build',
        'h2Asset != expectedH2Asset',
        'h2FirmwareSize < kH2MinFirmwareBytes',
        'h2FirmwareSize > kH2MaxFirmwareBytes',
        'hexToBytes(h2FirmwareSha.c_str(), candidate.h2FirmwareSha256',
    ):
        assert token in source


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
