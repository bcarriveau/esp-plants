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
SECTIONS_LD = MEM_DIR / "sections.ld"
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


for path in (MANIFEST, LCD_LIB, SECTIONS_LD, SDKCONFIG):
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
for path in (LCD_LIB, SECTIONS_LD, SDKCONFIG):
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

sections_text = SECTIONS_LD.read_text(encoding="utf-8", errors="replace")
for token in ("libesp_hw_support.a", "gdma_start", "gdma_reset", "mspi_timing_tuning"):
    if token not in sections_text:
        fail("generated sections.ld is missing required placement token: %s" % token)


def _is_sections_ld_reference(value):
    text = str(value).strip().replace("\\", "/").replace('"', "")
    if text.startswith("-Wl,-T,"):
        text = text[len("-Wl,-T,"):]
    elif text.startswith("-T"):
        text = text[2:]
    return text == "sections.ld" or text.endswith("/sections.ld")


def replace_sections_linker_script(build_env):
    flags = list(build_env.get("LINKFLAGS", []))
    replaced = 0
    updated = []
    for flag in flags:
        raw = str(flag).strip()
        if not _is_sections_ld_reference(raw):
            updated.append(flag)
            continue
        if raw.startswith("-Wl,-T,"):
            updated.append("-Wl,-T,%s" % SECTIONS_LD)
        elif raw.startswith("-T"):
            # Keep -T and the path as separate arguments so Windows paths with
            # spaces are quoted correctly by SCons.
            updated.extend(["-T", str(SECTIONS_LD)])
        else:
            updated.append(str(SECTIONS_LD))
        replaced += 1
    if replaced != 1:
        fail("expected to replace exactly one framework sections.ld linker flag; found %d" % replaced)
    build_env.Replace(LINKFLAGS=updated)
    print("[7b-vsync-sdk] using generated qio_opi sections.ld with GDMA IRAM placement")


# Keep application/framework compilation and all stock SDK archives on the
# shipped 3.0.7-h package. Only the rebuilt esp_lcd archive is overlaid. The
# generated qio_opi sections.ld adds the Kconfig-selected GDMA IRAM placement
# while preserving the stock esp_hw_support archive (including early PSRAM/MSPI
# initialization code).
replace_sections_linker_script(env)
env.Prepend(LIBPATH=[str(COMMON_DIR)])
env.Prepend(LIBS=["esp_lcd_vsync"])


def _read_symbol_addresses_from_map(map_text, wanted):
    symbols = {}
    wanted = set(wanted)
    for line in map_text.splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[-1] not in wanted:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        symbols[parts[-1]] = address
    return symbols


def verify_link_map(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    map_files = sorted(build_dir.glob("*.map"))
    if not map_files:
        fail("link map was not generated; cannot prove custom SDK linkage")
    map_text = map_files[0].read_text(encoding="utf-8", errors="replace")
    if "libesp_lcd_vsync.a(" not in map_text:
        fail("link map does not show extracted members from libesp_lcd_vsync.a")
    if "libesp_hw_support_vsync.a(" in map_text:
        fail("obsolete custom esp_hw_support archive is still linked")
    if "libesp_hw_support.a(" not in map_text:
        fail("link map does not show stock libesp_hw_support.a")

    iram_symbols = (
        "gdma_start",
        "gdma_stop",
        "gdma_append",
        "gdma_reset",
        "mspi_timing_enter_low_speed_mode",
        "mspi_timing_config_set_psram_clock",
    )
    symbols = _read_symbol_addresses_from_map(map_text, iram_symbols)
    for name in iram_symbols:
        address = symbols.get(name)
        if address is None:
            fail("ELF is missing required symbol: %s" % name)
        if not (0x40300000 <= address < 0x40400000):
            fail("%s linked outside S3 IRAM/noflash at 0x%08x" % (name, address))

    print("[7b-vsync-sdk] verified custom esp_lcd + stock esp_hw_support with IRAM GDMA/MSPI placement")
    print("[7b-vsync-sdk] gdma_start=0x%08x gdma_stop=0x%08x gdma_append=0x%08x gdma_reset=0x%08x mspi_low_speed=0x%08x" % (
        symbols["gdma_start"], symbols["gdma_stop"], symbols["gdma_append"],
        symbols["gdma_reset"], symbols["mspi_timing_enter_low_speed_mode"],
    ))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify_link_map)
print("[7b-vsync-sdk] custom VSYNC LCD + generated linker placement verified and enabled")
