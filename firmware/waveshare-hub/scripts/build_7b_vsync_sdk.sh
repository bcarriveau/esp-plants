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
TINYUSB_REPO="https://github.com/hathach/tinyusb.git"
TINYUSB_COMMIT="5217cee5de4cd555018da90f9f1bcc87fb1c1d3a"

required_commands=(git wget curl python3 cmake ninja jq flex bison gperf patch)
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
    cmake ninja-build ccache jq patch python3-venv libusb-1.0-0
DEPS
    exit 2
fi

# ESP-IDF's bundled OpenOCD binary links against libusb on Linux. Catch the
# missing runtime before IDF spends time validating/reinstalling its toolchain.
if ! ldconfig -p 2>/dev/null | grep -Fq 'libusb-1.0.so.0'; then
    cat >&2 <<'LIBUSB'
ERROR: OpenOCD runtime dependency libusb is missing in WSL.
Install it, then rerun the task:
  sudo apt-get install -y libusb-1.0-0
LIBUSB
    exit 2
fi

# ESP-IDF 5.1 creates its own Python virtual environment. Ubuntu can provide
# python3 without ensurepip, so catch that before the expensive IDF setup.
if ! python3 -c 'import ensurepip' >/dev/null 2>&1; then
    cat >&2 <<'VENV'
ERROR: Python venv support is missing in WSL.
Install it, then rerun the task:
  sudo apt-get install -y python3-venv
VENV
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

# The pinned builder's tools/update-components.sh follows TinyUSB master. That
# was acceptable in 2024, but current TinyUSB has merged usbd_control.c into
# usbd.c and no longer matches this builder's CMakeLists. Pin the stable TinyUSB
# 0.17.0 release that was current when this builder revision was made.
TINYUSB_DIR="$BUILDER_DIR/components/arduino_tinyusb/tinyusb"
if [[ ! -d "$TINYUSB_DIR/.git" ]]; then
    git clone "$TINYUSB_REPO" "$TINYUSB_DIR"
else
    git -C "$TINYUSB_DIR" fetch --all --tags --prune
fi
git -C "$TINYUSB_DIR" reset --hard
git -C "$TINYUSB_DIR" clean -xfd
git -C "$TINYUSB_DIR" checkout --detach "$TINYUSB_COMMIT"
if [[ "$(git -C "$TINYUSB_DIR" rev-parse HEAD)" != "$TINYUSB_COMMIT" ]]; then
    echo "ERROR: TinyUSB did not pin to $TINYUSB_COMMIT" >&2
    exit 3
fi
if [[ ! -f "$TINYUSB_DIR/src/device/usbd_control.c" ]]; then
    echo "ERROR: pinned TinyUSB is missing src/device/usbd_control.c required by this builder." >&2
    exit 3
fi

# The pinned builder's tools/install-arduino.sh performs a `git pull` after
# checking out AR_BRANCH. When AR_BRANCH is the 3.0.7 tag, Git is detached and
# that pull fails. Reproduce the builder setup explicitly so Arduino, TinyUSB,
# and IDF stay pinned to exact commits, then use build.sh -s to skip its updater.
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

# Set up the exact IDF revision directly instead of sourcing the pinned
# builder's install-esp-idf.sh. That old helper applies an ESP32-C6-only
# provisioning patch which no longer matches this exact IDF commit and is not
# relevant to the ESP32-S3 library build. Resetting both the IDF tree and its
# submodules makes reruns deterministic after any interrupted patch attempt.
if [[ ! -d "$IDF_PATH/.git" ]]; then
    git clone https://github.com/espressif/esp-idf.git -b "$IDF_BRANCH" "$IDF_PATH"
else
    git -C "$IDF_PATH" fetch --all --tags --prune
fi
git -C "$IDF_PATH" reset --hard
git -C "$IDF_PATH" clean -xfd
git -C "$IDF_PATH" checkout --detach "$IDF_COMMIT_EXPECTED"
git -C "$IDF_PATH" submodule sync --recursive
git -C "$IDF_PATH" submodule update --init --recursive --force
if [[ "$(git -C "$IDF_PATH" rev-parse HEAD)" != "$IDF_COMMIT_EXPECTED" ]]; then
    echo "ERROR: ESP-IDF did not pin to $IDF_COMMIT_EXPECTED" >&2
    exit 3
fi

# Match the pinned builder's supported IDF setup and its still-applicable
# compatibility patches. Tool and Python-environment installs are cached by
# ESP-IDF, so reruns do not redownload successful installations.
"$IDF_PATH/install.sh"

