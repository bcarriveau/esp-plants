"""Guard the 7B-only disconnect/RGB/UI timestamp experiment.

Compiles the real disconnectWifi() function body against small host stubs
without changing or compiling the actual PlatformIO firmware.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPDATE = ROOT / 'firmware/waveshare-hub/src/update_service.cpp'
MAIN = ROOT / 'firmware/waveshare-hub/src/main.cpp'
DRIVER = ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp'


def _source(path):
    return path.read_text(encoding='utf-8')


def test_7b_only_and_excluded_from_distribution():
    for src in (_source(UPDATE), _source(MAIN)):
        assert '#if defined(ESP_PLANTS_WAVESHARE_7B) && !defined(ESP_PLANTS_DISTRIBUTION_BUILD)' in src
    assert '#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)' in _source(DRIVER)


def test_shared_clock_for_disconnect_lvgl_and_rgb_event():
    update, main, driver = map(_source, (UPDATE, MAIN, DRIVER))
    assert '#include <esp_timer.h>' in update
    assert 'disconnectCallStartUs = static_cast<uint32_t>(esp_timer_get_time())' in update
    assert 'disconnectCallEndUs = static_cast<uint32_t>(esp_timer_get_time())' in update
    assert 'statusReadyUs = static_cast<uint32_t>(esp_timer_get_time())' in update
    assert 'sevenBDisconnectTapUs = static_cast<uint32_t>(esp_timer_get_time())' in main
    assert 'disconnectRedrawStartUs = traceDisconnectRedraw' in main
    assert 'disconnectRedrawEndUs = static_cast<uint32_t>(esp_timer_get_time())' in main
    assert 'frame_us = static_cast<uint32_t>(esp_timer_get_time())' in driver
    assert '[wifi-disconnect-diag]' in update
    assert '[wifi-ui-diag]' in main
    assert '[rgb-gap-diag]' in driver


def test_radio_still_disconnected_with_retained_credentials():
    update = _source(UPDATE)
    body = update.split('void disconnectWifi() {', 1)[1].split('\nvoid reconnectWifi() {', 1)[0]
    assert body.count('WiFi.disconnect(false, false);') == 1
    assert body.count('reconnectSuppressed = true;') == 1
    assert 'savedSsid =' not in body and 'savedPassword =' not in body
    assert 'networkPreferences.remove(' not in body
    assert 'requestDisplayRecovery' not in body
    assert body.index('disconnectCallStartUs =') < body.index('WiFi.disconnect(false, false);')
    assert body.index('WiFi.disconnect(false, false);') < body.index('disconnectCallEndUs =')
    assert body.index('setStatus("Wi-Fi disconnected until you reconnect or reboot")') < body.index('Serial.printf("[wifi-disconnect-diag]')


def test_ui_marks_dirty_after_same_radio_call_and_logs_after_unlock():
    main = _source(MAIN)
    event = main.split('void wifiDisconnectEvent(lv_event_t *event) {',1)[1].split('\nvoid wifiForgetAskEvent(',1)[0]
    assert event.index('sevenBDisconnectUiTracePending = true;') < event.index('espplants_update::disconnectWifi();')
    assert event.index('espplants_update::disconnectWifi();') < event.index('dirty.update = true;')
    refresh = main.split('void refreshUi(bool force, bool alreadyInLvglContext) {',1)[1].split('\nvoid handleNetworkStatus(',1)[0]
    assert refresh.index('traceDisconnectRedraw =') < refresh.index('if (updateModalOpen && (force || dirty.update || intervalElapsed))')
    assert refresh.index('disconnectRedrawEndUs = static_cast<uint32_t>(esp_timer_get_time());') > refresh.index('dirty.update = false;')
    assert refresh.index('if (lockedHere) lvgl_port_unlock();') < refresh.index('Serial.printf("[wifi-ui-diag]')


def test_rgb_reports_outside_isr_only_when_gap_over_60ms():
    driver = _source(DRIVER)
    callback = driver.split('IRAM_ATTR bool onRgbVsyncCallback(',1)[1].split('    return (need_yield == pdTRUE);',1)[0]
    assert 'if (frame_gap_us > 60000U)' in callback
    assert 'seven_b_diag_last_long_gap_us = frame_gap_us;' in callback
    assert 'seven_b_diag_last_long_gap_at_us = frame_us;' in callback
    assert 'Serial.printf' not in callback
    assert 'if (delayed_frames != diag_last_reported_long_gaps)' in driver


def test_real_disconnect_function_host_compiles_and_executes_both_variants():
    if not shutil.which('g++'):
        import pytest
        pytest.skip('g++ unavailable')
    update = _source(UPDATE)
    body = update.split('void disconnectWifi() {',1)[1].split('\nvoid reconnectWifi() {',1)[0]
    function = 'void disconnectWifi() {' + body
    fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
static bool initialized = true;
static bool installTaskRunning = false;
static bool setupPortalRunning = false;
static bool reconnectSuppressed = false;
static bool manualCheckRequested = true;
static bool waitingForClockNotice = true;
static bool sawWifiConnected = true;
static char connectedSsid[48] = "TestSSID";
static char wifiAddressText[48] = "192.168.2.1";
static std::string lastStatus;
static std::string printed;
static int disconnects = 0;
static uint32_t fakeTime = 1000;
int64_t esp_timer_get_time() { fakeTime += 10; return fakeTime; }
void stopSetupPortal() { setupPortalRunning = false; }
void setStatus(const char* s) { lastStatus = s; }
struct { bool disconnect(bool radioOff, bool erase) {
    assert(!radioOff && !erase); ++disconnects; fakeTime += 110; return true;
} } WiFi;
struct SerialType {
    void println(const char* s) { printed += s; printed += "\\n"; }
    void printf(const char* format, ...) {
        char temp[1024]; va_list args; va_start(args, format);
        vsnprintf(temp,sizeof(temp),format,args); va_end(args); printed += temp;
    }
} Serial;
'''
    main = r'''
int main() {
  disconnectWifi();
  assert(disconnects == 1);
  assert(reconnectSuppressed);
  assert(!manualCheckRequested);
  assert(!waitingForClockNotice);
  assert(!sawWifiConnected);
  assert(std::string(connectedSsid) == "--");
  assert(std::string(wifiAddressText) == "--");
  assert(lastStatus == "Wi-Fi disconnected until you reconnect or reboot");
#if defined(ESP_PLANTS_WAVESHARE_7B) && !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
  assert(printed.find("[wifi-disconnect-diag]") != std::string::npos);
  assert(printed.find("call_duration_us=120") != std::string::npos);
#else
  assert(printed.find("[wifi-disconnect-diag]") == std::string::npos);
#endif
  return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        f = Path(tmp)/'test.cpp';f.write_text(fixture+function+main)
        exe = Path(tmp)/'test'
        for args in [ ['-DESP_PLANTS_WAVESHARE_7B'], [], ['-DESP_PLANTS_WAVESHARE_7B','-DESP_PLANTS_DISTRIBUTION_BUILD'] ]:
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*args,str(f),'-o',str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)
