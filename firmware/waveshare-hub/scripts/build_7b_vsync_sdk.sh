#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HUB_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$HUB_DIR/vendor/esp32-3.0.7-h-vsync"
WORK_ROOT="${ESP_PLANTS_VSYNC_BUILDER_WORKDIR:-$HOME/.cache/esp-plants-7b-vsync-sdk}"
BUILDER_DIR="$WORK_ROOT/esp32-arduino-lib-builder"

BUILDER_REPO="https://github.com/esp-arduino-libs/esp32-arduino-lib-builder.git"
BUILDER_COMMIT="9e2e6b17b99af5b677f7a09b7c208cfe9a4c6e1d"
ARDUINO_CORE="3.0.7"
ARDUINO_CORE_COMMIT="3bfa3e0a56c80305eec90f10e8318af8d8091bab"
IDF_BRANCH="release/v5.1"
IDF_COMMIT_EXPECTED="632e0c2a9fc7c754db4135dabb67f7fc6aa9fb87"
STOCK_SDK_SHA256="41f67e1c11f68b57d651955c93b63d6a8d35808ce6aff6ba3d1e1476178758f2"

required_commands=(git python3 cmake ninja jq flex bison gperf)
missing=()
for command_name in "${required_commands[@]}"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing+=("$command_name")
    fi
