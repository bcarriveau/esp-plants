from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import NamedTuple

PACKAGE_MAGIC = b"ESP-PLANTS-OTA"
PACKAGE_PRODUCT_ID = b"ESP-PLANTS-WAVESHARE"
FORMAT_VERSION = 1
HEADER_SIZE = 512
ESP_IMAGE_MAGIC = 0xE9
ESP32_S3_CHIP_ID = 9
HEADER_STRUCT = struct.Struct("<16sHH32s32s96sI32s296s")
MANIFEST_ASSET_NAME = "esp-plants-waveshare.manifest.json"
MANIFEST_SCHEMA = 1
MAX_MANIFEST_BYTES = 2048
DISTRIBUTION_FIRMWARE_MARKER = b"ESP-PLANTS-DISTRIBUTION-BUILD"
H2_DISTRIBUTION_MARKER = b"ESP-PLANTS-H2-DISTRIBUTION-BUILD"
DISTRIBUTION_BUILD_FLAG = "ESP_PLANTS_DISTRIBUTION_BUILD"
WAVESHARE_7B_BUILD_FLAG = "ESP_PLANTS_WAVESHARE_7B"
MIN_FIRMWARE_BYTES = 64 * 1024
MAX_PACKAGE_BYTES = 7 * 1024 * 1024
H2_MAX_BYTES = 0xE0000
VARIANT_7 = "7"
VARIANT_7B = "7b"


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


def _normalize_variant(variant: str) -> str:
    value = str(variant).strip().lower()
    if value in ("7", "waveshare_s3_touch_lcd_7", "waveshare_s3_touch_lcd_7_release"):
        return VARIANT_7
    if value in ("7b", "waveshare_s3_touch_lcd_7b", "waveshare_s3_touch_lcd_7b_release"):
        return VARIANT_7B
    raise ValueError(f"unsupported Waveshare variant: {variant}")


def _generic_hardware_id(text: str, variant: str) -> str:
    """Resolve the generic runtime macro exactly as the selected build does."""
    variant = _normalize_variant(variant)
    wanted_symbol = (
        "ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID"
        if variant == VARIANT_7B
        else "ESP_PLANTS_WAVESHARE_7_HARDWARE_ID"
    )
    generic_targets = re.findall(
        r"#define\s+ESP_PLANTS_WAVESHARE_HARDWARE_ID\s+([A-Za-z0-9_]+)", text
    )
    if wanted_symbol not in generic_targets:
        raise ValueError(
            "ESP_PLANTS_WAVESHARE_HARDWARE_ID does not resolve to the selected variant"
        )
    return _str(text, wanted_symbol)


def read_build_identity(path: Path, variant: str = VARIANT_7) -> BuildIdentity:
    text = path.read_text(encoding="utf-8")
    version = _str(text, "ESP_PLANTS_WAVESHARE_VERSION")
    return BuildIdentity(
        version,
        _generic_hardware_id(text, variant),
        _str(text, "ESP_PLANTS_WAVESHARE_PRODUCT_ID"),
        _str(text, "ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL"),
        f"ESPPLANTS-WAVESHARE-{version}",
        _int(text, "ESP_PLANTS_WAVESHARE_UPDATER_VERSION"),
        _str(text, "ESP_PLANTS_WAVESHARE_RELEASE_NOTES"),
    )


