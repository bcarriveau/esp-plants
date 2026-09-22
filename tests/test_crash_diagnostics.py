from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIO = ROOT / "firmware" / "waveshare-hub" / "platformio.ini"
DIAG = ROOT / "firmware" / "waveshare-hub" / "src" / "crash_diagnostics.cpp"
TOOL = ROOT / "tools" / "read-waveshare-crashdump.ps1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    pio = PIO.read_text(encoding="utf-8")
    diag = DIAG.read_text(encoding="utf-8")
    tool = TOOL.read_text(encoding="utf-8")

    for flag in (
        "-Wl,--wrap=_Z4loopv",
        "-Wl,--wrap=lvgl_port_lock",
        "-Wl,--wrap=lvgl_port_unlock",
        "-Wl,--wrap=lv_timer_handler",
        "-Wl,--wrap=_ZN16espplants_update7serviceEv",
    ):
        require(flag in pio, f"missing linker diagnostic wrapper: {flag}")

    require("monitor_filters = esp32_exception_decoder" in pio,
            "PlatformIO exception decoder monitor filter is not enabled")
    require("kLoopWarnMs = 3000u" in diag, "3 second warning threshold changed")
    require("kLoopForceDumpMs = 15000u" in diag, "15 second forced dump threshold changed")
    require("phase != MainPhase::UpdateService" in diag,
            "update service must be exempt from automatic diagnostic abort")
    require("esp_core_dump_image_check" in diag, "boot coredump detection missing")
    require("heap_caps_get_minimum_free_size" in diag, "minimum heap telemetry missing")
    require("abort();" in diag, "stall-to-coredump panic path missing")
    require("0xff0000 0x10000" in tool,
            "crashdump reader does not match existing Waveshare coredump partition")
    require("firmware.elf" in tool, "decoder must preserve/use exact firmware ELF")
    require("read_flash" in tool, "coredump reader does not read flash")

    # Guard against accidental UI/layout changes in this diagnostic package.
    require(not (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").exists(),
            "diagnostic package must not replace the locked main.cpp UI/application source")
    require(not (ROOT / "firmware" / "waveshare-hub" / "lib").exists(),
            "diagnostic package must not replace the locked display/LVGL library")

    print("PASS: crash diagnostics source/package guards")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
