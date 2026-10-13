"""Host and source checks for 7B native Wi-Fi event-to-RGB correlation."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPDATE = (ROOT / 'firmware/waveshare-hub/src/update_service.cpp').read_text()
DRIVER = (ROOT / 'firmware/waveshare-hub/lib/Waveshare_ST7262_LVGL/src/Waveshare_ST7262_LVGL.cpp').read_text()
MAIN = (ROOT / 'firmware/waveshare-hub/src/main.cpp').read_text()


def test_7b_only_and_not_distributed():
    assert '#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)\n// Minimal native-event recorder' in UPDATE
    assert '#if defined(ESP_PLANTS_WAVESHARE_7B) && !defined(ESP_PLANTS_DISTRIBUTION_BUILD)\n  // No Serial' in UPDATE
    assert 'WiFi.onEvent(on7bWifiRadioEvent);' in UPDATE
    assert '[wifi-event-diag]' in UPDATE
    assert '[wifi-event-diag] begin_start_us=' in UPDATE
    assert 'wifiBeginCallUs = static_cast<uint32_t>(esp_timer_get_time())' in UPDATE
    assert '[wifi-disconnect-diag]' in UPDATE
    assert '[wifi-ui-diag]' in MAIN
    assert '[rgb-gap-diag]' in DRIVER


def test_callback_timestamp_and_no_blocking_or_printing():
    callback = UPDATE.split('void on7bWifiRadioEvent(',1)[1].split('\nvoid drain7bWifiRadioEvents()',1)[0]
    assert 'static_cast<uint32_t>(esp_timer_get_time())' in callback
    assert 'info.wifi_sta_disconnected.reason' in callback
    assert 'Serial.' not in callback
    assert 'delay(' not in callback and 'lv_' not in callback
    assert 'portENTER_CRITICAL(&wifiRadioEventMux);' in callback
    assert 'portEXIT_CRITICAL(&wifiRadioEventMux);' in callback


def test_wifi_native_callback_ring_host_compiles():
    if not shutil.which('g++'):
        import pytest
        pytest.skip('g++ unavailable')
    ring = UPDATE.split('struct WifiRadioEventDiag {',1)[1].split('\n#endif  // !ESP_PLANTS_DISTRIBUTION_BUILD',1)[0]
    code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
using WiFiEvent_t = int;
struct WiFiEventInfo_t { struct { uint16_t reason = 0; } wifi_sta_disconnected; };
constexpr int ARDUINO_EVENT_WIFI_STA_START = 1;
constexpr int ARDUINO_EVENT_WIFI_STA_STOP = 2;
constexpr int ARDUINO_EVENT_WIFI_STA_CONNECTED = 3;
constexpr int ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 4;
constexpr int ARDUINO_EVENT_WIFI_STA_GOT_IP = 5;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static uint32_t nowUs = 100000;
int64_t esp_timer_get_time() { return nowUs; }
static std::string printed;
struct SerialT { void printf(const char *format, ...) {
  char temp[256]{};
  va_list arg; va_start(arg, format); vsnprintf(temp, sizeof(temp), format, arg); va_end(arg);
  printed += temp;
} } Serial;
struct WifiRadioEventDiag {
''' + ring + '\n' + r'''
int main() {
  on7bWifiRadioEvent(ARDUINO_EVENT_WIFI_STA_CONNECTED, {});
  nowUs = 100501;
  WiFiEventInfo_t info{};
  info.wifi_sta_disconnected.reason = 8;
  on7bWifiRadioEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
  drain7bWifiRadioEvents();
  assert(printed.find("event_us=100000 event=STA_CONNECTED reason=0") != std::string::npos);
  assert(printed.find("event_us=100501 event=STA_DISCONNECTED reason=8") != std::string::npos);
  const auto previous = printed;
  drain7bWifiRadioEvents();
  assert(printed == previous);
}
'''
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / 'events.cpp'
        exe = Path(directory) / 'events'
        src.write_text(code)
        subprocess.run(['g++', '-std=c++17', str(src), '-o', str(exe)],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(exe)], check=True, capture_output=True, text=True)
