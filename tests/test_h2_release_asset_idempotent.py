from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware/waveshare-hub/scripts/build_plants_ota.py"
H2_HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"

spec = importlib.util.spec_from_file_location("build_plants_ota_h2_idempotent", SCRIPT)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


def fake_h2_image(build_id: str) -> bytes:
    data = bytearray(b"\0" * (70 * 1024))
    data[0] = 0xE9
    identity = ota.H2_DISTRIBUTION_MARKER + b"\0" + build_id.encode("ascii") + b"\0"
    data[1024 : 1024 + len(identity)] = identity
    return bytes(data)


def test_identical_h2_release_asset_is_reused_without_rewrite(tmp_path: Path, monkeypatch):
    identity = ota.read_h2_build_identity(H2_HEADER)
    firmware = tmp_path / "firmware.bin"
    release = tmp_path / "release"
    release.mkdir()
    firmware.write_bytes(fake_h2_image(identity.build_id))

    first = ota.write_h2_asset(firmware, release, identity.version, identity.hardware, identity.product, 2)
    output = release / first["asset"]
    digest_path = release / f"{first['asset']}.sha256"
    original_write_bytes = Path.write_bytes
    original_write_text = Path.write_text

    def guarded_write_bytes(self, data):
        if self == output:
            raise AssertionError("identical H2 binary was rewritten")
        return original_write_bytes(self, data)

    def guarded_write_text(self, data, *args, **kwargs):
        if self == digest_path:
            raise AssertionError("identical H2 digest was rewritten")
        return original_write_text(self, data, *args, **kwargs)

    monkeypatch.setattr(Path, "write_bytes", guarded_write_bytes)
    monkeypatch.setattr(Path, "write_text", guarded_write_text)
    second = ota.write_h2_asset(firmware, release, identity.version, identity.hardware, identity.product, 2)
    assert second == first


def test_changed_h2_release_asset_is_replaced(tmp_path: Path):
    identity = ota.read_h2_build_identity(H2_HEADER)
    firmware = tmp_path / "firmware.bin"
    release = tmp_path / "release"
    release.mkdir()
    image = fake_h2_image(identity.build_id)
    firmware.write_bytes(image)
    result = ota.write_h2_asset(firmware, release, identity.version, identity.hardware, identity.product, 2)
    output = release / result["asset"]
    output.write_bytes(b"stale")
    ota.write_h2_asset(firmware, release, identity.version, identity.hardware, identity.product, 2)
    assert output.read_bytes() == image
