"""The 7B Wi-Fi timing experiment now waits for *complete splash dismissal*.

This supersedes the initial 4.3-second growth-only A/B test without changing
splash rendering or the original Waveshare 7 behavior.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HUB = ROOT / 'firmware/waveshare-hub'
MAIN = (HUB / 'src/main.cpp').read_text()
UPDATE = (HUB / 'src/update_service.cpp').read_text()
API = (HUB / 'include/update_service.h').read_text()
SPLASH = (HUB / 'src/boot_splash.cpp').read_text()


def test_unchanged_full_splash_and_growth():
    assert 'constexpr uint32_t kGrowDurationMs = 4300U;' in SPLASH
    assert 'constexpr uint32_t kSplashDurationMs = 6500U;' in SPLASH
    assert 'lv_timer_create(tick, 100U, nullptr)' in SPLASH
    assert 'lv_img_set_zoom(plant, zoom);' in SPLASH
    assert 'k7bPlantGrowthWifiDelayMs' not in UPDATE
    assert 'constexpr uint32_t k7bPostSplashWifiSettleMs = 300U;' in UPDATE


def test_only_7b_defers_station_and_signals_after_dismissal():
    assert 'espplants_update::begin(bootSplashActive);\n#else\n  espplants_update::begin();' in MAIN
    assert 'void begin(bool deferWifiUntilSplashDismissed = false);' in API
    assert 'void notifyBootSplashDismissed();' in API
    dismissal = MAIN.split('if (bootSplashActive && espplants_boot_splash::readyToOpenDashboard())', 1)[1]
    assert dismissal.index('espplants_boot_splash::dismiss();') < dismissal.index('bootSplashActive = false;')
    assert dismissal.index('lvgl_port_unlock();') < dismissal.index('espplants_update::notifyBootSplashDismissed();')
    assert '#ifdef ESP_PLANTS_WAVESHARE_7B\n      // Start the Wi-Fi settle clock' in dismissal


def test_station_only_deferred_not_updater_or_7():
    assert UPDATE.index('releaseUnusedBluetoothControllerMemory();') < UPDATE.index('networkPreferences.begin(')
    assert UPDATE.index('networkPreferences.begin(') < UPDATE.index('configurePortalRoutes();')
    assert UPDATE.index('configurePortalRoutes();') < UPDATE.index('wifiStartupDeferredForSplash = true;')
    assert 'if (savedSsid.length()) {\n#ifdef ESP_PLANTS_WAVESHARE_7B\n    if (deferWifiUntilSplashDismissed)' in UPDATE
    assert '#ifndef ESP_PLANTS_WAVESHARE_7B\n  (void)deferWifiUntilSplashDismissed;' in UPDATE
    assert '!wifiStartupDeferredForSplash &&' in UPDATE
    assert 'wifiSplashDismissedAtMs != 0 &&' in UPDATE
    assert 'millis() - wifiSplashDismissedAtMs >= k7bPostSplashWifiSettleMs' in UPDATE
    assert 'wifiStartupDeferredForSplash = false;' in UPDATE
    assert UPDATE.count('reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;') >= 2
    assert 'espplants_ota_installer::serviceRestart();' in UPDATE


def test_timing_gate_host_compiles_and_runs():
    if not shutil.which('g++'):
        import pytest
        pytest.skip('g++ not installed')
    method = UPDATE.split('void notifyBootSplashDismissed() {', 1)[1].split('\nvoid begin(bool ', 1)[0]
    signal = 'void notifyBootSplashDismissed() {' + method
    body = UPDATE.split('void service() {', 1)[1].split('  espplants_ota_installer::serviceRestart();', 1)[0]
    code = r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
constexpr uint32_t k7bPostSplashWifiSettleMs = 300U;
static bool initialized = true;
static bool wifiStartupDeferredForSplash = true;
static uint32_t wifiSplashDismissedAtMs = 0;
static uint32_t bootStartedMs = 1000;
static uint32_t clockMs = 2000;
static uint32_t millis() { return clockMs; }
struct SerialT { void printf(const char*, ...) {} } Serial;
constexpr int ESP_RST_POWERON = 1, ESP_RST_BROWNOUT = 2;
using esp_reset_reason_t = int;
int esp_reset_reason() { return 0; }
static int starts = 0;
void beginStationConnection(bool = false) { ++starts; }
void drain7bWifiRadioEvents() {}
''' + signal + '\nvoid oneServiceStep() {\n' + body + '\n}\n' + r'''
int main() {
  clockMs = 12000;
  oneServiceStep(); // Even after many seconds, no splash signal = no station
  assert(starts == 0);
  clockMs = 13000;
  notifyBootSplashDismissed();
  assert(wifiSplashDismissedAtMs == 13000);
  clockMs = 13299;
  oneServiceStep();
  assert(starts == 0);
  clockMs = 13300;
  oneServiceStep();
  assert(starts == 1);
  oneServiceStep();
  assert(starts == 1);
  notifyBootSplashDismissed(); // never schedules again
  assert(starts == 1);
}
'''
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / 'gate.cpp'
        exe = Path(directory) / 'gate'
        src.write_text(code)
        subprocess.run(['g++', '-std=c++17', '-DESP_PLANTS_WAVESHARE_7B',
                        str(src), '-o', str(exe)], check=True, capture_output=True, text=True)
        subprocess.run([str(exe)], check=True, capture_output=True, text=True)
