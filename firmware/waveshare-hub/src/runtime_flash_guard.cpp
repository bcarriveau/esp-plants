#include "runtime_flash_guard.h"

#include <Arduino.h>
#include <nvs.h>

#include <Waveshare_ST7262_LVGL.h>

namespace {

portMUX_TYPE gRuntimeFlashMux = portMUX_INITIALIZER_UNLOCKED;

#ifdef ESP_PLANTS_WAVESHARE_7B
// TEMPORARY A/B DIAGNOSTIC, 7B ONLY. Compare Wi-Fi setup/reboot with the
// existing auto-resync baseline. Keep explicit OTA/display resync intact.
// Set false to restore automatic VSYNC resync after each NVS commit.
constexpr bool k7bSuppressNvsAutoRecoveryForABTest = true;
#endif
bool gDisplayReady = false;
bool gDisplayRecoveryPending = false;

void queueRecoveryFromRuntimeCommit() {
  bool shouldLog = false;
  portENTER_CRITICAL(&gRuntimeFlashMux);
  if (gDisplayReady) {
#ifdef ESP_PLANTS_WAVESHARE_7B
    if (!k7bSuppressNvsAutoRecoveryForABTest) {
      gDisplayRecoveryPending = true;
    }
#endif
    shouldLog = true;
  }
  portEXIT_CRITICAL(&gRuntimeFlashMux);

  if (shouldLog) {
#ifdef ESP_PLANTS_WAVESHARE_7B
    Serial.println(k7bSuppressNvsAutoRecoveryForABTest
        ? "[rgb-ab] NVS commit OK; automatic RGB recovery suppressed"
        : "[flash] runtime NVS commit complete; RGB recovery queued");
#else
    Serial.println("[flash] runtime NVS commit complete");
#endif
  }
}

}  // namespace

// Preferences::put*/remove/clear all end in nvs_commit(). Wrapping that one
// C API gives ESP PLANTS a single guard for every runtime Preferences write,
// including writes made by the update/Wi-Fi service, without changing NVS keys
// or persistence schemas. The real flash operation happens first; recovery is
// only requested after the commit has completed.
extern "C" esp_err_t __real_nvs_commit(nvs_handle_t handle);
extern "C" esp_err_t __wrap_nvs_commit(nvs_handle_t handle) {
  const esp_err_t result = __real_nvs_commit(handle);
  if (result == ESP_OK) queueRecoveryFromRuntimeCommit();
  return result;
}

namespace espplants_runtime_flash {

void markDisplayReady() {
  portENTER_CRITICAL(&gRuntimeFlashMux);
  gDisplayReady = true;
  portEXIT_CRITICAL(&gRuntimeFlashMux);
#ifdef ESP_PLANTS_WAVESHARE_7B
  Serial.println(k7bSuppressNvsAutoRecoveryForABTest
      ? "[rgb-ab] 7B TEST: NVS auto-resync OFF; explicit recovery ON"
      : "[rgb-ab] 7B baseline: NVS auto-resync ON; explicit recovery ON");
#endif
}

void requestDisplayRecovery(const char *reason) {
#ifdef ESP_PLANTS_WAVESHARE_7B
  bool ready = false;
  portENTER_CRITICAL(&gRuntimeFlashMux);
  ready = gDisplayReady;
  if (ready) gDisplayRecoveryPending = true;
  portEXIT_CRITICAL(&gRuntimeFlashMux);
  if (ready) {
    Serial.printf("[display] RGB recovery requested: %s\n",
                  reason && reason[0] ? reason : "runtime activity");
  }
#else
  (void)reason;
#endif
}

void serviceDisplayRecovery() {
#ifdef ESP_PLANTS_WAVESHARE_7B
  bool pending = false;
  portENTER_CRITICAL(&gRuntimeFlashMux);
  if (gDisplayRecoveryPending) {
    gDisplayRecoveryPending = false;
    pending = true;
  }
  portEXIT_CRITICAL(&gRuntimeFlashMux);
  if (!pending) return;

  if (restart_rgb_panel_scan()) {
    Serial.println("[display] RGB VSYNC resync queued");
    return;
  }

  // A transient failure should not silently discard the recovery request.
  portENTER_CRITICAL(&gRuntimeFlashMux);
  gDisplayRecoveryPending = true;
  portEXIT_CRITICAL(&gRuntimeFlashMux);
  Serial.println("[display] RGB VSYNC resync request failed; retrying");
#endif
}

}  // namespace espplants_runtime_flash
