"""Source-contract tests for the 7B Wi-Fi-vs-boot-growth A/B experiment.

Run from repository root: pytest -q tests/test_7b_wifi_boot_growth_deferral.py
These tests do not replace a PlatformIO build or physical 7B verification.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / "firmware" / "waveshare-hub"
MAIN = (HUB / "src" / "main.cpp").read_text(encoding="utf-8")
UPDATE = (HUB / "src" / "update_service.cpp").read_text(encoding="utf-8")
API = (HUB / "include" / "update_service.h").read_text(encoding="utf-8")
SPLASH = (HUB / "src" / "boot_splash.cpp").read_text(encoding="utf-8")


def test_7b_delay_matches_unchanged_grow_duration():
    assert "constexpr uint32_t kGrowDurationMs = 4300U;" in SPLASH
    assert "constexpr uint32_t k7bPlantGrowthWifiDelayMs = 4300U;" in UPDATE
    assert "lv_timer_create(tick, 100U, nullptr)" in SPLASH


def test_only_7b_selects_deferred_station_start():
    assert "#ifdef ESP_PLANTS_WAVESHARE_7B\n  // Controlled 7B A/B test" in MAIN
    assert "espplants_update::begin(bootSplashActive);\n#else\n  espplants_update::begin();" in MAIN
    assert "void begin(bool deferWifiUntilPlantGrown = false);" in API


def test_7b_defers_station_not_updater_setup():
    assert UPDATE.index("releaseUnusedBluetoothControllerMemory();") < UPDATE.index("networkPreferences.begin(")
    assert UPDATE.index("networkPreferences.begin(") < UPDATE.index("configurePortalRoutes();")
    assert UPDATE.index("configurePortalRoutes();") < UPDATE.index("wifiStartupDeferredForPlantGrowth = true;")
    assert "if (savedSsid.length()) {\n#ifdef ESP_PLANTS_WAVESHARE_7B\n    if (deferWifiUntilPlantGrown)" in UPDATE


def test_service_releases_station_after_growth_once():
    assert "millis() - bootStartedMs >= k7bPlantGrowthWifiDelayMs" in UPDATE
    assert "wifiStartupDeferredForPlantGrowth = false;" in UPDATE
    assert "beginStationConnection(coldRadioReset);" in UPDATE
    assert "!wifiStartupDeferredForPlantGrowth &&" in UPDATE


def test_existing_connection_policy_and_diagnostic_logs_remain():
    assert UPDATE.count("reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;") >= 2
    assert "[wifi-ab] 7B station startup deferred" in UPDATE
    assert "[wifi-ab] 7B station startup released" in UPDATE
    assert "[wifi] connecting to" in UPDATE


def test_deferral_does_not_change_ui_or_ota_package_paths():
    assert "espplants_boot_splash::startAnimation();" in MAIN
    assert "espplants_boot_splash::readyToOpenDashboard()" in MAIN
    assert "espplants_ota_installer::serviceRestart();" in UPDATE
