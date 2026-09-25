#include <Arduino.h>

#include <esp_attr.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdint.h>
#include <stdlib.h>

#include "build_version.h"

namespace espplants_crash_diag {
namespace {

constexpr char kDiagnosticBuild[] = ESP_PLANTS_WAVESHARE_BUILD_ID;
constexpr uint32_t kRtcMagic = 0x45504447u;  // EPDG
constexpr uint32_t kStartupQuietMs = 2500u;
constexpr uint32_t kLoopWarnMs = 3000u;
constexpr uint32_t kLoopSecondWarnMs = 8000u;
constexpr uint32_t kLoopForceDumpMs = 15000u;
constexpr uint32_t kHealthIntervalMs = 30000u;
constexpr uint32_t kMonitorPollMs = 100u;
constexpr uint32_t kDiagStackBytes = 4096u;
constexpr UBaseType_t kDiagPriority = 1;
constexpr BaseType_t kDiagCore = 0;

#if defined(ESP_PLANTS_DIAGNOSTIC_PANIC)
constexpr bool kAggressivePanicEnabled = true;
#else
constexpr bool kAggressivePanicEnabled = false;
#endif

enum class MainPhase : uint32_t {
  Unknown = 0, Loop = 1, UpdateService = 2, UiLockWait = 3,
  UiLockHeld = 4, UiUnlock = 5,
};

struct RtcCrashBreadcrumb {
  uint32_t magic; uint32_t magicInverse; uint32_t bootCount;
  uint32_t forcedDumpCount; uint32_t lastResetReason; uint32_t lastPhase;
  uint32_t lastLoopCount; uint32_t lastUptimeMs;
};

RTC_NOINIT_ATTR RtcCrashBreadcrumb rtcBreadcrumb;
TaskHandle_t loopTask = nullptr;
TaskHandle_t monitorTask = nullptr;
volatile uint32_t mainPhase = static_cast<uint32_t>(MainPhase::Unknown);
volatile uint32_t loopActive = 0;
volatile uint32_t loopEnteredAtMs = 0;
volatile uint32_t loopLastCompletedAtMs = 0;
volatile uint32_t loopCount = 0;
volatile uint32_t recoveredStallMs = 0;
volatile uint32_t recoveredStallPending = 0;
volatile uint32_t lvglHandlerActive = 0;
volatile uint32_t lvglHandlerEnteredAtMs = 0;
volatile uint32_t lvglHandlerLastCompletedAtMs = 0;
volatile uint32_t lvglHandlerCount = 0;
volatile uint32_t started = 0;

const char *phaseName(MainPhase phase) {
  switch (phase) {
    case MainPhase::Loop: return "LOOP";
    case MainPhase::UpdateService: return "UPDATE_SERVICE";
    case MainPhase::UiLockWait: return "UI_LOCK_WAIT";
    case MainPhase::UiLockHeld: return "UI_LOCK_HELD";
    case MainPhase::UiUnlock: return "UI_UNLOCK";
    case MainPhase::Unknown:
    default: return "UNKNOWN";
  }
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_UNKNOWN:
    default: return "UNKNOWN";
  }
}

void initializeRtcBreadcrumb() {
  if (rtcBreadcrumb.magic != kRtcMagic || rtcBreadcrumb.magicInverse != ~kRtcMagic) {
    rtcBreadcrumb = {};
    rtcBreadcrumb.magic = kRtcMagic;
    rtcBreadcrumb.magicInverse = ~kRtcMagic;
  }
  ++rtcBreadcrumb.bootCount;
  rtcBreadcrumb.lastResetReason = static_cast<uint32_t>(esp_reset_reason());
}

void setMainPhase(MainPhase phase) {
  mainPhase = static_cast<uint32_t>(phase);
  rtcBreadcrumb.lastPhase = mainPhase;
  rtcBreadcrumb.lastUptimeMs = millis();
}

void printHeapLine(const char *prefix) {
  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("[diag] %s heap internal free=%u min=%u largest=%u psram_free=%u\n",
                prefix, static_cast<unsigned>(internalFree),
                static_cast<unsigned>(internalMin), static_cast<unsigned>(internalLargest),
                static_cast<unsigned>(psramFree));
}

void printCoreDumpState() {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
  const esp_err_t check = esp_core_dump_image_check();
  if (check == ESP_OK) {
    size_t address = 0; size_t size = 0;
    const esp_err_t getResult = esp_core_dump_image_get(&address, &size);
    if (getResult == ESP_OK) {
      Serial.printf("[diag] previous coredump VALID flash=0x%08X size=%u bytes\n",
                    static_cast<unsigned>(address), static_cast<unsigned>(size));
      Serial.println("[diag] run tools\\read-waveshare-crashdump.ps1 before erasing/flashing if you want this dump");
    } else {
      Serial.printf("[diag] coredump valid but metadata read failed: %s\n", esp_err_to_name(getResult));
    }
  } else {
    Serial.printf("[diag] previous coredump: none/invalid (%s)\n", esp_err_to_name(check));
  }
#else
  Serial.println("[diag] WARNING: flash coredump support is disabled in this framework build");
#endif
}

void printBootDiagnostics() {
  const esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[diag] %s online\n", kDiagnosticBuild);
  Serial.printf("[diag] reset=%s(%d) rtc_boots=%lu forced_dumps=%lu\n",
                resetReasonName(reason), static_cast<int>(reason),
                static_cast<unsigned long>(rtcBreadcrumb.bootCount),
                static_cast<unsigned long>(rtcBreadcrumb.forcedDumpCount));
  if (rtcBreadcrumb.bootCount > 1u) {
    Serial.printf("[diag] previous breadcrumb phase=%s loop=%lu uptime=%lums\n",
                  phaseName(static_cast<MainPhase>(rtcBreadcrumb.lastPhase)),
                  static_cast<unsigned long>(rtcBreadcrumb.lastLoopCount),
                  static_cast<unsigned long>(rtcBreadcrumb.lastUptimeMs));
  }
  printHeapLine("boot");
  printCoreDumpState();
  if (kAggressivePanicEnabled) {
    Serial.printf("[diag] stall policy: warn=%lus repeat=%lus diagnostic-panic=%lus (UPDATE_SERVICE is exempt)\n",
                  static_cast<unsigned long>(kLoopWarnMs / 1000u),
                  static_cast<unsigned long>(kLoopSecondWarnMs / 1000u),
                  static_cast<unsigned long>(kLoopForceDumpMs / 1000u));
  } else {
    Serial.printf("[diag] stall policy: warn=%lus repeat=%lus production-safe logging only; automatic diagnostic panic disabled\n",
                  static_cast<unsigned long>(kLoopWarnMs / 1000u),
                  static_cast<unsigned long>(kLoopSecondWarnMs / 1000u));
  }
}

#if defined(ESP_PLANTS_DIAGNOSTIC_PANIC)
void forceDiagnosticPanic(uint32_t stalledMs, MainPhase phase) {
  ++rtcBreadcrumb.forcedDumpCount;
  rtcBreadcrumb.lastPhase = static_cast<uint32_t>(phase);
  rtcBreadcrumb.lastLoopCount = loopCount;
  rtcBreadcrumb.lastUptimeMs = millis();
  Serial.printf("[diag] FATAL: main loop stalled %lums in %s; forcing panic so flash coredump captures all task stacks\n",
                static_cast<unsigned long>(stalledMs), phaseName(phase));
  printHeapLine("pre-panic");
  Serial.flush(); delay(25); abort();
}
#endif

void monitorTaskMain(void *) {
  delay(kStartupQuietMs);
  printBootDiagnostics();
  uint32_t lastHealthMs = millis();
  uint8_t warningLevel = 0;
  bool lvglMissingReported = false;
  for (;;) {
    const uint32_t now = millis();
    if (recoveredStallPending) {
      const uint32_t recovered = recoveredStallMs;
      recoveredStallPending = 0;
      Serial.printf("[diag] RECOVERED: main loop resumed after %lums\n", static_cast<unsigned long>(recovered));
      warningLevel = 0;
    }
    if (loopActive) {
      const uint32_t age = now - loopEnteredAtMs;
      const MainPhase phase = static_cast<MainPhase>(mainPhase);
      if (age >= kLoopWarnMs && warningLevel < 1u) {
        warningLevel = 1u;
        const uint32_t lvglAge = lvglHandlerActive ? now - lvglHandlerEnteredAtMs : now - lvglHandlerLastCompletedAtMs;
        Serial.printf("[diag] STALL: main loop=%lums phase=%s lvgl_active=%u lvgl_age=%lums loop=%lu\n",
                      static_cast<unsigned long>(age), phaseName(phase), static_cast<unsigned>(lvglHandlerActive),
                      static_cast<unsigned long>(lvglAge), static_cast<unsigned long>(loopCount));
        printHeapLine("stall");
      }
      if (age >= kLoopSecondWarnMs && warningLevel < 2u) {
        warningLevel = 2u;
        if (kAggressivePanicEnabled && phase != MainPhase::UpdateService) {
          Serial.printf("[diag] STALL CONTINUES: %lums phase=%s; diagnostic build will panic at %lus\n",
                        static_cast<unsigned long>(age), phaseName(phase),
                        static_cast<unsigned long>(kLoopForceDumpMs / 1000u));
        } else {
          Serial.printf("[diag] STALL CONTINUES: %lums phase=%s; automatic panic is disabled for this build/phase\n",
                        static_cast<unsigned long>(age), phaseName(phase));
        }
      }
#if defined(ESP_PLANTS_DIAGNOSTIC_PANIC)
      if (age >= kLoopForceDumpMs && phase != MainPhase::UpdateService) forceDiagnosticPanic(age, phase);
#endif
    } else warningLevel = 0;
    const uint32_t lvglLast = lvglHandlerLastCompletedAtMs;
    if (lvglLast && now - lvglLast > 5000u && !lvglHandlerActive) {
      if (!lvglMissingReported) {
        lvglMissingReported = true;
        Serial.printf("[diag] WARNING: LVGL timer handler has not completed for %lums\n",
                      static_cast<unsigned long>(now - lvglLast));
      }
    } else lvglMissingReported = false;
    if (now - lastHealthMs >= kHealthIntervalMs) {
      lastHealthMs = now;
      const uint32_t lvglAge = lvglHandlerActive ? now - lvglHandlerEnteredAtMs : now - lvglHandlerLastCompletedAtMs;
      Serial.printf("[diag] health phase=%s loop=%lu loop_active=%u lvgl_active=%u lvgl_age=%lums lvgl_calls=%lu\n",
                    phaseName(static_cast<MainPhase>(mainPhase)), static_cast<unsigned long>(loopCount),
                    static_cast<unsigned>(loopActive), static_cast<unsigned>(lvglHandlerActive),
                    static_cast<unsigned long>(lvglAge), static_cast<unsigned long>(lvglHandlerCount));
      printHeapLine("health");
    }
    vTaskDelay(pdMS_TO_TICKS(kMonitorPollMs));
  }
}

void ensureStarted() {
  if (started) return;
  started = 1; initializeRtcBreadcrumb(); loopTask = xTaskGetCurrentTaskHandle();
  const uint32_t now = millis(); loopLastCompletedAtMs = now; lvglHandlerLastCompletedAtMs = now; setMainPhase(MainPhase::Loop);
  const BaseType_t result = xTaskCreatePinnedToCore(monitorTaskMain, "plants_diag", kDiagStackBytes, nullptr, kDiagPriority, &monitorTask, kDiagCore);
  if (result != pdPASS) { monitorTask = nullptr; Serial.println("[diag] ERROR: diagnostic monitor task could not start"); }
}
void loopEnter() { ensureStarted(); loopActive = 1; loopEnteredAtMs = millis(); setMainPhase(MainPhase::Loop); }
void loopExit() {
  const uint32_t now = millis(); const uint32_t duration = now - loopEnteredAtMs;
  if (duration >= kLoopWarnMs) { recoveredStallMs = duration; recoveredStallPending = 1; }
  ++loopCount; rtcBreadcrumb.lastLoopCount = loopCount; rtcBreadcrumb.lastUptimeMs = now;
  loopLastCompletedAtMs = now; loopActive = 0; setMainPhase(MainPhase::Loop);
}
bool onLoopTask() { return loopTask && xTaskGetCurrentTaskHandle() == loopTask; }
}  // namespace

