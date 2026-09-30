from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMD = ROOT / "tools" / "make-waveshare-release.cmd"


def test_release_cmd_builds_only_current_h2_and_waveshare():
    source = CMD.read_text(encoding="utf-8")
    assert "bridge" not in source.lower()
    assert "m5_gateway_h2_release" in source
    assert "waveshare_s3_touch_lcd_7_release" in source


def test_release_cmd_explicitly_runs_packager_after_builds():
    source = CMD.read_text(encoding="utf-8")
    assert "Packaging both Waveshare variants + H2 release assets explicitly..." in source
    assert "waveshare_s3_touch_lcd_7b_release" in source
    assert "--variant 7b" in source
    assert "--combine-existing" in source
    assert 'firmware\\waveshare-hub\\scripts\\build_plants_ota.py' in source
    assert 'waveshare_s3_touch_lcd_7_release\\firmware.bin' in source


def test_release_cmd_verifies_both_controller_release_assets():
    source = CMD.read_text(encoding="utf-8")
    required = (
        'release\\esp-plants-waveshare-7-%RELEASE_VERSION%.plantsota',
        'release\\esp-plants-waveshare-7b-%RELEASE_VERSION%.plantsota',
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


def test_release_cmd_bootstraps_normal_h2_only_after_release_failure_then_retries():
    source = CMD.read_text(encoding="utf-8")
    first_release = source.index('"%PIO%" run -d firmware\\m5-h2-zigbee -e m5_gateway_h2_release')
    bootstrap = source.index('"%PIO%" run -d firmware\\m5-h2-zigbee -e m5_gateway_h2', first_release + 1)
    retry_release = source.index('"%PIO%" run -d firmware\\m5-h2-zigbee -e m5_gateway_h2_release', bootstrap + 1)
    waveshare = source.index(":build_waveshare")
    assert first_release < bootstrap < retry_release < waveshare
    assert "if not errorlevel 1 goto build_waveshare" in source
