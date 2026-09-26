import importlib.util
import json
import struct
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

    def h2_metadata(self):
        return {
            "product": "esp-plants-h2",
            "hardware": "m5stack-unit-gateway-h2",
            "version": "0.2.0-alpha.25",
            "build_id": "ESPPLANTS-H2-0.2.0-alpha.25",
            "protocol": 1,
            "asset": "esp-plants-h2-0.2.0-alpha.25.bin",
            "firmware_size": 729616,
            "firmware_sha256": "22" * 32,
        }

    def test_package_round_trip_metadata(self):
        identity = self.identity()
        firmware = self.firmware()
        package = ota.create_package(firmware, identity)
        metadata = ota.metadata(package, identity)
        self.assertEqual(metadata.package_size, len(package))
        self.assertEqual(metadata.firmware_size, len(firmware))
        manifest = json.loads(
            ota.create_manifest(
                identity,
                metadata,
                f"esp-plants-waveshare-{identity.version}.plantsota",
                self.h2_metadata(),
            )
        )
        self.assertEqual(manifest["hardware"], "waveshare-esp32-s3-touch-lcd-7")
        self.assertEqual(manifest["product"], "esp-plants-waveshare")
        self.assertTrue(manifest["asset"].endswith(".plantsota"))
        self.assertEqual(manifest["package_size"], manifest["firmware_size"] + 512)
        self.assertEqual(manifest["h2"]["version"], "0.2.0-alpha.25")

    def test_wrong_chip_rejected(self):
        with self.assertRaisesRegex(ValueError, "not ESP32-S3"):
            ota.create_package(self.firmware(chip_id=0), self.identity())

    def test_private_build_rejected(self):
        with self.assertRaisesRegex(ValueError, "distribution/build identity"):
            ota.create_package(self.firmware(marker=False), self.identity())

    def test_missing_build_id_rejected(self):
        with self.assertRaisesRegex(ValueError, "distribution/build identity"):
            ota.create_package(self.firmware(build=False), self.identity())

    def test_package_metadata_sha_detects_tamper(self):
        identity = self.identity()
        package = bytearray(ota.create_package(self.firmware(), identity))
        clean = ota.metadata(bytes(package), identity)
        package[-1] ^= 0x01
        tampered = ota.metadata(bytes(package), identity)
        self.assertNotEqual(clean.package_sha256, tampered.package_sha256)
        self.assertNotEqual(clean.firmware_sha256, tampered.firmware_sha256)


if __name__ == "__main__":
    unittest.main()
