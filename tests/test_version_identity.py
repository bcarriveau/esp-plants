from pathlib import Path
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[1]
WAVESHARE_HEADER = ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h"
H2_HEADER = ROOT / "firmware" / "m5-h2-zigbee" / "include" / "build_version.h"
SCRIPT = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"

spec = importlib.util.spec_from_file_location("build_plants_ota", SCRIPT)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


def test_root_version_tracks_waveshare_release_identity():
    waveshare = ota.read_build_identity(WAVESHARE_HEADER)
    assert (ROOT / "VERSION").read_text(encoding="utf-8").strip() == waveshare.version


def test_waveshare_and_h2_are_valid_independent_release_identities():
    waveshare = ota.read_build_identity(WAVESHARE_HEADER)
    h2 = ota.read_h2_build_identity(H2_HEADER)

    semver_alpha = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$")

    assert semver_alpha.fullmatch(waveshare.version)
    assert semver_alpha.fullmatch(h2.version)

    assert waveshare.product == "esp-plants-waveshare"
    assert h2.product == "esp-plants-h2"

    assert waveshare.hardware == "waveshare-esp32-s3-touch-lcd-7"
    assert waveshare.build_id == f"ESPPLANTS-WAVESHARE-{waveshare.version}"
    assert h2.build_id == f"ESPPLANTS-H2-{h2.version}"

    # The two products are deliberately versioned independently.
    # This test validates identity construction rather than pinning a historical alpha number.


def test_release_manifest_uses_regular_waveshare_7_hardware_identity():
    waveshare = ota.read_build_identity(WAVESHARE_HEADER)
    metadata = ota.PackageMetadata(
        package_size=1024,
        package_sha256="00" * 32,
        firmware_size=512,
        firmware_sha256="11" * 32,
        build_id=waveshare.build_id,
    )
    h2 = {
        "product": "esp-plants-h2",
        "hardware": "m5stack-unit-gateway-h2",
        "version": "0.2.0-alpha.24",
        "build_id": "ESPPLANTS-H2-0.2.0-alpha.24",
        "protocol": 1,
        "asset": "esp-plants-h2-0.2.0-alpha.24.bin",
        "firmware_size": 123,
        "firmware_sha256": "22" * 32,
    }
    manifest = json.loads(
        ota.create_manifest(
            waveshare,
            metadata,
            f"esp-plants-waveshare-{waveshare.version}.plantsota",
            h2,
        )
    )
    assert manifest["hardware"] == "waveshare-esp32-s3-touch-lcd-7"
    assert manifest["hardware"] != "waveshare-esp32-s3-touch-lcd-7b"


def test_build_header_keeps_runtime_7b_identity_separate_from_release_7_identity():
    header = WAVESHARE_HEADER.read_text(encoding="utf-8")
    assert '#define ESP_PLANTS_WAVESHARE_7_HARDWARE_ID "waveshare-esp32-s3-touch-lcd-7"' in header
    assert '#define ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID "waveshare-esp32-s3-touch-lcd-7b"' in header
    assert '#define ESP_PLANTS_WAVESHARE_HARDWARE_ID ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID' in header
    assert '#define ESP_PLANTS_WAVESHARE_HARDWARE_ID ESP_PLANTS_WAVESHARE_7_HARDWARE_ID' in header


def test_crash_diagnostics_uses_waveshare_build_identity():
    # This guard is intended for a full checkout. The delivery ZIP only carries
    # files changed by this task, so skip the source-preservation check there.
    source_path = ROOT / "firmware" / "waveshare-hub" / "src" / "crash_diagnostics.cpp"
    if not source_path.exists():
        return
    source = source_path.read_text(encoding="utf-8")

    assert '#include "build_version.h"' in source
    assert "ESP_PLANTS_WAVESHARE_BUILD_ID" in source
    assert "alpha.11-diag1" not in source