apply_builder_patch() {
    local patch_name="$1"
    local patch_path="$BUILDER_DIR/patches/$patch_name"
    echo "Applying lib-builder compatibility patch: $patch_name"
    (cd "$IDF_PATH" && patch -p1 -N -i "$patch_path")
}

apply_builder_patch "esp32s2_i2c_ll_master_init.diff"
apply_builder_patch "mmu_map.diff"
apply_builder_patch "lwip_max_tcp_pcb.diff"

# The builder's esp32c6_provisioning_bluedroid.diff expects an older conditional.
# At IDF 632e0c2a..., simple_ble already uses BLE mode for every non-BR/EDR/BTDM
# target, which includes ESP32-C6. Verify that semantic replacement before
# deliberately skipping the obsolete C6-only patch.
SIMPLE_BLE="$IDF_PATH/components/protocomm/src/simple_ble/simple_ble.c"
if ! grep -Fq '#else  //For all other chips supporting BLE Only' "$SIMPLE_BLE" || \
   ! grep -Fq 'ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);' "$SIMPLE_BLE"; then
    echo "ERROR: pinned IDF simple_ble fallback changed; refusing to skip the obsolete ESP32-C6 patch." >&2
    exit 3
fi
echo "Skipping obsolete ESP32-C6 provisioning patch; pinned IDF already has the BLE-only fallback."

# The pinned 2024 IDF export scripts assume ordinary Bash unset-variable
# semantics. Relax nounset only while sourcing that external environment, then
# restore strict mode for our wrapper.
# shellcheck disable=SC1091
set +u
source "$IDF_PATH/export.sh"
set -u

# The lib-builder's idf-libs and mem-variant convenience targets both depend
# on the complete application ELF. That needlessly compiles thousands of
# unrelated managed components (TensorFlow Lite, Matter, speech, etc.) and
# makes this two-library experiment vulnerable to upstream component drift.
# Configure the exact same S3 SDK variants, then ask CMake/Ninja only for the
# two component archives required by the VSYNC experiment.
export IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1
STAGE_DIR="$WORK_ROOT/vsync-stage"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/common" "$STAGE_DIR/qio_opi/include"

COMMON_CONFIGS="configs/defconfig.common;configs/defconfig.esp32s3;configs/defconfig.debug_default;configs/defconfig.esp_sr;configs/defconfig.qio;configs/defconfig.80m;configs/defconfig.qio_ram"
rm -rf build sdkconfig
idf.py -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="$COMMON_CONFIGS" reconfigure
cmake --build build --target __idf_esp_lcd
COMMON_LCD_BUILD="$BUILDER_DIR/build/esp-idf/esp_lcd/libesp_lcd.a"
if [[ ! -f "$COMMON_LCD_BUILD" ]]; then
    echo "ERROR: targeted esp_lcd build did not produce $COMMON_LCD_BUILD" >&2
    exit 3
fi
cp "$COMMON_LCD_BUILD" "$STAGE_DIR/common/libesp_lcd_vsync.a"

# Reconfigure only for the stock qio + 80 MHz flash + OPI-PSRAM memory
# variant, then compile esp_hw_support. CONFIG_LCD_RGB_RESTART_IN_VSYNC selects
# GDMA_CTRL_FUNC_IN_IRAM in this generated config, so this archive contains the
# matching GDMA ISR-safe implementation without building unrelated components.
MEM_CONFIGS="configs/defconfig.common;configs/defconfig.esp32s3;configs/defconfig.debug_default;configs/defconfig.esp_sr;configs/defconfig.qio;configs/defconfig.80m;configs/defconfig.opi_ram"
rm -rf build sdkconfig
idf.py -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="$MEM_CONFIGS" reconfigure
cmake --build build --target __idf_esp_hw_support
HW_LIB_BUILD="$BUILDER_DIR/build/esp-idf/esp_hw_support/libesp_hw_support.a"
SDKCONFIG_BUILD="$BUILDER_DIR/build/config/sdkconfig.h"
for required in "$HW_LIB_BUILD" "$SDKCONFIG_BUILD"; do
    if [[ ! -f "$required" ]]; then
        echo "ERROR: targeted qio_opi build output missing: $required" >&2
        exit 3
    fi
done
cp "$HW_LIB_BUILD" "$STAGE_DIR/qio_opi/libesp_hw_support_vsync.a"
cp "$SDKCONFIG_BUILD" "$STAGE_DIR/qio_opi/include/sdkconfig.h"

COMMON_LCD="$STAGE_DIR/common/libesp_lcd_vsync.a"
HW_LIB="$STAGE_DIR/qio_opi/libesp_hw_support_vsync.a"
SDKCONFIG="$STAGE_DIR/qio_opi/include/sdkconfig.h"

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