done
if ((${#missing[@]})); then
    printf 'ERROR: missing WSL/Linux build tools: %s\n' "${missing[*]}" >&2
    cat >&2 <<'DEPS'
Install the ESP-IDF/lib-builder prerequisites in WSL, then rerun the task:
  sudo apt-get update
  sudo apt-get install -y git wget curl libssl-dev libncurses-dev flex bison gperf \
    python3 python3-pip python3-setuptools python3-serial python3-click \
    python3-cryptography python3-future python3-pyparsing python3-pyelftools \
    cmake ninja-build ccache jq
DEPS
    exit 2
fi

mkdir -p "$WORK_ROOT"
if [[ ! -d "$BUILDER_DIR/.git" ]]; then
    rm -rf "$BUILDER_DIR"
    git clone "$BUILDER_REPO" "$BUILDER_DIR"
fi

git -C "$BUILDER_DIR" fetch --all --tags --prune
git -C "$BUILDER_DIR" reset --hard
# Reset the builder itself without throwing away the large pinned source clones
# from a previous attempt. Each preserved clone is independently reset/pinned
# below before it is used.
git -C "$BUILDER_DIR" clean -xfd \
    -e components/arduino/ \
    -e components/arduino_tinyusb/tinyusb/ \
    -e esp-idf/
git -C "$BUILDER_DIR" checkout --detach "$BUILDER_COMMIT"

# Reproduce the documented esp32-3.0.7-h S3 settings, then add only the
# LCD restart option. Do not set GDMA_CTRL_FUNC_IN_IRAM directly: Kconfig must
# select it from LCD_RGB_RESTART_IN_VSYNC, and the validation below proves it did.
python3 - "$BUILDER_DIR/configs/defconfig.common" "$BUILDER_DIR/configs/defconfig.esp32s3" <<'PY'
from pathlib import Path
import sys

common = Path(sys.argv[1])
s3 = Path(sys.argv[2])


def set_config(path: Path, name: str, value: str) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    prefixes = (f"CONFIG_{name}=", f"# CONFIG_{name} is not set")
    lines = [line for line in lines if not line.startswith(prefixes)]
    lines.append(f"CONFIG_{name}={value}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def unset_config(path: Path, name: str) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    prefixes = (f"CONFIG_{name}=", f"# CONFIG_{name} is not set")
    lines = [line for line in lines if not line.startswith(prefixes)]
    lines.append(f"# CONFIG_{name} is not set")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

unset_config(common, "COMPILER_OPTIMIZATION_SIZE")
set_config(common, "COMPILER_OPTIMIZATION_PERF", "y")
set_config(s3, "ESP32S3_DATA_CACHE_LINE_64B", "y")
set_config(s3, "SPIRAM_FETCH_INSTRUCTIONS", "y")
set_config(s3, "SPIRAM_RODATA", "y")
set_config(s3, "LCD_RGB_RESTART_IN_VSYNC", "y")
PY

pushd "$BUILDER_DIR" >/dev/null

# The pinned builder's tools/install-arduino.sh performs a `git pull` after
# checking out AR_BRANCH. When AR_BRANCH is the 3.0.7 tag, Git is detached and
# that pull fails. Reproduce the builder setup explicitly so both Arduino and
# IDF stay pinned to the exact commits, then use build.sh -s to skip its updater.
./tools/update-components.sh

ARDUINO_DIR="$BUILDER_DIR/components/arduino"
if [[ ! -d "$ARDUINO_DIR/.git" ]]; then
    git clone https://github.com/espressif/arduino-esp32.git "$ARDUINO_DIR"
else
    git -C "$ARDUINO_DIR" fetch --all --tags --prune
fi
git -C "$ARDUINO_DIR" reset --hard
git -C "$ARDUINO_DIR" clean -xfd
git -C "$ARDUINO_DIR" checkout --detach "$ARDUINO_CORE_COMMIT"
if [[ "$(git -C "$ARDUINO_DIR" rev-parse HEAD)" != "$ARDUINO_CORE_COMMIT" ]]; then
    echo "ERROR: Arduino core did not pin to $ARDUINO_CORE_COMMIT" >&2
    exit 3
fi

export IDF_PATH="$BUILDER_DIR/esp-idf"
export IDF_BRANCH
export IDF_COMMIT="$IDF_COMMIT_EXPECTED"
# shellcheck disable=SC1091
source "$BUILDER_DIR/tools/install-esp-idf.sh"
if [[ "$(git -C "$IDF_PATH" rev-parse HEAD)" != "$IDF_COMMIT_EXPECTED" ]]; then
    echo "ERROR: ESP-IDF did not pin to $IDF_COMMIT_EXPECTED" >&2
    exit 3
fi

# Build the common ESP32-S3 IDF libraries first. This produces libesp_lcd.a.
# -s is intentional: the exact Arduino + IDF revisions were installed above,
# and skipping the updater avoids the lib-builder's detached-tag git-pull bug.
./build.sh \
    -s \
    -t esp32s3 \
    -b idf-libs \
    qio 80m qio_ram

SDK_ROOT="$BUILDER_DIR/out/tools/esp32-arduino-libs/esp32s3"
COMMON_LCD="$SDK_ROOT/lib/libesp_lcd.a"
if [[ ! -f "$COMMON_LCD" ]]; then
    echo "ERROR: lib-builder did not produce $COMMON_LCD" >&2
    exit 3
fi

# Build only the qio + 80 MHz flash + OPI-PSRAM memory variant. The selected
# GDMA IRAM Kconfig changes libesp_hw_support.a in this variant. The same pinned
# environment remains exported in this shell from install-esp-idf.sh above.
./build.sh \
    -s \
    -t esp32s3 \
    -b mem-variant \
    qio 80m opi_ram

MEM_ROOT="$SDK_ROOT/qio_opi"
HW_LIB="$MEM_ROOT/libesp_hw_support.a"
SDKCONFIG="$MEM_ROOT/include/sdkconfig.h"
for required in "$COMMON_LCD" "$HW_LIB" "$SDKCONFIG"; do
    if [[ ! -f "$required" ]]; then
        echo "ERROR: required lib-builder output missing: $required" >&2
        exit 3
    fi
done

required_config_lines=(
    '#define CONFIG_LCD_RGB_RESTART_IN_VSYNC 1'
    '#define CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1'
    '#define CONFIG_COMPILER_OPTIMIZATION_PERF 1'
    '#define CONFIG_ESP32S3_DATA_CACHE_LINE_64B 1'
    '#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1'
    '#define CONFIG_SPIRAM_RODATA 1'
)
for line in "${required_config_lines[@]}"; do
    if ! grep -Fqx "$line" "$SDKCONFIG"; then
        echo "ERROR: generated qio_opi sdkconfig.h is missing: $line" >&2
        exit 4
    fi
done

AR_TOOL="$(command -v xtensa-esp32s3-elf-ar || true)"
if [[ -z "$AR_TOOL" ]]; then
    echo "ERROR: xtensa-esp32s3-elf-ar not found after IDF export" >&2
    exit 4
fi
if ! "$AR_TOOL" t "$COMMON_LCD" | grep -q 'esp_lcd_panel_rgb'; then
    echo "ERROR: rebuilt libesp_lcd.a does not contain the RGB panel driver object" >&2
    exit 4
fi
if ! "$AR_TOOL" t "$HW_LIB" | grep -qi 'gdma'; then
    echo "ERROR: rebuilt qio_opi libesp_hw_support.a does not contain GDMA object(s)" >&2
    exit 4
fi

rm -rf "$OUTPUT_DIR/common" "$OUTPUT_DIR/qio_opi"
mkdir -p "$OUTPUT_DIR/common" "$OUTPUT_DIR/qio_opi/include"
cp "$COMMON_LCD" "$OUTPUT_DIR/common/libesp_lcd_vsync.a"
cp "$HW_LIB" "$OUTPUT_DIR/qio_opi/libesp_hw_support_vsync.a"
cp "$SDKCONFIG" "$OUTPUT_DIR/qio_opi/include/sdkconfig.h"

python3 - "$OUTPUT_DIR" "$BUILDER_COMMIT" "$ARDUINO_CORE" "$ARDUINO_CORE_COMMIT" "$IDF_COMMIT_EXPECTED" "$STOCK_SDK_SHA256" <<'PY'
from pathlib import Path
import hashlib
import json
import sys

root = Path(sys.argv[1])
builder_commit, arduino_core, arduino_commit, idf_commit, stock_sha = sys.argv[2:]
paths = [
    Path("common/libesp_lcd_vsync.a"),
    Path("qio_opi/libesp_hw_support_vsync.a"),
    Path("qio_opi/include/sdkconfig.h"),
]

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

manifest = {
    "schema": 1,
    "purpose": "ESP PLANTS Waveshare 7B automatic RGB GDMA restart at VSYNC",
    "builder_repo": "https://github.com/esp-arduino-libs/esp32-arduino-lib-builder.git",
    "builder_commit": builder_commit,
    "arduino_core": arduino_core,
    "arduino_core_commit": arduino_commit,
    "idf_branch": "release/v5.1",
    "idf_commit": idf_commit,
    "stock_esp32_3_0_7_h_sha256": stock_sha,
    "memory_type": "qio_opi",
    "lcd_rgb_restart_in_vsync": True,
    "gdma_ctrl_func_in_iram": True,
    "files": {p.as_posix(): sha256(root / p) for p in paths},
}
(root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps(manifest, indent=2))
PY

popd >/dev/null

echo
echo "7B VSYNC SDK overlay built successfully."
echo "Output: $OUTPUT_DIR"
echo "Next: build/upload waveshare_s3_touch_lcd_7b_diag and confirm lcd_restart_in_vsync=1."