def read_h2_build_identity(path: Path) -> H2BuildIdentity:
    text = path.read_text(encoding="utf-8")
    # There is one current H2 target; PlantLink v2 has no bridge build.
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
        or identity.hardware.encode() not in firmware
    ):
        raise ValueError("Waveshare firmware lacks distribution/build identity")
    digest = hashlib.sha256(firmware).digest()
    header = HEADER_STRUCT.pack(
        _fixed(PACKAGE_MAGIC, 16, "magic"),
        FORMAT_VERSION,
        HEADER_SIZE,
        _fixed(identity.hardware.encode("ascii"), 32, "hardware"),
        _fixed(PACKAGE_PRODUCT_ID, 32, "product"),
        _fixed(identity.build_id.encode("ascii"), 96, "build"),
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


def read_plantlink_version(repo: Path | None = None) -> int:
    if repo is None:
        repo = Path(__file__).resolve().parents[3]
    header = repo / "shared" / "plantlink" / "plantlink.h"
    match = re.search(r"kProtocolVersion\s*=\s*(\d+)", header.read_text(encoding="utf-8"))
    if not match:
        raise ValueError("PlantLink protocol version missing")
    return int(match.group(1))


def write_h2_asset(
    firmware_path: Path,
    release_dir: Path,
    version: str,
    hardware: str,
    product: str,
    protocol: int,
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
    digest = hashlib.sha256(data).hexdigest()

    # The release script packages Waveshare 7 and 7B separately and then runs
    # an explicit combined-manifest pass. Each pass references the same H2
    # binary. Reuse an identical release asset instead of repeatedly opening
    # and truncating it; on Windows a just-written file can transiently reject
    # that redundant rewrite with OSError(EINVAL).
    reuse_output = False
    if output.is_file() and output.stat().st_size == len(data):
        try:
            reuse_output = output.read_bytes() == data
        except OSError:
            reuse_output = False
    if not reuse_output:
        output.write_bytes(data)

    digest_path = release_dir / f"{name}.sha256"
    digest_text = f"{digest}  {name}\n"
    reuse_digest = False
    if digest_path.is_file():
        try:
            reuse_digest = digest_path.read_text(encoding="ascii") == digest_text
        except OSError:
            reuse_digest = False
    if not reuse_digest:
        digest_path.write_text(digest_text, encoding="ascii")
    return {
        "product": product,
        "hardware": hardware,
        "version": version,
        "build_id": build_id,
        "protocol": protocol,
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
        firmware,
        release_dir,
        identity.version,
        identity.hardware,
        identity.product,
        read_plantlink_version(repo),
    )


def package_asset_name(identity: BuildIdentity) -> str:
    suffix = "7b" if identity.hardware.endswith("-7b") else "7"
    return f"esp-plants-waveshare-{suffix}-{identity.version}.plantsota"


def _variant_manifest_entry(
    identity: BuildIdentity, package_metadata: PackageMetadata, asset: str
) -> dict:
    return {
        "hardware": identity.hardware,
        "build_id": package_metadata.build_id,
        "asset": asset,
        "package_size": package_metadata.package_size,
        "package_sha256": package_metadata.package_sha256,
        "firmware_size": package_metadata.firmware_size,
        "firmware_sha256": package_metadata.firmware_sha256,
    }


def create_multi_manifest(variants: list[tuple[BuildIdentity, PackageMetadata, str]], h2: dict) -> bytes:
    if not variants:
        raise ValueError("at least one Waveshare variant is required")
    identity = variants[0][0]
    for candidate, _, _ in variants[1:]:
        if (
            candidate.version != identity.version
            or candidate.product != identity.product
            or candidate.channel != identity.channel
            or candidate.updater_version != identity.updater_version
        ):
            raise ValueError("Waveshare variants do not share one release identity")
    entries = [
        _variant_manifest_entry(candidate, package_metadata, asset)
        for candidate, package_metadata, asset in variants
    ]
    entries.sort(key=lambda item: item["hardware"])
    # Keep schema 1 and its top-level Waveshare fields for installed 7-inch
    # firmware.  The additive variants[] list is understood by new 7/7B builds,
    # while older 7-inch builds safely ignore it and keep using the legacy 7
    # fields below.  This makes the first dual-hardware release OTA-reachable.
    legacy_index = 0
    for index, (candidate, _, _) in enumerate(variants):
        if candidate.hardware.endswith("-7"):
            legacy_index = index
            break
    legacy_identity, legacy_metadata, legacy_asset = variants[legacy_index]
    document = {
        "schema": MANIFEST_SCHEMA,
        "tag": f"v{identity.version}",
        "product": identity.product,
        "hardware": legacy_identity.hardware,
        "channel": identity.channel,
        "version": identity.version,
        "build_id": legacy_metadata.build_id,
        "asset": legacy_asset,
        "package_size": legacy_metadata.package_size,
        "package_sha256": legacy_metadata.package_sha256,
        "firmware_size": legacy_metadata.firmware_size,
        "firmware_sha256": legacy_metadata.firmware_sha256,
        "min_updater": identity.updater_version,
        "notes": identity.release_notes,
        "variants": entries,
        "h2": h2,
    }
    encoded = (json.dumps(document, separators=(",", ":"), sort_keys=True) + "\n").encode(
        "ascii"
    )
    if len(encoded) > MAX_MANIFEST_BYTES:
        raise ValueError("manifest exceeds firmware limit")
    return encoded


def create_manifest(
    identity: BuildIdentity,
    package_metadata: PackageMetadata,
    asset: str,
    h2: dict,
) -> bytes:
    # Compatibility helper used by host tests and single-variant tooling.
    return create_multi_manifest([(identity, package_metadata, asset)], h2)


def _metadata_from_asset(path: Path, identity: BuildIdentity) -> PackageMetadata:
    package = path.read_bytes()
    if len(package) < HEADER_SIZE:
        raise ValueError(f"package is truncated: {path}")
    fields = HEADER_STRUCT.unpack_from(package)
    hardware = fields[3].split(b"\0", 1)[0].decode("ascii")
    build_id = fields[5].split(b"\0", 1)[0].decode("ascii")
    if hardware != identity.hardware or build_id != identity.build_id:
        raise ValueError(f"package identity mismatch: {path}")
    return metadata(package, identity)


def write_release_assets(
    firmware: Path,
    build_header: Path,
    release_dir: Path,
    variant: str = VARIANT_7,
    combine_existing: bool = False,
):
    variant = _normalize_variant(variant)
    identity = read_build_identity(build_header, variant)
    package = create_package(firmware.read_bytes(), identity)
    package_metadata = metadata(package, identity)
    release_dir.mkdir(parents=True, exist_ok=True)

    asset = package_asset_name(identity)
    asset_path = release_dir / asset
    asset_path.write_bytes(package)

    repo = build_header.parents[3]
    h2 = h2_asset(repo, release_dir)

    manifest_variants: list[tuple[BuildIdentity, PackageMetadata, str]] = [
        (identity, package_metadata, asset)
    ]
    if combine_existing:
        for other_variant in (VARIANT_7, VARIANT_7B):
            other_identity = read_build_identity(build_header, other_variant)
            if other_identity.hardware == identity.hardware:
                continue
            other_asset = package_asset_name(other_identity)
            other_path = release_dir / other_asset
            if other_path.is_file():
                manifest_variants.append(
                    (other_identity, _metadata_from_asset(other_path, other_identity), other_asset)
                )

    manifest_path = release_dir / MANIFEST_ASSET_NAME
    manifest_path.write_bytes(create_multi_manifest(manifest_variants, h2))
    return asset_path, manifest_path, package_metadata


def _enabled(env) -> bool:
    return DISTRIBUTION_BUILD_FLAG in str(env.get("BUILD_FLAGS", []))


def _variant_from_env(env) -> str:
    return VARIANT_7B if WAVESHARE_7B_BUILD_FLAG in str(env.get("BUILD_FLAGS", [])) else VARIANT_7


def _post(source, target, env):
    if not _enabled(env):
        raise RuntimeError("distribution build required")
    project = Path(env.subst("$PROJECT_DIR"))
    asset, manifest, package_metadata = write_release_assets(
        Path(target[0].get_abspath()),
        project / "include" / "build_version.h",
        project.parents[1] / "release",
        variant=_variant_from_env(env),
        combine_existing=False,
    )
    print(asset)
    print(manifest)
    print(package_metadata)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("build_header", type=Path)
    parser.add_argument("release_dir", type=Path)
    parser.add_argument("--variant", choices=(VARIANT_7, VARIANT_7B), default=VARIANT_7)
    parser.add_argument(
        "--combine-existing",
        action="store_true",
        help="include the other current-version Waveshare package in the unified manifest",
    )
    args = parser.parse_args()
    print(
        write_release_assets(
            args.firmware,
            args.build_header,
            args.release_dir,
            variant=args.variant,
            combine_existing=args.combine_existing,
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
