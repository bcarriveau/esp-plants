from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMD = ROOT / "tools" / "make-waveshare-release.cmd"


def test_release_cmd_builds_bridge_target_current_h2_and_waveshare():
    source = CMD.read_text(encoding="utf-8")
    assert "m5_gateway_h2_release_bridge_alpha23" in source
    assert "m5_gateway_h2_release" in source
    assert "waveshare_s3_touch_lcd_7_release" in source
    assert source.index("m5_gateway_h2_release_bridge_alpha23") < source.index(
        '"%PIO%" run -d firmware\\m5-h2-zigbee -e m5_gateway_h2_release\n'
    )


def test_release_cmd_explicitly_runs_packager_after_builds():
    source = CMD.read_text(encoding="utf-8")
    assert "Packaging Waveshare + H2 release assets explicitly..." in source
    assert 'firmware\\waveshare-hub\\scripts\\build_plants_ota.py' in source
    assert 'waveshare_s3_touch_lcd_7_release\\firmware.bin' in source
    assert '--h2-bridge-firmware' in source
    assert '--h2-bridge-version "%H2_BRIDGE_VERSION%"' in source


def test_release_cmd_verifies_all_six_transition_release_assets():
    source = CMD.read_text(encoding="utf-8")
    required = (
        'release\\esp-plants-waveshare-%RELEASE_VERSION%.plantsota',
        'release\\esp-plants-waveshare.manifest.json',
        'release\\esp-plants-h2-%H2_VERSION%.bin',
        'release\\esp-plants-h2-%H2_VERSION%.bin.sha256',
        'release\\esp-plants-h2-%H2_BRIDGE_VERSION%.bin',
        'release\\esp-plants-h2-%H2_BRIDGE_VERSION%.bin.sha256',
    )
    for path in required:
        assert f'if not exist "{path}" goto package_failed' in source
    assert "Verified generated files in release\\:" in source


def test_release_cmd_does_not_claim_success_before_packaging():
    source = CMD.read_text(encoding="utf-8")
    packager = source.index('"%PYTHON%" "firmware\\waveshare-hub\\scripts\\build_plants_ota.py"')
    success = source.index("Waveshare release v%RELEASE_VERSION% complete")
    assert packager < success
