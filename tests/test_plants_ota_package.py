import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"
spec = importlib.util.spec_from_file_location("build_plants_ota", SCRIPT)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


class PlantsOtaPackageTests(unittest.TestCase):
    def identity(self):
        return ota.read_build_identity(
            ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h"
        )

    def firmware(self, *, chip_id=9, marker=True, build=True):
        identity = self.identity()
        data = bytearray(b"\xff" * (80 * 1024))
        data[0] = 0xE9
        struct.pack_into("<H", data, 12, chip_id)
        if marker:
            value = ota.DISTRIBUTION_FIRMWARE_MARKER
            data[1024 : 1024 + len(value)] = value
        if build:
            value = identity.build_id.encode("ascii")
            data[2048 : 2048 + len(value)] = value
        return bytes(data)

    def test_package_round_trip(self):
        identity = self.identity()
        package = ota.create_package(self.firmware(), identity)
        metadata = ota.validate_package(package, identity)
        self.assertEqual(metadata.package_size, len(package))
        self.assertEqual(metadata.firmware_size, len(self.firmware()))
        manifest = json.loads(
            ota.create_manifest(identity, metadata, ota.versioned_package_name(identity))
        )
        self.assertEqual(manifest["hardware"], "waveshare-esp32-s3-touch-lcd-7")
        self.assertEqual(manifest["product"], "esp-plants-waveshare")
        self.assertTrue(manifest["asset"].endswith(".plantsota"))
        self.assertEqual(manifest["package_size"], manifest["firmware_size"] + 512)

    def test_wrong_chip_rejected(self):
        with self.assertRaisesRegex(ValueError, "not ESP32-S3"):
            ota.create_package(self.firmware(chip_id=0), self.identity())

    def test_private_build_rejected(self):
        with self.assertRaisesRegex(ValueError, "DISTRIBUTION"):
            ota.create_package(self.firmware(marker=False), self.identity())

    def test_missing_build_id_rejected(self):
        with self.assertRaisesRegex(ValueError, "build ID"):
            ota.create_package(self.firmware(build=False), self.identity())

    def test_tamper_rejected(self):
        identity = self.identity()
        package = bytearray(ota.create_package(self.firmware(), identity))
        package[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            ota.validate_package(bytes(package), identity)

    def test_release_assets_have_fixed_manifest_name(self):
        identity = self.identity()
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            firmware = td / "firmware.bin"
            firmware.write_bytes(self.firmware())
            asset, manifest, metadata = ota.write_release_assets(
                firmware,
                ROOT / "firmware" / "waveshare-hub" / "include" / "build_version.h",
                td / "release",
            )
            self.assertEqual(manifest.name, ota.MANIFEST_ASSET_NAME)
            self.assertEqual(asset.name, ota.versioned_package_name(identity))
            self.assertEqual(asset.stat().st_size, metadata.package_size)


if __name__ == "__main__":
    unittest.main()
