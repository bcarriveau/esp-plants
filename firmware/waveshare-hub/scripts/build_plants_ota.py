"""Create the bounded ESP PLANTS Waveshare .plantsota package and manifest.

The format intentionally follows the proven Aircraft Radar release boundary:
- a 512-byte package header identifies hardware/product/build;
- package and firmware SHA-256 are both published;
- only a provenance-marked distribution firmware may become a public package.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
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
DISTRIBUTION_BUILD_FLAG = "ESP_PLANTS_DISTRIBUTION_BUILD"
MIN_FIRMWARE_BYTES = 64 * 1024
MAX_PACKAGE_BYTES = 7 * 1024 * 1024


class BuildIdentity(NamedTuple):
    version: str
    hardware: str
    product: str
    channel: str
    build_id: str
    updater_version: int
    release_notes: str


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


def _extract_string(text: str, name: str) -> str:
    match = re.search(rf'#define\s+{re.escape(name)}\s+"([^"]*)"', text)
    if not match:
        # release notes use a continued macro but still contain one string
        match = re.search(
            rf'#define\s+{re.escape(name)}\s+\\?\s*\n?\s*"([^"]*)"', text
        )
    if not match:
        raise ValueError(f"{name} was not found")
    return match.group(1)


def _extract_integer(text: str, name: str) -> int:
    match = re.search(rf'#define\s+{re.escape(name)}\s+(\d+)', text)
    if not match:
        raise ValueError(f"{name} was not found")
    return int(match.group(1))


def read_build_identity(path: Path) -> BuildIdentity:
    text = path.read_text(encoding="utf-8")
    identity = BuildIdentity(
        version=_extract_string(text, "ESP_PLANTS_WAVESHARE_VERSION"),
        hardware=_extract_string(text, "ESP_PLANTS_WAVESHARE_HARDWARE_ID"),
        product=_extract_string(text, "ESP_PLANTS_WAVESHARE_PRODUCT_ID"),
        channel=_extract_string(text, "ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL"),
        build_id=_extract_string(text, "ESP_PLANTS_WAVESHARE_BUILD_ID"),
        updater_version=_extract_integer(text, "ESP_PLANTS_WAVESHARE_UPDATER_VERSION"),
        release_notes=_extract_string(text, "ESP_PLANTS_WAVESHARE_RELEASE_NOTES"),
    )
    if identity.hardware != "waveshare-esp32-s3-touch-lcd-7":
        raise ValueError("unexpected ESP PLANTS hardware identifier")
    if identity.product != "esp-plants-waveshare":
        raise ValueError("unexpected ESP PLANTS product identifier")
    if identity.channel not in {"alpha", "beta", "stable"}:
        raise ValueError("unsupported ESP PLANTS release channel")
    if identity.updater_version < 1:
        raise ValueError("updater version must be positive")
    if not identity.build_id.startswith("ESPPLANTS-WAVESHARE-"):
        raise ValueError("build ID does not identify ESP PLANTS Waveshare")
    if len(identity.version.encode("ascii")) > 31:
        raise ValueError("version is too long")
    if len(identity.build_id.encode("ascii")) > 95:
        raise ValueError("build ID is too long")
    if len(identity.release_notes.encode("ascii")) > 191:
        raise ValueError("release notes are too long")
    return identity


def validate_firmware(firmware: bytes) -> None:
    if len(firmware) < MIN_FIRMWARE_BYTES:
        raise ValueError("firmware image is too small")
    if firmware[0] != ESP_IMAGE_MAGIC:
        raise ValueError("firmware does not have ESP application image magic")
    chip_id = struct.unpack_from("<H", firmware, 12)[0]
    if chip_id != ESP32_S3_CHIP_ID:
        raise ValueError(f"firmware chip ID {chip_id} is not ESP32-S3")


def validate_distribution_firmware(firmware: bytes, identity: BuildIdentity) -> None:
    validate_firmware(firmware)
    if DISTRIBUTION_FIRMWARE_MARKER not in firmware:
        raise ValueError(
            "firmware is not an ESP_PLANTS_DISTRIBUTION_BUILD image; public package refused"
        )
    if identity.build_id.encode("ascii") not in firmware:
        raise ValueError("declared ESP PLANTS build ID is not embedded in firmware")


def create_package(firmware: bytes, identity: BuildIdentity) -> bytes:
    validate_distribution_firmware(firmware, identity)
    firmware_digest = hashlib.sha256(firmware).digest()
    header = HEADER_STRUCT.pack(
        _fixed(PACKAGE_MAGIC, 16, "package magic"),
        FORMAT_VERSION,
        HEADER_SIZE,
        _fixed(PACKAGE_HARDWARE_ID, 32, "hardware ID"),
        _fixed(PACKAGE_PRODUCT_ID, 32, "product ID"),
        _fixed(identity.build_id.encode("ascii"), 96, "build ID"),
        len(firmware),
        firmware_digest,
        bytes(296),
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError("unexpected ESP PLANTS OTA header size")
    package = header + firmware
    if len(package) > MAX_PACKAGE_BYTES:
        raise ValueError("ESP PLANTS OTA package exceeds maximum supported size")
    return package


def validate_package(package: bytes, identity: BuildIdentity) -> PackageMetadata:
    if len(package) < HEADER_SIZE + MIN_FIRMWARE_BYTES:
        raise ValueError("OTA package is too small")
    fields = HEADER_STRUCT.unpack(package[:HEADER_SIZE])
    magic = fields[0].rstrip(b"\0")
    hardware = fields[3].rstrip(b"\0")
    product = fields[4].rstrip(b"\0")
    build_id_bytes = fields[5].rstrip(b"\0")
    firmware_size = fields[6]
    expected_firmware_sha = fields[7]
    firmware = package[HEADER_SIZE:]
    if magic != PACKAGE_MAGIC:
        raise ValueError("OTA package magic is invalid")
    if fields[1] != FORMAT_VERSION or fields[2] != HEADER_SIZE:
        raise ValueError("OTA package format is unsupported")
    if hardware != PACKAGE_HARDWARE_ID or product != PACKAGE_PRODUCT_ID:
        raise ValueError("OTA package hardware/product identity is invalid")
    if build_id_bytes.decode("ascii") != identity.build_id:
        raise ValueError("OTA package build ID is invalid")
    if len(firmware) != firmware_size:
        raise ValueError("OTA package firmware length is invalid")
    validate_distribution_firmware(firmware, identity)
    firmware_sha = hashlib.sha256(firmware).digest()
    if firmware_sha != expected_firmware_sha:
        raise ValueError("OTA package firmware SHA-256 is invalid")
    return PackageMetadata(
        package_size=len(package),
        package_sha256=hashlib.sha256(package).hexdigest(),
        firmware_size=len(firmware),
        firmware_sha256=firmware_sha.hex(),
        build_id=identity.build_id,
    )


def versioned_package_name(identity: BuildIdentity) -> str:
    return f"esp-plants-waveshare-{identity.version}.plantsota"


def create_manifest(identity: BuildIdentity, metadata: PackageMetadata, asset_name: str) -> bytes:
    manifest = {
        "schema": 1,
        "tag": f"v{identity.version}",
        "product": identity.product,
        "hardware": identity.hardware,
        "channel": identity.channel,
        "version": identity.version,
        "build_id": metadata.build_id,
        "asset": asset_name,
        "package_size": metadata.package_size,
        "package_sha256": metadata.package_sha256,
        "firmware_size": metadata.firmware_size,
        "firmware_sha256": metadata.firmware_sha256,
        "min_updater": identity.updater_version,
        "notes": identity.release_notes,
    }
    encoded = json.dumps(manifest, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode("ascii") + b"\n"
    if len(encoded) > MAX_MANIFEST_BYTES:
        raise ValueError("release manifest exceeds the 2048-byte firmware limit")
    return encoded


def write_release_assets(firmware_path: Path, build_header: Path, release_dir: Path) -> tuple[Path, Path, PackageMetadata]:
    identity = read_build_identity(build_header)
    firmware = firmware_path.read_bytes()
    package = create_package(firmware, identity)
    metadata = validate_package(package, identity)
    release_dir.mkdir(parents=True, exist_ok=True)
    asset_name = versioned_package_name(identity)
    asset_path = release_dir / asset_name
    manifest_path = release_dir / MANIFEST_ASSET_NAME
    asset_path.write_bytes(package)
    manifest_path.write_bytes(create_manifest(identity, metadata, asset_name))
    return asset_path, manifest_path, metadata


def _platformio_distribution_enabled(env) -> bool:
    try:
        raw_flags = env.get("BUILD_FLAGS", [])
    except Exception:
        raw_flags = []
    if isinstance(raw_flags, str):
        raw_items = [raw_flags]
    else:
        try:
            raw_items = list(raw_flags)
        except TypeError:
            raw_items = [raw_flags]
    tokens: list[str] = []
    for item in raw_items:
        tokens.extend(str(item).split())
    if not tokens:
        try:
            tokens.extend(str(env.subst("$BUILD_FLAGS")).split())
        except Exception:
            pass
    accepted = {f"-D{DISTRIBUTION_BUILD_FLAG}", f"-D{DISTRIBUTION_BUILD_FLAG}=1"}
    return any(token.strip("'\",[]") in accepted for token in tokens)


def _platformio_post_action(source, target, env) -> None:
    if not _platformio_distribution_enabled(env):
        raise RuntimeError(
            "Public ESP PLANTS OTA packaging requires ESP_PLANTS_DISTRIBUTION_BUILD; private build refused"
        )
    firmware_path = Path(target[0].get_abspath())
    project_dir = Path(env.subst("$PROJECT_DIR"))
    repo_dir = project_dir.parents[1]
    build_header = project_dir / "include" / "build_version.h"
    asset, manifest, metadata = write_release_assets(
        firmware_path, build_header, repo_dir / "release"
    )
    print(f"ESP PLANTS distribution OTA: {asset}")
    print(f"  package:  {metadata.package_size} bytes SHA256 {metadata.package_sha256}")
    print(f"  firmware: {metadata.firmware_size} bytes SHA256 {metadata.firmware_sha256}")
    print(f"GitHub Release manifest: {manifest}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("build_header", type=Path)
    parser.add_argument("release_dir", type=Path)
    args = parser.parse_args()
    asset, manifest, metadata = write_release_assets(
        args.firmware, args.build_header, args.release_dir
    )
    print(asset)
    print(manifest)
    print(metadata)
    return 0


try:
    Import("env")  # type: ignore[name-defined]
except NameError:
    env = None

if env is not None and not env.IsIntegrationDump():
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _platformio_post_action)

if __name__ == "__main__":
    raise SystemExit(main())
