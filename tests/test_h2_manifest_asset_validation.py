from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "firmware/waveshare-hub/include/update_policy.h"
UPDATE = ROOT / "firmware/waveshare-hub/src/update_service.cpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_waveshare_and_h2_assets_use_separate_suffix_validators():
    policy = read(POLICY)
    assert "inline bool assetNameValid(const char *asset)" in policy
    assert 'constexpr char suffix[] = ".plantsota";' in policy
    assert "inline bool h2AssetNameValid(const char *asset)" in policy
    assert 'constexpr char prefix[] = "esp-plants-h2-";' in policy
    assert 'constexpr char suffix[] = ".bin";' in policy


def test_h2_manifest_uses_h2_bin_validator_and_exact_expected_name():
    source = read(UPDATE)
    parser = source[source.index("bool parseManifest(") : source.index("bool checkGithubRelease()")]
    assert 'const String expectedH2Asset = String("esp-plants-h2-") + h2Version + ".bin";' in parser
    assert "h2Asset != expectedH2Asset" in parser
    assert "!h2AssetNameValid(h2Asset.c_str())" in parser
    assert "!assetNameValid(h2Asset.c_str())" not in parser
    assert "h2Product != kH2ProductId" in parser
    assert "h2Hardware != kH2HardwareId" in parser
    assert "h2Protocol != plantlink::kProtocolVersion" in parser
    assert "!lowerHexDigest(h2FirmwareSha.c_str())" in parser


def test_waveshare_package_still_uses_plantsota_validator():
    source = read(UPDATE)
    parser = source[source.index("bool parseManifest(") : source.index("bool checkGithubRelease()")]
    assert "!assetNameValid(asset.c_str())" in parser
