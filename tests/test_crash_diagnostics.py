from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIO = ROOT / "firmware" / "waveshare-hub" / "platformio.ini"
DIAG = ROOT / "firmware" / "waveshare-hub" / "src" / "crash_diagnostics.cpp"
TOOL = ROOT / "tools" / "read-waveshare-crashdump.ps1"


def test_diagnostic_wrappers_and_monitor_filter_remain_enabled():
    pio = PIO.read_text(encoding="utf-8")

    for flag in (
        "-Wl,--wrap=_Z4loopv",
        "-Wl,--wrap=lvgl_port_lock",
        "-Wl,--wrap=lvgl_port_unlock",
        "-Wl,--wrap=lv_timer_handler",
        "-Wl,--wrap=_ZN16espplants_update7serviceEv",
    ):
        assert flag in pio

    assert "monitor_filters = esp32_exception_decoder" in pio


def test_stall_and_coredump_guards_remain_present():
    diag = DIAG.read_text(encoding="utf-8")

    assert "kLoopWarnMs = 3000u" in diag
    assert "kLoopForceDumpMs = 15000u" in diag
    assert "phase != MainPhase::UpdateService" in diag
    assert "esp_core_dump_image_check" in diag
    assert "heap_caps_get_minimum_free_size" in diag
    assert "abort();" in diag


def test_crashdump_reader_matches_partition_and_elf_workflow():
    tool = TOOL.read_text(encoding="utf-8")

    assert "0xff0000 0x10000" in tool
    assert "firmware.elf" in tool
    assert "read_flash" in tool
