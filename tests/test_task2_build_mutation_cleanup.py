import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ALLOWED_EXTRA_SCRIPT = "post:scripts/build_plants_ota.py"
DEAD_H2_INJECTORS = {
    "alpha18_zigbee_rejoin_inject.py",
    "alpha20_zigbee_rejoin_inject.py",
    "alpha21_zigbee_rejoin_inject.py",
}
DEAD_WAVESHARE_INJECTORS = {
    "alpha22_phrases_inject.py",
    "alpha23_mixed_theme_inject.py",
    "alpha24_personality_ui_inject.py",
    "alpha25_personality_boot_fix.py",
    "alpha26_personality_page_fix.py",
    "alpha27_personality_zero_alloc_fix.py",
    "alpha28_personality_polish.py",
    "alpha29_personality_layout_cleanup.py",
    "alpha30_personality_header_fix.py",
    "phase2_inject.py",
}
DEAD_ROOT_MUTATORS = {
    "APPLY HOME PLANT SELECTOR FIX.cmd",
    "add-t5-udp-sequence-log.ps1",
    "apply-identity-service-cleanup.ps1",
    "apply-red-slower-sensor-id-flashes-v2.ps1",
    "apply-red-slower-sensor-id-flashes.ps1",
    "apply-service-green-and-repeat-id.ps1",
    "apply-t5-xiao-authoritative-identity-v2.ps1",
    "apply-t5-xiao-authoritative-identity-v3.ps1",
    "apply-t5-xiao-authoritative-identity.ps1",
    "apply-xiao-enrollment-identity-foundation.ps1",
    "reduce-xiao-service-espnow-beacons.ps1",
    "replace-t5-identity-cooldown-with-wake-tracking.ps1",
    "stop-repeated-t5-identity-assigns-v2.ps1",
    "stop-repeated-t5-identity-assigns.ps1",
    "update-changelog-identity-checkpoint.ps1",
}


def test_no_prebuild_source_mutators_are_active():
    active = []
    for pio in (ROOT / "firmware").rglob("platformio.ini"):
        text = pio.read_text(encoding="utf-8")
        for match in re.finditer(r"^\s*((?:pre|post):[^\s;]+)\s*$", text, re.MULTILINE):
            active.append((pio.relative_to(ROOT).as_posix(), match.group(1)))

    assert active == [("firmware/waveshare-hub/platformio.ini", ALLOWED_EXTRA_SCRIPT)]


def test_dead_injectors_are_gone():
    h2_scripts = ROOT / "firmware/m5-h2-zigbee/scripts"
    wave_scripts = ROOT / "firmware/waveshare-hub/scripts"
    for name in DEAD_H2_INJECTORS:
        assert not (h2_scripts / name).exists()
    for name in DEAD_WAVESHARE_INJECTORS:
        assert not (wave_scripts / name).exists()


def test_obsolete_root_mutators_are_gone():
    for name in DEAD_ROOT_MUTATORS:
        assert not (ROOT / name).exists()


def test_h2_commissioning_and_identity_behavior_is_in_real_source():
    source = (ROOT / "firmware/m5-h2-zigbee/src/main.cpp").read_text(encoding="utf-8")
    for token in (
        '#include "build_version.h"',
        "Zigbee.setRebootOpenNetwork(0);",
        "Zigbee.begin(&coordinatorConfig, false)",
        "case plantlink::MessageType::PermitJoin:",
        "Zigbee.openNetwork(seconds);",
        "static constexpr char kBuild[] = ESP_PLANTS_H2_BUILD_ID;",
    ):
        assert token in source
    assert "Zigbee.openNetwork(30);" not in source
    assert "one-shot router rejoin window" not in source


def test_waveshare_behavior_is_in_real_source_without_injectors():
    main = (ROOT / "firmware/waveshare-hub/src/main.cpp").read_text(encoding="utf-8")
    installer = (ROOT / "firmware/waveshare-hub/src/plants_ota_installer.cpp").read_text(encoding="utf-8")
    service = (ROOT / "firmware/waveshare-hub/src/update_service.cpp").read_text(encoding="utf-8")

    for token in (
        "reportedFieldFlagsThisBoot",
        "ONLY a soil-moisture report advances/selects a phrase",
        "phraseTheme = espplants_phrases::Theme::MIXED",
        "kPersonalityMap",
        "personalityDialogActive",
        "refreshPersonalitySelection",
        "Alpha.18 permit-join stale-status guard",
        "Alpha.21 H2 firmware identity display",
    ):
        assert token in main

    # H2 orchestration now belongs to the persistent PSRAM network worker;
    # the Waveshare installer is intentionally flash/package-only.
    assert '#include "h2_ota_client.h"' not in installer
    assert "espplants_h2_ota::updateForRelease" not in installer
    assert '#include "h2_ota_client.h"' in service
    assert "espplants_h2_ota::updateForRelease" in service
    assert "void otaNetworkWorkerTask(void *)" in service
    assert "Phase 2 release selection: choose highest compatible semantic version" in service
    assert "haveBestRelease" in service
