from pathlib import Path
import importlib.util

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


def test_waveshare_and_h2_are_independently_versioned():
    waveshare = ota.read_build_identity(WAVESHARE_HEADER)
    h2 = ota.read_h2_build_identity(H2_HEADER)
    assert waveshare.version == "0.2.0-alpha.31"
    assert h2.version == "0.2.0-alpha.21"
    assert waveshare.build_id == "ESPPLANTS-WAVESHARE-0.2.0-alpha.31"
    assert h2.build_id == "ESPPLANTS-H2-0.2.0-alpha.21"


def test_crash_diagnostics_uses_waveshare_build_identity():
    source = (ROOT / "firmware" / "waveshare-hub" / "src" / "crash_diagnostics.cpp").read_text(encoding="utf-8")
    assert '#include "build_version.h"' in source
    assert "ESP_PLANTS_WAVESHARE_BUILD_ID" in source
    assert "alpha.11-diag1" not in source
