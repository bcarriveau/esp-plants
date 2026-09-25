from pathlib import Path
import importlib.util
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

    assert waveshare.build_id == f"ESPPLANTS-WAVESHARE-{waveshare.version}"
    assert h2.build_id == f"ESPPLANTS-H2-{h2.version}"

    # The two products are deliberately versioned independently.
    # This test validates identity construction rather than pinning a historical alpha number.


def test_crash_diagnostics_uses_waveshare_build_identity():
    source = (
        ROOT / "firmware" / "waveshare-hub" / "src" / "crash_diagnostics.cpp"
    ).read_text(encoding="utf-8")

    assert '#include "build_version.h"' in source
    assert "ESP_PLANTS_WAVESHARE_BUILD_ID" in source
    assert "alpha.11-diag1" not in source
