import importlib.util
import json
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "firmware" / "waveshare-hub" / "scripts" / "build_plants_ota.py"
WS_HEADER = ROOT / "firmware/waveshare-hub/include/build_version.h"
H2_HEADER = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"
spec = importlib.util.spec_from_file_location("build_plants_ota", SCRIPT)
ota = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(ota)


class PlantsOtaPackageTests(unittest.TestCase):
    def identity(self, variant="7"):
        return ota.read_build_identity(WS_HEADER, variant)

    def h2_identity(self):
        return ota.read_h2_build_identity(H2_HEADER)

    def firmware(self, *, variant="7", chip_id=9, marker=True, build=True, hardware=True):
        identity = self.identity(variant)
        data = bytearray(b"\xff" * (80 * 1024))
        data[0] = 0xE9
        struct.pack_into("<H", data, 12, chip_id)
        if marker:
            value = ota.DISTRIBUTION_FIRMWARE_MARKER
            data[1024 : 1024 + len(value)] = value
        if build:
            value = identity.build_id.encode("ascii")
            data[2048 : 2048 + len(value)] = value
        if hardware:
            value = identity.hardware.encode("ascii")
            data[3072 : 3072 + len(value)] = value
        return bytes(data)

    def h2_metadata(self):
        identity = self.h2_identity()
        return {
            "product": identity.product,
            "hardware": identity.hardware,
            "version": identity.version,
            "build_id": identity.build_id,
            "protocol": 2,
            "asset": f"esp-plants-h2-{identity.version}.bin",
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
        asset = ota.package_asset_name(identity)
        manifest = json.loads(
            ota.create_manifest(identity, metadata, asset, self.h2_metadata())
        )
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["product"], "esp-plants-waveshare")
        self.assertEqual(len(manifest["variants"]), 1)
        variant = manifest["variants"][0]
        self.assertEqual(variant["hardware"], "waveshare-esp32-s3-touch-lcd-7")
        self.assertEqual(variant["asset"], asset)
        self.assertEqual(variant["package_size"], variant["firmware_size"] + 512)
        self.assertEqual(manifest["h2"]["version"], self.h2_identity().version)

        fields = ota.HEADER_STRUCT.unpack_from(package)
        self.assertEqual(fields[3].split(b"\0", 1)[0].decode(), identity.hardware)

    def test_7b_package_uses_7b_hardware_identity_and_distinct_filename(self):
        identity = self.identity("7b")
        package = ota.create_package(self.firmware(variant="7b"), identity)
        fields = ota.HEADER_STRUCT.unpack_from(package)
        self.assertEqual(
            fields[3].split(b"\0", 1)[0].decode(),
            "waveshare-esp32-s3-touch-lcd-7b",
        )
        self.assertEqual(
            ota.package_asset_name(identity),
            f"esp-plants-waveshare-7b-{identity.version}.plantsota",
        )

    def test_dual_variant_manifest_lists_both_hardware_targets(self):
        seven = self.identity("7")
        seven_b = self.identity("7b")
        meta7 = ota.metadata(ota.create_package(self.firmware(variant="7"), seven), seven)
        meta7b = ota.metadata(
            ota.create_package(self.firmware(variant="7b"), seven_b), seven_b
        )
        manifest = json.loads(
            ota.create_multi_manifest(
                [
                    (seven, meta7, ota.package_asset_name(seven)),
                    (seven_b, meta7b, ota.package_asset_name(seven_b)),
                ],
                self.h2_metadata(),
            )
        )
        by_hw = {entry["hardware"]: entry for entry in manifest["variants"]}
        self.assertEqual(
            set(by_hw),
            {
                "waveshare-esp32-s3-touch-lcd-7",
                "waveshare-esp32-s3-touch-lcd-7b",
            },
        )
        self.assertNotEqual(by_hw[seven.hardware]["asset"], by_hw[seven_b.hardware]["asset"])

    def test_wrong_chip_rejected(self):
        with self.assertRaisesRegex(ValueError, "not ESP32-S3"):
            ota.create_package(self.firmware(chip_id=0), self.identity())

    def test_private_build_rejected(self):
        with self.assertRaisesRegex(ValueError, "distribution/build identity"):
            ota.create_package(self.firmware(marker=False), self.identity())

    def test_missing_build_id_rejected(self):
        with self.assertRaisesRegex(ValueError, "distribution/build identity"):
            ota.create_package(self.firmware(build=False), self.identity())

    def test_missing_hardware_id_rejected(self):
        with self.assertRaisesRegex(ValueError, "distribution/build identity"):
            ota.create_package(self.firmware(hardware=False), self.identity())

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