void beforeUpdateService() { if (onLoopTask()) setMainPhase(MainPhase::UpdateService); }
void afterUpdateService() { if (onLoopTask()) setMainPhase(MainPhase::Loop); }
void beforeUiLock() { if (onLoopTask()) setMainPhase(MainPhase::UiLockWait); }
void afterUiLock(bool locked) { if (onLoopTask()) setMainPhase(locked ? MainPhase::UiLockHeld : MainPhase::Loop); }
void beforeUiUnlock() { if (onLoopTask()) setMainPhase(MainPhase::UiUnlock); }
void afterUiUnlock() { if (onLoopTask()) setMainPhase(MainPhase::Loop); }
void lvglEnter() { lvglHandlerActive = 1; lvglHandlerEnteredAtMs = millis(); }
void lvglExit() { ++lvglHandlerCount; lvglHandlerLastCompletedAtMs = millis(); lvglHandlerActive = 0; }
void wrappedLoopEnter() { loopEnter(); }
void wrappedLoopExit() { loopExit(); }
}  // namespace espplants_crash_diag

extern "C" void __real__Z4loopv(void);
extern "C" void __wrap__Z4loopv(void) { espplants_crash_diag::wrappedLoopEnter(); __real__Z4loopv(); espplants_crash_diag::wrappedLoopExit(); }
extern "C" void __real__ZN16espplants_update7serviceEv(void);
extern "C" void __wrap__ZN16espplants_update7serviceEv(void) { espplants_crash_diag::beforeUpdateService(); __real__ZN16espplants_update7serviceEv(); espplants_crash_diag::afterUpdateService(); }
extern "C" bool __real_lvgl_port_lock(int timeout_ms);
extern "C" bool __wrap_lvgl_port_lock(int timeout_ms) { espplants_crash_diag::beforeUiLock(); const bool locked = __real_lvgl_port_lock(timeout_ms); espplants_crash_diag::afterUiLock(locked); return locked; }
extern "C" bool __real_lvgl_port_unlock(void);
extern "C" bool __wrap_lvgl_port_unlock(void) { espplants_crash_diag::beforeUiUnlock(); const bool result = __real_lvgl_port_unlock(); espplants_crash_diag::afterUiUnlock(); return result; }
extern "C" uint32_t __real_lv_timer_handler(void);
extern "C" uint32_t __wrap_lv_timer_handler(void) { espplants_crash_diag::lvglEnter(); const uint32_t next = __real_lv_timer_handler(); espplants_crash_diag::lvglExit(); return next; }
