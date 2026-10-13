"""7B NVS auto-resync A/B experiment; exercise the actual guard with host C++ stubs."""
from pathlib import Path
import shutil
import subprocess

import pytest

HUB = Path(__file__).resolve().parents[1] / 'firmware' / 'waveshare-hub'
SOURCE = HUB / 'src' / 'runtime_flash_guard.cpp'
PIO = HUB / 'platformio.ini'
DRIVER = HUB / 'lib' / 'Waveshare_ST7262_LVGL' / 'src' / 'Waveshare_ST7262_LVGL.cpp'


def test_safety_scope_is_one_7b_only_auto_queue_gate():
    text = SOURCE.read_text(encoding='utf-8')
    assert '#ifdef ESP_PLANTS_WAVESHARE_7B' in text
    assert 'k7bSuppressNvsAutoRecoveryForABTest = true;' in text
    assert 'if (!k7bSuppressNvsAutoRecoveryForABTest)' in text
    assert 'if (result == ESP_OK) queueRecoveryFromRuntimeCommit();' in text
    assert 'gDisplayRecoveryPending = true;' in text
    assert 'esp_lcd_rgb_panel_restart' not in text  # keep LCD driver separate
    assert 'restart_rgb_panel_scan()' in text
    assert '[rgb-ab] 7B TEST: NVS auto-resync OFF; explicit recovery ON' in text
    assert '[rgb-ab] NVS commit OK; automatic RGB recovery suppressed' in text
    pio = PIO.read_text(encoding='utf-8')
    assert 'upload_port = COM11' in pio
    assert 'upload_port = COM13' in pio
    assert 'post:scripts/use_7b_vsync_sdk.py' in pio
    assert 'max_frame_gap_us=%lu' in DRIVER.read_text(encoding='utf-8')  # preserve previous timing diagnostics


@pytest.mark.skipif(shutil.which('g++') is None, reason='g++ unavailable (host simulation)')
def test_real_guard_ab_behavior_under_host_cpp_stubs(tmp_path):
    inc = tmp_path / 'stubs'
    inc.mkdir()
    (inc / 'Arduino.h').write_text('''
#pragma once
#include <cstdarg>
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
struct TestSerial { void println(const char *) {} void printf(const char *, ...) {} };
extern TestSerial Serial;
''')
    (inc / 'nvs.h').write_text('''
#pragma once
using nvs_handle_t = int;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
''')
    (inc / 'Waveshare_ST7262_LVGL.h').write_text('''
#pragma once
bool restart_rgb_panel_scan();
''')
    harness = tmp_path / 'harness.cpp'
    harness.write_text(r'''
#include "Arduino.h"
#include "nvs.h"
#include "runtime_flash_guard.h"

TestSerial Serial;
static int restartCount = 0;
static bool restartSucceeds = true;
static esp_err_t commitResult = ESP_OK;
extern "C" esp_err_t __real_nvs_commit(nvs_handle_t) { return commitResult; }
extern "C" esp_err_t __wrap_nvs_commit(nvs_handle_t);
bool restart_rgb_panel_scan() { ++restartCount; return restartSucceeds; }

int main() {
  using namespace espplants_runtime_flash;
  __wrap_nvs_commit(1);
  serviceDisplayRecovery();
  if (restartCount != 0) return 1;  // Before display init, no restart
  markDisplayReady();
  commitResult = 1;
  __wrap_nvs_commit(1);
  serviceDisplayRecovery();
  if (restartCount != 0) return 2;  // Failed commit must never queue recovery
  commitResult = ESP_OK;
  __wrap_nvs_commit(1);
  serviceDisplayRecovery();
#if defined(ESP_PLANTS_WAVESHARE_7B)
  if (restartCount != AB_NVS_RESTARTS) return 3;
  requestDisplayRecovery("OTA worker idle");
  restartSucceeds = false;
  serviceDisplayRecovery();
  if (restartCount != AB_NVS_RESTARTS + 1) return 4;
  restartSucceeds = true;
  serviceDisplayRecovery();
  if (restartCount != AB_NVS_RESTARTS + 2) return 5;  // Retry still works
#else
  if (restartCount != 0) return 6;
  requestDisplayRecovery("OTA worker idle");
  serviceDisplayRecovery();
  if (restartCount != 0) return 7;  // Original 7 unchanged
#endif
  return 0;
}
''')

    def run_case(name, code, build_flags, expected):
        source = tmp_path / (name + '.cpp')
        source.write_text(code)
        exe = tmp_path / name
        result = subprocess.run([
            'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-I', str(inc),
            '-I', str(HUB / 'include'), '-DAB_NVS_RESTARTS=' + str(expected),
            *build_flags, str(source), str(harness), '-o', str(exe),
        ], text=True, capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
        run = subprocess.run([str(exe)], text=True, capture_output=True)
        assert run.returncode == 0, f'{name}: {run.stderr} exit={run.returncode}'

    code = SOURCE.read_text(encoding='utf-8')
    run_case('7b_test_off', code, ['-DESP_PLANTS_WAVESHARE_7B'], 0)
    run_case('7b_baseline_on', code.replace('k7bSuppressNvsAutoRecoveryForABTest = true;',
                                          'k7bSuppressNvsAutoRecoveryForABTest = false;'),
             ['-DESP_PLANTS_WAVESHARE_7B'], 1)
    run_case('original_7_unchanged', code, [], 0)
