from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import NamedTuple

PACKAGE_MAGIC = b"ESP-PLANTS-OTA"
PACKAGE_HARDWARE_ID = b"WAVESHARE-ESP32-S3-LCD-7"
PACKAGE_PRODUCT_ID = b"ESP-PLANTS-WAVESHARE"
FORMAT_VERSION = 1
HEADER_SIZE = 512
ESP_IMAGE_MAGIC = 0xE9
ESP32_S3_CHIP_ID = 9
HEADER_STRUCT = struct.Struct("<16sHH32s32s96sI32s296s")
MANIFEST_ASSET_NAME = "esp-plants-waveshare.manifest.json"
MAX_MANIFEST_BYTES = 2048
DISTRIBUTION_FIRMWARE_MARKER = b"ESP-PLANTS-DISTRIBUTION-BUILD"
H2_DISTRIBUTION_MARKER = b"ESP-PLANTS-H2-DISTRIBUTION-BUILD"
DISTRIBUTION_BUILD_FLAG = "ESP_PLANTS_DISTRIBUTION_BUILD"
MIN_FIRMWARE_BYTES = 64 * 1024
MAX_PACKAGE_BYTES = 7 * 1024 * 1024
H2_MAX_BYTES = 0xE0000


class BuildIdentity(NamedTuple):
    version: str
    hardware: str
    product: str
    channel: str
    build_id: str
    updater_version: int
    release_notes: str


class H2BuildIdentity(NamedTuple):
    version: str
    hardware: str
    product: str
    build_id: str


class PackageMetadata(NamedTuple):
    package_size: int
    package_sha256: str
    firmware_size: int
    firmware_sha256: str
    build_id: str


def _fixed(value: bytes, size: int, field: str) -> bytes:
    if len(value) >= size:
        raise ValueError(f"{field} must be shorter than {size} bytes")
    return value + bytes(size - len(value))


def _str(text: str, name: str) -> str:
    match = re.search(rf'#define\s+{re.escape(name)}\s+"([^"]*)"', text)
    if not match:
        match = re.search(
            rf'#define\s+{re.escape(name)}\s+\\?\s*\n?\s*"([^"]*)"', text
        )
    if not match:
        raise ValueError(f"{name} was not found")
    return match.group(1)


def _int(text: str, name: str) -> int:
    match = re.search(rf"#define\s+{re.escape(name)}\s+(\d+)", text)
    if not match:
        raise ValueError(f"{name} was not found")
    return int(match.group(1))


def read_build_identity(path: Path) -> BuildIdentity:
    text = path.read_text(encoding="utf-8")
    version = _str(text, "ESP_PLANTS_WAVESHARE_VERSION")
    return BuildIdentity(
        version,
        _str(text, "ESP_PLANTS_WAVESHARE_7_HARDWARE_ID"),
        _str(text, "ESP_PLANTS_WAVESHARE_PRODUCT_ID"),
        _str(text, "ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL"),
        f"ESPPLANTS-WAVESHARE-{version}",
        _int(text, "ESP_PLANTS_WAVESHARE_UPDATER_VERSION"),
        _str(text, "ESP_PLANTS_WAVESHARE_RELEASE_NOTES"),
    )


def read_h2_build_identity(path: Path) -> H2BuildIdentity:
    text = path.read_text(encoding="utf-8")
    # The default/current H2 target is the non-bridge branch in build_version.h.
    matches = re.findall(r'#define\s+ESP_PLANTS_H2_VERSION\s+"([^"]+)"', text)
    if not matches:
        raise ValueError("ESP_PLANTS_H2_VERSION was not found")
    version = matches[-1]
    return H2BuildIdentity(
        version,
        _str(text, "ESP_PLANTS_H2_HARDWARE_ID"),
        _str(text, "ESP_PLANTS_H2_PRODUCT_ID"),
        f"ESPPLANTS-H2-{version}",
    )


def validate_firmware(firmware: bytes) -> None:
    if len(firmware) < MIN_FIRMWARE_BYTES or firmware[0] != ESP_IMAGE_MAGIC:
        raise ValueError("invalid ESP application image")
    if struct.unpack_from("<H", firmware, 12)[0] != ESP32_S3_CHIP_ID:
        raise ValueError("firmware is not ESP32-S3")


def create_package(firmware: bytes, identity: BuildIdentity) -> bytes:
    validate_firmware(firmware)
    if (
        DISTRIBUTION_FIRMWARE_MARKER not in firmware
        or identity.build_id.encode() not in firmware
    ):
        raise ValueError("Waveshare firmware lacks distribution/build identity")
    digest = hashlib.sha256(firmware).digest()
    header = HEADER_STRUCT.pack(
        _fixed(PACKAGE_MAGIC, 16, "magic"),
        FORMAT_VERSION,
        HEADER_SIZE,
        _fixed(PACKAGE_HARDWARE_ID, 32, "hardware"),
        _fixed(PACKAGE_PRODUCT_ID, 32, "product"),
        _fixed(identity.build_id.encode(), 96, "build"),
        len(firmware),
        digest,
        bytes(296),
    )
    package = header + firmware
    if len(package) > MAX_PACKAGE_BYTES:
        raise ValueError("package too large")
    return package


