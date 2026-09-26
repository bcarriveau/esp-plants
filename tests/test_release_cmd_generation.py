from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMD = ROOT / "tools" / "make-waveshare-release.cmd"


def test_release_cmd_explicitly_runs_packager_after_builds():
    source = CMD.read_text(encoding="utf-8")
    assert "Packaging Waveshare + H2 release assets explicitly..." in source
    assert 'firmware\\waveshare-hub\\scripts\\build_plants_ota.py' in source
    assert 'waveshare_s3_touch_lcd_7_release\\firmware.bin' in source
    assert 'm5_gateway_h2_release\\firmware.bin' in source


def test_release_cmd_verifies_all_four_current_release_assets():
    source = CMD.read_text(encoding="utf-8")
    required = (
        'release\\esp-plants-waveshare-%RELEASE_VERSION%.plantsota',
        'release\\esp-plants-waveshare.manifest.json',
        'release\\esp-plants-h2-%H2_VERSION%.bin',
        'release\\esp-plants-h2-%H2_VERSION%.bin.sha256',
    )
    for path in required:
        assert f'if not exist "{path}" goto package_failed' in source
    assert "Verified generated files in release\\:" in source


def test_release_cmd_does_not_claim_success_before_packaging():
    source = CMD.read_text(encoding="utf-8")
    packager = source.index('"%PYTHON%" "firmware\\waveshare-hub\\scripts\\build_plants_ota.py"')
    success = source.index("Waveshare release v%RELEASE_VERSION% complete")
    assert packager < success
