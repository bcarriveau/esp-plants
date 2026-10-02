Import("env", "projenv")

if env.IsIntegrationDump():
    Return()

import hashlib
import json
from pathlib import Path

from SCons.Script import Exit

PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()
SDK_DIR = PROJECT_DIR / "vendor" / "esp32-3.0.7-h-vsync"
COMMON_DIR = SDK_DIR / "common"
MEM_DIR = SDK_DIR / "qio_opi"
CONFIG_DIR = MEM_DIR / "include"
MANIFEST = SDK_DIR / "manifest.json"
LCD_LIB = COMMON_DIR / "libesp_lcd_vsync.a"
HW_LIB = MEM_DIR / "libesp_hw_support_vsync.a"
SDKCONFIG = CONFIG_DIR / "sdkconfig.h"

REQUIRED_CONFIGS = (
    "#define CONFIG_LCD_RGB_RESTART_IN_VSYNC 1",
    "#define CONFIG_GDMA_CTRL_FUNC_IN_IRAM 1",
    "#define CONFIG_COMPILER_OPTIMIZATION_PERF 1",
    "#define CONFIG_ESP32S3_DATA_CACHE_LINE_64B 1",
    "#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1",
    "#define CONFIG_SPIRAM_RODATA 1",
)


def fail(message):
    print("[7b-vsync-sdk] ERROR: " + message)
    print("[7b-vsync-sdk] Run the VS Code task 'ESP PLANTS: Build 7B VSYNC SDK' first.")
    Exit(1)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


for path in (MANIFEST, LCD_LIB, HW_LIB, SDKCONFIG):
    if not path.is_file():
        fail("missing generated file: %s" % path.relative_to(PROJECT_DIR))

try:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
except Exception as exc:
    fail("invalid manifest.json: %s" % exc)

if manifest.get("schema") != 1:
    fail("unsupported manifest schema")
if manifest.get("arduino_core") != "3.0.7":
    fail("manifest Arduino core is not 3.0.7")
if manifest.get("idf_commit") != "632e0c2a9fc7c754db4135dabb67f7fc6aa9fb87":
    fail("manifest IDF commit does not match Arduino 3.0.7")
if not manifest.get("lcd_rgb_restart_in_vsync"):
    fail("manifest does not assert LCD RGB restart-in-VSYNC")
if not manifest.get("gdma_ctrl_func_in_iram"):
    fail("manifest does not assert GDMA control functions in IRAM")

files = manifest.get("files", {})
for path in (LCD_LIB, HW_LIB, SDKCONFIG):
    rel = path.relative_to(SDK_DIR).as_posix()
    expected = files.get(rel)
    if not expected:
        fail("manifest is missing SHA-256 for %s" % rel)
    actual = sha256_file(path)
    if actual.lower() != expected.lower():
        fail("SHA-256 mismatch for %s" % rel)

config_text = SDKCONFIG.read_text(encoding="utf-8", errors="replace")
for token in REQUIRED_CONFIGS:
    if token not in config_text:
        fail("generated sdkconfig.h is missing: %s" % token)

# This is a POST extra script: pioarduino has already constructed its normal
# framework search paths. Prepending here affects only the 7B environments and
# leaves the original Waveshare 7 on the stock esp32-3.0.7-h package.
env.Prepend(CPPPATH=[str(CONFIG_DIR)])
projenv.Prepend(CPPPATH=[str(CONFIG_DIR)])
env.Prepend(LIBPATH=[str(COMMON_DIR), str(MEM_DIR)])
env.Prepend(LIBS=["esp_lcd_vsync", "esp_hw_support_vsync"])


def verify_link_map(source, target, build_env):
    build_dir = Path(build_env.subst("$BUILD_DIR"))
    map_files = sorted(build_dir.glob("*.map"))
    if not map_files:
        fail("link map was not generated; cannot prove custom SDK archives were linked")
    map_text = map_files[0].read_text(encoding="utf-8", errors="replace")
    required_members = (
        "libesp_lcd_vsync.a(",
        "libesp_hw_support_vsync.a(",
    )
    for token in required_members:
        if token not in map_text:
            fail("link map does not show extracted members from %s" % token[:-1])
    print("[7b-vsync-sdk] verified custom esp_lcd + esp_hw_support members in %s" % map_files[0].name)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify_link_map)
print("[7b-vsync-sdk] custom VSYNC/GDMA SDK overlay verified and enabled")