def metadata(package: bytes, identity: BuildIdentity) -> PackageMetadata:
    firmware = package[HEADER_SIZE:]
    return PackageMetadata(
        len(package),
        hashlib.sha256(package).hexdigest(),
        len(firmware),
        hashlib.sha256(firmware).hexdigest(),
        identity.build_id,
    )


def write_h2_asset(
    firmware_path: Path,
    release_dir: Path,
    version: str,
    hardware: str,
    product: str,
) -> dict:
    if not firmware_path.is_file():
        raise ValueError(f"H2 release firmware missing: {firmware_path}")
    data = firmware_path.read_bytes()
    build_id = f"ESPPLANTS-H2-{version}"
    if (
        len(data) < MIN_FIRMWARE_BYTES
        or len(data) > H2_MAX_BYTES
        or data[0] != ESP_IMAGE_MAGIC
    ):
        raise ValueError(f"H2 firmware size/image invalid: {firmware_path}")
    if H2_DISTRIBUTION_MARKER not in data or build_id.encode() not in data:
        raise ValueError(f"H2 firmware lacks distribution/build identity: {build_id}")

    name = f"esp-plants-h2-{version}.bin"
    output = release_dir / name
    output.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    (release_dir / f"{name}.sha256").write_text(
        f"{digest}  {name}\n", encoding="ascii"
    )
    return {
        "product": product,
        "hardware": hardware,
        "version": version,
        "build_id": build_id,
        "protocol": 1,
        "asset": name,
        "firmware_size": len(data),
        "firmware_sha256": digest,
    }


def h2_asset(repo: Path, release_dir: Path) -> dict:
    identity = read_h2_build_identity(
        repo / "firmware" / "m5-h2-zigbee" / "include" / "build_version.h"
    )
    firmware = (
        repo
        / "firmware"
        / "m5-h2-zigbee"
        / ".pio"
        / "build"
        / "m5_gateway_h2_release"
        / "firmware.bin"
    )
    return write_h2_asset(
        firmware, release_dir, identity.version, identity.hardware, identity.product
    )


def create_manifest(
    identity: BuildIdentity,
    package_metadata: PackageMetadata,
    asset: str,
    h2: dict,
) -> bytes:
    document = {
        "schema": 1,
        "tag": f"v{identity.version}",
        "product": identity.product,
        "hardware": identity.hardware,
        "channel": identity.channel,
        "version": identity.version,
        "build_id": package_metadata.build_id,
        "asset": asset,
        "package_size": package_metadata.package_size,
        "package_sha256": package_metadata.package_sha256,
        "firmware_size": package_metadata.firmware_size,
        "firmware_sha256": package_metadata.firmware_sha256,
        "min_updater": identity.updater_version,
        "notes": identity.release_notes,
        "h2": h2,
    }
    encoded = (json.dumps(document, separators=(",", ":"), sort_keys=True) + "\n").encode(
        "ascii"
    )
    if len(encoded) > MAX_MANIFEST_BYTES:
        raise ValueError("manifest exceeds firmware limit")
    return encoded


def write_release_assets(
    firmware: Path,
    build_header: Path,
    release_dir: Path,
    h2_bridge_firmware: Path | None = None,
    h2_bridge_version: str | None = None,
):
    identity = read_build_identity(build_header)
    package = create_package(firmware.read_bytes(), identity)
    package_metadata = metadata(package, identity)
    release_dir.mkdir(parents=True, exist_ok=True)

    asset = f"esp-plants-waveshare-{identity.version}.plantsota"
    (release_dir / asset).write_bytes(package)

    repo = build_header.parents[3]
    h2 = h2_asset(repo, release_dir)

    if h2_bridge_firmware is not None or h2_bridge_version is not None:
        if h2_bridge_firmware is None or not h2_bridge_version:
            raise ValueError("H2 bridge firmware and version must be supplied together")
        if h2_bridge_version == h2["version"]:
            raise ValueError("H2 bridge version must differ from the current H2 target")
        write_h2_asset(
            h2_bridge_firmware,
            release_dir,
            h2_bridge_version,
            h2["hardware"],
            h2["product"],
        )

    manifest_path = release_dir / MANIFEST_ASSET_NAME
    manifest_path.write_bytes(create_manifest(identity, package_metadata, asset, h2))
    return release_dir / asset, manifest_path, package_metadata


def _enabled(env) -> bool:
    return DISTRIBUTION_BUILD_FLAG in str(env.get("BUILD_FLAGS", []))


def _post(source, target, env):
    if not _enabled(env):
        raise RuntimeError("distribution build required")
    project = Path(env.subst("$PROJECT_DIR"))
    asset, manifest, package_metadata = write_release_assets(
        Path(target[0].get_abspath()),
        project / "include" / "build_version.h",
        project.parents[1] / "release",
    )
    print(asset)
    print(manifest)
    print(package_metadata)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("build_header", type=Path)
    parser.add_argument("release_dir", type=Path)
    parser.add_argument("--h2-bridge-firmware", type=Path)
    parser.add_argument("--h2-bridge-version")
    args = parser.parse_args()
    print(
        write_release_assets(
            args.firmware,
            args.build_header,
            args.release_dir,
            args.h2_bridge_firmware,
            args.h2_bridge_version,
        )
    )
    return 0


try:
    Import("env")
except NameError:
    env = None

if env is not None and not env.IsIntegrationDump():
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _post)

if __name__ == "__main__":
    raise SystemExit(main())
