"""Host-side tests compile the actual Waveshare persistence service with mock NVS.

No ESP32 toolchain is required. The production function/queue helpers are
extracted unchanged and built as C++ so assertions exercise real control flow.
"""
from pathlib import Path
import shutil
import subprocess
import textwrap

import pytest

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT / "firmware" / "waveshare-hub"
MAIN = WS / "src" / "main.cpp"


def _source_between(text, start, end):
    assert text.count(start) == 1, start
    return text.split(start, 1)[1].split(end, 1)[0]


@pytest.mark.parametrize("display_variant", ["7", "7B"])
def test_deferred_save_recovery_compiled(tmp_path, display_variant):
    if not shutil.which("g++"):
        pytest.skip("g++ unavailable")

    text = MAIN.read_text(encoding="utf-8")
    queues = "void queueUiPersistence" + _source_between(
        text, "void queueUiPersistence", "void logRuntimePreferenceWrite"
    )
    service = "void serviceDeferredPersistence" + _source_between(
        text, "void serviceDeferredPersistence", "void loadRegistry"
    )

    preamble = r'''
        #include <stdint.h>
        #include <stddef.h>
        #include <stdio.h>
        #include <string.h>
        #include <stdarg.h>
        #include "deferred_persistence_retry.h"
        struct MockSerial {
          template<class... Args> void printf(const char *, Args...) {}
        } Serial;
        uint32_t fakeNow = 10000;
        uint32_t millis() { return fakeNow; }
        using portMUX_TYPE = int;
        #define portMUX_INITIALIZER_UNLOCKED 0
        #define portENTER_CRITICAL(x) ((void)0)
        #define portEXIT_CRITICAL(x) ((void)0)
        constexpr size_t kMaxSensors = 4;
        constexpr size_t kMaxInfrastructure = 4;
        constexpr uint32_t kUiPersistenceDelayMs = 1500;
        enum UiPersistenceBits : uint32_t {
          kPersistBrightness = 1u << 0,
          kPersistTemperatureUnit = 1u << 1,
          kPersistPhraseTheme = 1u << 2,
          kPersistDeviceName = 1u << 3,
          kPersistAppearance = 1u << 4,
        };
        portMUX_TYPE uiPersistenceMux = 0;
        uint32_t pendingUiPersistenceBits = 0;
        uint32_t pendingPlantPersistenceMask = 0;
        uint32_t pendingInfrastructurePersistenceMask = 0;
        uint32_t pendingMoisturePersistenceMask = 0;
        uint32_t uiPersistenceDueMs = 0;
        uint8_t uiPersistenceFailedBatches = 0;
        void queueUiPersistence(uint32_t);
        int temperatureCalls = 0, themeCalls = 0, nameCalls = 0;
        int brightnessCalls = 0, plantCalls[4]{}, moistureCalls[4]{}, infraCalls[4]{};
        int failTemperature = 0, failBrightness = 0;
        bool injectDuringWrite = false;
        uint32_t failPlants = 0, failMoisture = 0, failInfrastructure = 0;
        struct MockPreferences {
          size_t putBool(const char *key, bool) {
            if (!strcmp(key, "fahrenheit")) {
              ++temperatureCalls;
              if (injectDuringWrite) {
                injectDuringWrite = false;
                queueUiPersistence(kPersistPhraseTheme);
              }
              if (failTemperature-- > 0) return 0;
            }
            return 1;
          }
          size_t putUChar(const char *key, uint8_t) {
            if (!strcmp(key, "phrase_theme")) ++themeCalls;
            if (!strcmp(key, "bright_lvl")) {
              ++brightnessCalls;
              if (failBrightness-- > 0) return 0;
            }
            return 1;
          }
          size_t putString(const char *, const char *str) {
            ++nameCalls;
            return strlen(str);
          }
        } preferences;
        bool useFahrenheit = false;
        uint8_t brightnessLevel = 4;
        enum class Theme : uint8_t { MIXED };
        Theme phraseTheme = Theme::MIXED;
        char deviceName[] = "ESP PLANTS";
        bool espplants_ui_is_light() { return false; }
        void logRuntimePreferenceWrite(const char *, bool) {}
        bool saveSlot(size_t n) { ++plantCalls[n]; return !(failPlants & (1u << n)); }
        bool saveMoisturePreference(size_t n) {
          ++moistureCalls[n]; return !(failMoisture & (1u << n));
        }
        bool saveInfrastructureSlot(size_t n) {
          ++infraCalls[n]; return !(failInfrastructure & (1u << n));
        }
    '''
    scenarios = r'''
        #define CHECK(v) do { if (!(v)) { fprintf(stderr,"CHECK at line %d: %s\n",__LINE__,#v); return 2;} } while(0)
        int main() {
          using espplants_deferred_persistence::retryDelayMs;
          CHECK(retryDelayMs(1) == 5000);
          CHECK(retryDelayMs(2) == 10000);
          CHECK(retryDelayMs(3) == 20000);
          CHECK(retryDelayMs(4) == 40000);
          CHECK(retryDelayMs(5) == 60000);
          CHECK(retryDelayMs(255) == 60000);
          espplants_deferred_persistence::Masks a, b;
          a.plants = 2; b.plants = 4; b.settings = 8;
          a.merge(b);
          CHECK(a.plants == 6 && a.settings == 8 && a.any());

          // Failing one setting does not force successful settings to retry.
          failTemperature = 1;
          pendingUiPersistenceBits = kPersistTemperatureUnit | kPersistDeviceName;
          serviceDeferredPersistence();
          CHECK(temperatureCalls == 1 && nameCalls == 1);
          CHECK(pendingUiPersistenceBits == kPersistTemperatureUnit);
          CHECK(uiPersistenceDueMs == fakeNow + 5000);
          CHECK(uiPersistenceFailedBatches == 1);
          fakeNow += 4999;
          serviceDeferredPersistence();
          CHECK(temperatureCalls == 1);
          fakeNow += 1;
          serviceDeferredPersistence();
          CHECK(temperatureCalls == 2 && nameCalls == 1);
          CHECK(!pendingUiPersistenceBits && uiPersistenceFailedBatches == 0);

          // A separate edit arriving *during* a failed write must not be
          // wiped out, and its 1500ms debounce must not be postponed.
          pendingUiPersistenceBits = kPersistTemperatureUnit;
          failTemperature = 1;
          injectDuringWrite = true;
          serviceDeferredPersistence();
          CHECK(pendingUiPersistenceBits == (kPersistTemperatureUnit | kPersistPhraseTheme));
          CHECK(uiPersistenceDueMs == fakeNow + kUiPersistenceDelayMs);
          fakeNow += kUiPersistenceDelayMs;
          serviceDeferredPersistence();
          CHECK(!pendingUiPersistenceBits && themeCalls == 1);

          // Independently retry a failing plant record, soil preference,
          // and repeater record; never redo successful slots.
          pendingPlantPersistenceMask = (1u << 0) | (1u << 1);
          pendingMoisturePersistenceMask = (1u << 2);
          pendingInfrastructurePersistenceMask = (1u << 3);
          failPlants = 1u << 1;
          failMoisture = 1u << 2;
          failInfrastructure = 1u << 3;
          serviceDeferredPersistence();
          CHECK(pendingPlantPersistenceMask == (1u << 1));
          CHECK(pendingMoisturePersistenceMask == (1u << 2));
          CHECK(pendingInfrastructurePersistenceMask == (1u << 3));
          CHECK(plantCalls[0] == 1 && plantCalls[1] == 1);
          failPlants = failMoisture = failInfrastructure = 0;
          fakeNow = uiPersistenceDueMs;
          serviceDeferredPersistence();
          CHECK(plantCalls[0] == 1 && plantCalls[1] == 2);
          CHECK(moistureCalls[2] == 2 && infraCalls[3] == 2);
          CHECK(!pendingPlantPersistenceMask && !pendingMoisturePersistenceMask);
          CHECK(!pendingInfrastructurePersistenceMask);

          // On 7B the brightness key must take the same retry path.
          #ifdef ESP_PLANTS_WAVESHARE_7B
          pendingUiPersistenceBits = kPersistBrightness;
          failBrightness = 1;
          serviceDeferredPersistence();
          CHECK(pendingUiPersistenceBits == kPersistBrightness);
          fakeNow = uiPersistenceDueMs;
          serviceDeferredPersistence();
          CHECK(brightnessCalls == 2 && !pendingUiPersistenceBits);
          #endif

          // Long-lasting NVS failure backs off; a success resets the streak.
          pendingUiPersistenceBits = kPersistTemperatureUnit;
          failTemperature = 6;
          for (uint8_t attempt = 1; attempt <= 6; ++attempt) {
            serviceDeferredPersistence();
            CHECK(uiPersistenceFailedBatches == attempt);
            CHECK(uiPersistenceDueMs == fakeNow + retryDelayMs(attempt));
            fakeNow = uiPersistenceDueMs;
          }
          serviceDeferredPersistence();
          CHECK(uiPersistenceFailedBatches == 0 && !pendingUiPersistenceBits);
          return 0;
        }
    '''
    cpp = tmp_path / "persistence.cpp"
    binary = tmp_path / "persistence_host"
    cpp.write_text(preamble + "\n" + queues + "\n" + service + "\n" + scenarios)
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if display_variant == "7B":
        flags += ["-DESP_PLANTS_WAVESHARE_7B=1"]
    subprocess.run(
        ["g++", *flags, "-I", str(WS / "include"), str(cpp), "-o", str(binary)],
        check=True, capture_output=True, text=True,
    )
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)


def test_direct_registry_writes_retry_without_schema_changes():
    main = MAIN.read_text(encoding="utf-8")
    assert "if (!saveSlot(slot)) queuePlantPersistence(slot);" in main
    assert "if (!saveInfrastructureSlot(slot)) queueInfrastructurePersistence(slot);" in main
    assert "bool saveMoisturePreference(size_t slot)" in main
    assert "bool saveSlot(size_t slot)" in main
    assert "bool saveInfrastructureSlot(size_t slot)" in main
    assert "kPlantRecordVersion" in main
    ini = (WS / "platformio.ini").read_text(encoding="utf-8")
    assert "upload_port = COM11" in ini
    assert "lvgl/lvgl@8.3.11" in ini
