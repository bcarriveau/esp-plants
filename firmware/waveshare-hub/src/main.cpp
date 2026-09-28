#include <Arduino.h>
#include <Preferences.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <src/extra/libs/qrcode/qrcodegen.h>

#include "plantlink.h"
#include "advanced_virtual_list.h"
#include "all_virtual_list.h"
#include "home_virtual_list.h"
#include "phrase_engine.h"
#include "update_service.h"
#include "sensor_route_view.h"
#include "ui/screens.h"

namespace {

constexpr uint32_t kPlantLinkBaud = 115200;
constexpr int kPlantLinkRxPin = 44;
constexpr int kPlantLinkTxPin = 43;
constexpr uint32_t kHelloIntervalMs = 1500;
constexpr uint32_t kLinkTimeoutMs = 7000;
constexpr uint32_t kUiRefreshIntervalMs = 1000;
constexpr uint32_t kHomeManualSelectionMs = 30 * 1000;
constexpr size_t kMaxSensors = 32;
constexpr size_t kMaxInfrastructure = 32;
constexpr size_t kPlantNameBytes = 24;
constexpr size_t kInfrastructureNameBytes = 24;
constexpr size_t kDeviceNameBytes = 24;
constexpr uint32_t kPlantRecordMagic = 0x504C4E54u;  // PLNT
constexpr uint8_t kPlantRecordVersion = 1;
constexpr uint32_t kInfrastructureRecordMagic = 0x52505452u;  // RPTR
constexpr uint8_t kInfrastructureRecordVersion = 1;
constexpr lv_coord_t kSetupQrOuterSize = 116;
constexpr lv_coord_t kSetupQrSize = 108;
constexpr uint8_t kSetupQrMaxVersion = 5;
constexpr uint8_t kSetupQrQuietModules = 4;
constexpr size_t kSetupQrPaletteBytes = 8U;
constexpr size_t kSetupQrRowBytes =
    (static_cast<size_t>(kSetupQrSize) + 7U) / 8U;
constexpr size_t kSetupQrCanvasBufferBytes =
    LV_IMG_BUF_SIZE_INDEXED_1BIT(kSetupQrSize, kSetupQrSize);
constexpr size_t kSetupQrEncodeBufferBytes =
    (((kSetupQrMaxVersion * 4U + 17U) *
      (kSetupQrMaxVersion * 4U + 17U) + 7U) / 8U) + 1U;
static_assert(kSetupQrEncodeBufferBytes == 173U,
              "Unexpected ESP PLANTS setup QR encoder buffer size");

enum class Page : uint8_t { Home = 0, All = 1, Plant = 2, Settings = 3, Advanced = 4 };
enum class RenameTarget : uint8_t { Plant = 0, Device = 1, Infrastructure = 2 };
enum class PairDialogState : uint8_t {
  Hidden = 0,
  Pairing = 1,
  Found = 2,
  TimedOut = 3,
  RemoveConfirm = 4,
};

struct PersistedPlant {
  uint32_t magic = kPlantRecordMagic;
  uint8_t version = kPlantRecordVersion;
  uint8_t ieee[8]{};
  char name[kPlantNameBytes]{};
};

struct PersistedInfrastructure {
  uint32_t magic = kInfrastructureRecordMagic;
  uint8_t version = kInfrastructureRecordVersion;
  uint8_t ieee[8]{};
  char name[kInfrastructureNameBytes]{};
};

struct PlantSensor {
  sensor_route_view::Route route{};  // RAM only; never part of PersistedPlant.
  bool used = false;
  bool seenThisBoot = false;
  uint8_t ieee[8]{};
  uint16_t shortAddress = 0xffff;
  uint16_t fieldFlags = 0;  // merged values known in RAM
  uint16_t reportedFieldFlagsThisBoot = 0;
  espplants_phrases::Rotation phraseRotation{};
  int16_t temperatureCentiC = 0;
  uint16_t humidityCentiPct = 0;
  uint8_t soilMoisturePct = 0;
  uint8_t batteryPct = 0;
  uint8_t waterWarning = 0;
  uint8_t lqi = 0;
  int8_t rssi = plantlink::kRssiUnavailableDbm;
  uint32_t lastSeenMs = 0;
  char name[kPlantNameBytes]{};
};

struct PlantListRow {
  lv_obj_t *box = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *moisture = nullptr;
  lv_obj_t *bar = nullptr;
  int boundSlot = -1;
  size_t boundLogicalIndex = kMaxSensors;
  char nameText[kPlantNameBytes]{};
  char moistureText[8]{};
};

struct AllSensorRow {
  lv_obj_t *box = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *moisture = nullptr;
  lv_obj_t *battery = nullptr;
  lv_obj_t *updated = nullptr;
  int boundSlot = -1;
  size_t boundLogicalIndex = kMaxSensors;
  char moistureText[8]{};
  char batteryText[8]{};
};

struct InfrastructureNode {
  bool used = false;
  bool online = false;
  uint8_t ieee[8]{};
  uint16_t shortAddress = 0xffff;
  uint8_t flags = 0;
  uint8_t deviceType = 0;
  uint8_t lqi = 0;
  int8_t rssi = plantlink::kRssiUnavailableDbm;
  uint32_t lastSeenMs = 0;
  char name[kInfrastructureNameBytes]{};
};

struct InfrastructureRow {
  lv_obj_t *box = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *signal = nullptr;
  int boundSlot = -1;
  size_t boundLogicalIndex = kMaxInfrastructure;
  char nameText[kInfrastructureNameBytes]{};
  char statusText[8]{};
  char signalText[16]{};
};

plantlink::Decoder decoder;
Preferences preferences;
PlantSensor sensors[kMaxSensors];
PlantListRow rows[espplants_home_virtual_list::kPoolSize];
AllSensorRow allRows[espplants_all_virtual_list::kPoolSize];
InfrastructureNode infrastructure[kMaxInfrastructure];
InfrastructureRow infrastructureRows[espplants_advanced_virtual_list::kPoolSize];

uint16_t nextSequence = 1;
uint32_t lastHelloMs = 0;
uint32_t lastH2RxMs = 0;
uint32_t lastUiRefreshMs = 0;
bool h2Online = false;
bool haveH2Uptime = false;
uint32_t lastH2Uptime = 0;
bool networkReady = false;
bool useFahrenheit = true;
// Alpha.23 persistent phrase theme selector
espplants_phrases::Theme phraseTheme = espplants_phrases::Theme::MIXED;
struct UiDirtyState {
  bool header = true;
  bool sensorOrder = true;
  bool home = true;
  bool all = true;
  bool plant = true;
  bool settings = true;
  bool advanced = true;
  bool update = true;
  bool pair = true;
};

UiDirtyState dirty;
bool updateModalOpen = false;

void markSensorRegistryDirty() {
  dirty.header = true;
  dirty.sensorOrder = true;
  dirty.home = true;
  dirty.all = true;
  dirty.plant = true;
  dirty.settings = true;
}

void markSensorValuesDirty(bool orderMayChange) {
  if (orderMayChange) dirty.sensorOrder = true;
  dirty.home = true;
  dirty.all = true;
  dirty.plant = true;
}

void markInfrastructureDirty() {
  dirty.advanced = true;
  dirty.plant = true;
}

void markNetworkStateDirty() {
  dirty.settings = true;
  dirty.plant = true;
  dirty.update = true;
  dirty.pair = true;
}
uint8_t zigbeeChannel = 0;
uint8_t h2SensorCount = 0;
uint8_t h2InfrastructureCount = 0;
uint8_t permitJoinRemaining = 0;
// Alpha.21 H2 firmware identity display. Populated only from H2 HelloAck.
char h2BuildId[96]{};
int selectedSensor = -1;
int manualHomeSensor = -1;
uint32_t manualHomeUntilMs = 0;
char deviceName[kDeviceNameBytes] = "ESP PLANTS";
Page currentPage = Page::Home;
size_t sortedSensorSlots[kMaxSensors]{};
size_t sortedSensorCount = 0;

PairDialogState pairDialogState = PairDialogState::Hidden;
bool pairInfrastructure = false;
bool pairRemovingInfrastructure = false;
int pairFoundInfrastructure = -1;
int selectedInfrastructure = -1;
bool pairReplacing = false;
int pairTargetSlot = -1;
int pairFoundSlot = -1;
uint32_t pairStartedMs = 0;
// Alpha.18 permit-join stale-status guard: ignore a queued pre-request zero
// briefly while waiting for the H2 to acknowledge a new nonzero join window.
uint32_t permitJoinGuardUntilMs = 0;

lv_obj_t *homePage = nullptr;
lv_obj_t *allPage = nullptr;
lv_obj_t *plantPage = nullptr;
lv_obj_t *settingsPage = nullptr;
lv_obj_t *advancedPage = nullptr;
lv_obj_t *headerTitle = nullptr;
lv_obj_t *headerCount = nullptr;
lv_obj_t *headerUpdateButton = nullptr;
lv_obj_t *navHome = nullptr;
lv_obj_t *navAll = nullptr;
lv_obj_t *navPlant = nullptr;
lv_obj_t *navSettings = nullptr;

lv_obj_t *homeName = nullptr;
lv_obj_t *homeMood = nullptr;
lv_obj_t *homeSoil = nullptr;
lv_obj_t *homeTemp = nullptr;
lv_obj_t *homeHumidity = nullptr;
lv_obj_t *homeBar = nullptr;
lv_obj_t *homeWarning = nullptr;
lv_obj_t *homeSummary = nullptr;
lv_obj_t *homeList = nullptr;
lv_obj_t *homeVirtualContent = nullptr;
size_t homeLogicalSlots[kMaxSensors]{};
size_t homeLogicalCount = 0;
size_t homeFirstLogicalIndex = kMaxSensors;
bool homeVirtualBinding = false;

lv_obj_t *allSummary = nullptr;
lv_obj_t *allList = nullptr;
lv_obj_t *allVirtualContent = nullptr;
size_t allLogicalSlots[kMaxSensors]{};
size_t allLogicalCount = 0;
size_t allFirstLogicalIndex = kMaxSensors;
bool allVirtualBinding = false;

lv_obj_t *detailSlot = nullptr;
lv_obj_t *detailName = nullptr;
lv_obj_t *detailMood = nullptr;
lv_obj_t *detailIeee = nullptr;
lv_obj_t *detailSoil = nullptr;
lv_obj_t *detailTemp = nullptr;
lv_obj_t *detailHumidity = nullptr;
lv_obj_t *detailBattery = nullptr;
lv_obj_t *detailSignal = nullptr;
lv_obj_t *detailUpdated = nullptr;
lv_obj_t *detailBar = nullptr;
lv_obj_t *detailWarning = nullptr;
lv_obj_t *renameButton = nullptr;
lv_obj_t *replaceButton = nullptr;
lv_obj_t *removeButton = nullptr;

lv_obj_t *settingsH2 = nullptr;
lv_obj_t *settingsZigbee = nullptr;
lv_obj_t *settingsPlants = nullptr;
lv_obj_t *settingsUnit = nullptr;
lv_obj_t *settingsTheme = nullptr;
// Personality selection reuses the existing rename modal/button matrix so
// it does not add another LVGL object tree.
espplants_phrases::Theme pendingTheme = espplants_phrases::Theme::MIXED;
lv_obj_t *settingsPair = nullptr;
lv_obj_t *settingsDeviceName = nullptr;
lv_obj_t *updateModal = nullptr;
lv_obj_t *updateWifiState = nullptr;
lv_obj_t *updateWifiDetail = nullptr;
lv_obj_t *updateSetupButton = nullptr;
lv_obj_t *updateSetupLabel = nullptr;
lv_obj_t *updatePortalInfo = nullptr;
lv_obj_t *updateQrCard = nullptr;
lv_obj_t *updateQrCode = nullptr;
lv_obj_t *updateQrHint = nullptr;
uint8_t updateQrCanvasBuffer[kSetupQrCanvasBufferBytes]{};
uint8_t updateQrTempBuffer[kSetupQrEncodeBufferBytes]{};
uint8_t updateQrEncodedBuffer[kSetupQrEncodeBufferBytes]{};
char updateQrPayload[128]{};
lv_obj_t *updateCurrentVersion = nullptr;
lv_obj_t *updateLatestVersion = nullptr;
lv_obj_t *updateH2Version = nullptr;
lv_obj_t *updateStatus = nullptr;
lv_obj_t *updateCheckButton = nullptr;
lv_obj_t *updateCheckLabel = nullptr;
lv_obj_t *updateDisconnectButton = nullptr;
lv_obj_t *updateDisconnectLabel = nullptr;
lv_obj_t *updateForgetButton = nullptr;
lv_obj_t *wifiForgetConfirm = nullptr;
lv_obj_t *updateInstallButton = nullptr;
lv_obj_t *updateInstallLabel = nullptr;
lv_obj_t *releaseNotesModal = nullptr;
lv_obj_t *releaseNotesLabel = nullptr;
lv_obj_t *advancedSummary = nullptr;
lv_obj_t *advancedDetail = nullptr;
lv_obj_t *advancedRenameButton = nullptr;
lv_obj_t *advancedRemoveButton = nullptr;
lv_obj_t *advancedAddButton = nullptr;
lv_obj_t *advancedList = nullptr;
lv_obj_t *advancedVirtualContent = nullptr;
size_t advancedLogicalSlots[kMaxInfrastructure]{};
size_t advancedLogicalCount = 0;
size_t advancedFirstLogicalIndex = kMaxInfrastructure;
bool advancedVirtualBinding = false;

lv_obj_t *renameModal = nullptr;
lv_obj_t *renameTitle = nullptr;
lv_obj_t *renameHint = nullptr;
lv_obj_t *renameInput = nullptr;
lv_obj_t *renameKeyboard = nullptr;
bool renameUppercase = true;
RenameTarget renameTarget = RenameTarget::Plant;

lv_obj_t *pairModal = nullptr;
lv_obj_t *pairTitle = nullptr;
lv_obj_t *pairInstruction = nullptr;
lv_obj_t *pairStatus = nullptr;
lv_obj_t *pairPrimary = nullptr;
lv_obj_t *pairPrimaryLabel = nullptr;
lv_obj_t *pairSecondary = nullptr;
lv_obj_t *pairSecondaryLabel = nullptr;

bool ieeeEqual(const uint8_t a[8], const uint8_t b[8]) { return memcmp(a, b, 8) == 0; }

bool ieeeZero(const uint8_t ieee[8]) {
  for (size_t i = 0; i < 8; ++i) if (ieee[i]) return false;
  return true;
}

void label(lv_obj_t *obj, const char *text) {
  if (!obj || !text) return;
  const char *current = lv_label_get_text(obj);
  if (current && strcmp(current, text) == 0) return;
  lv_label_set_text(obj, text);
}

void staticRowLabel(lv_obj_t *obj, char *storage, size_t storageSize, const char *text) {
  if (!obj || !storage || storageSize == 0 || !text) return;
  if (strcmp(storage, text) == 0 && lv_label_get_text(obj) == storage) return;
  strncpy(storage, text, storageSize - 1);
  storage[storageSize - 1] = '\0';
  lv_label_set_text_static(obj, storage);
}

#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
void logLvglMemory(const char *reason) {
  lv_mem_monitor_t monitor{};
  lv_mem_monitor(&monitor);
  const size_t used =
      monitor.total_size >= monitor.free_size ? monitor.total_size - monitor.free_size : 0;
  Serial.printf(
      "[lvgl-mem] %s total=%lu free=%lu used=%lu biggest=%lu free_cnt=%lu "
      "used_cnt=%lu max_used=%lu frag=%u%% used_pct=%u%%\n",
      reason ? reason : "unknown",
      static_cast<unsigned long>(monitor.total_size),
      static_cast<unsigned long>(monitor.free_size),
      static_cast<unsigned long>(used),
      static_cast<unsigned long>(monitor.free_biggest_size),
      static_cast<unsigned long>(monitor.free_cnt),
      static_cast<unsigned long>(monitor.used_cnt),
      static_cast<unsigned long>(monitor.max_used),
      static_cast<unsigned>(monitor.frag_pct),
      static_cast<unsigned>(monitor.used_pct));
}
#else
void logLvglMemory(const char *) {}
#endif

#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
void logDisplayRuntimeConfig();

constexpr uint32_t kRuntimeTelemetryIntervalMs = 60U * 1000U;

struct UiRuntimeStats {
  uint32_t refreshCount = 0;
  uint32_t forcedCount = 0;
  uint32_t timedCount = 0;
  uint64_t totalRefreshUs = 0;
  uint32_t maxRefreshUs = 0;
  uint32_t maxLockWaitUs = 0;
};

UiRuntimeStats uiRuntimeStats{};
uint32_t lastRuntimeTelemetryMs = 0;

void logEspMemory(const char *reason) {
  const size_t internalFree =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramFree =
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t psramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf(
      "[runtime-mem] %s internal_free=%lu internal_largest=%lu "
      "psram_free=%lu psram_largest=%lu\n",
      reason ? reason : "unknown",
      static_cast<unsigned long>(internalFree),
      static_cast<unsigned long>(internalLargest),
      static_cast<unsigned long>(psramFree),
      static_cast<unsigned long>(psramLargest));
}

void recordUiRefreshRuntime(uint64_t startedUs, uint32_t lockWaitUs, bool forced,
                            bool timedWork) {
  const uint64_t elapsed64 = esp_timer_get_time() - startedUs;
  const uint32_t elapsedUs = elapsed64 > UINT32_MAX ? UINT32_MAX
                                                   : static_cast<uint32_t>(elapsed64);
  ++uiRuntimeStats.refreshCount;
  if (forced) ++uiRuntimeStats.forcedCount;
  if (timedWork) ++uiRuntimeStats.timedCount;
  uiRuntimeStats.totalRefreshUs += elapsedUs;
  if (elapsedUs > uiRuntimeStats.maxRefreshUs) uiRuntimeStats.maxRefreshUs = elapsedUs;
  if (lockWaitUs > uiRuntimeStats.maxLockWaitUs) uiRuntimeStats.maxLockWaitUs = lockWaitUs;
}

void logUiRuntimeStats() {
  const uint32_t count = uiRuntimeStats.refreshCount;
  const uint32_t avgUs = count
      ? static_cast<uint32_t>(uiRuntimeStats.totalRefreshUs / count)
      : 0;
  Serial.printf(
      "[ui-runtime] refreshes=%lu forced=%lu timed=%lu avg_us=%lu max_us=%lu "
      "max_lock_wait_us=%lu\n",
      static_cast<unsigned long>(count),
      static_cast<unsigned long>(uiRuntimeStats.forcedCount),
      static_cast<unsigned long>(uiRuntimeStats.timedCount),
      static_cast<unsigned long>(avgUs),
      static_cast<unsigned long>(uiRuntimeStats.maxRefreshUs),
      static_cast<unsigned long>(uiRuntimeStats.maxLockWaitUs));
}

void serviceRuntimeTelemetry() {
  const uint32_t now = millis();
  if (lastRuntimeTelemetryMs == 0) {
    lastRuntimeTelemetryMs = now;
    return;
  }
  if (now - lastRuntimeTelemetryMs < kRuntimeTelemetryIntervalMs) return;
  lastRuntimeTelemetryMs = now;

  logEspMemory("runtime-60s");
  logUiRuntimeStats();

  const uint64_t lockStartedUs = esp_timer_get_time();
  if (lvgl_port_lock(25)) {
    const uint64_t lockWait64 = esp_timer_get_time() - lockStartedUs;
    const uint32_t lockWaitUs = lockWait64 > UINT32_MAX ? UINT32_MAX
                                                        : static_cast<uint32_t>(lockWait64);
    if (lockWaitUs > uiRuntimeStats.maxLockWaitUs) uiRuntimeStats.maxLockWaitUs = lockWaitUs;
    logDisplayRuntimeConfig();
    logLvglMemory("runtime-60s");
    lvgl_port_unlock();
  } else {
    Serial.println("[ui-runtime] telemetry LVGL lock timeout after 25ms");
  }
}
#else
void logEspMemory(const char *) {}
void recordUiRefreshRuntime(uint64_t, uint32_t, bool, bool) {}
void logUiRuntimeStats() {}
void serviceRuntimeTelemetry() {}
#endif

#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
void logDisplayRuntimeConfig() {
  lv_disp_t *disp = lv_disp_get_default();
  lv_disp_drv_t *drv = disp ? disp->driver : nullptr;
  lv_disp_draw_buf_t *drawBuf = drv ? drv->draw_buf : nullptr;
  Serial.printf(
      "[display-runtime] tearing_mode=%d configured_rgb_buffers=%d full_refresh=%d "
      "direct_mode=%d resolution=%dx%d draw_buf_px=%lu buf1=%p buf2=%p\n",
      static_cast<int>(LVGL_PORT_AVOID_TEARING_MODE),
      static_cast<int>(LVGL_PORT_DISP_BUFFER_NUM),
      drv ? static_cast<int>(drv->full_refresh) : -1,
      drv ? static_cast<int>(drv->direct_mode) : -1,
      drv ? static_cast<int>(drv->hor_res) : -1,
      drv ? static_cast<int>(drv->ver_res) : -1,
      drawBuf ? static_cast<unsigned long>(drawBuf->size) : 0UL,
      drawBuf ? static_cast<void *>(drawBuf->buf1) : nullptr,
      drawBuf ? static_cast<void *>(drawBuf->buf2) : nullptr);
#if defined(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE)
  Serial.printf("[display-runtime] rgb_bounce_buffer_pixels=%lu\n",
                static_cast<unsigned long>(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE));
#endif
}
#else
void logDisplayRuntimeConfig() {}
#endif

void clearSetupQrCanvas() {
  memset(updateQrCanvasBuffer + kSetupQrPaletteBytes, 0xFF,
         kSetupQrRowBytes * static_cast<size_t>(kSetupQrSize));
}

bool renderSetupQr(const char *payload) {
  if (!updateQrCode || !payload || !payload[0]) return false;

  memset(updateQrTempBuffer, 0, sizeof(updateQrTempBuffer));
  memset(updateQrEncodedBuffer, 0, sizeof(updateQrEncodedBuffer));
  if (!qrcodegen_encodeText(
          payload, updateQrTempBuffer, updateQrEncodedBuffer,
          qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
          kSetupQrMaxVersion, qrcodegen_Mask_AUTO, true)) {
    return false;
  }

  const int moduleCount = qrcodegen_getSize(updateQrEncodedBuffer);
  if (moduleCount <= 0) return false;
  const int totalModules =
      moduleCount + static_cast<int>(kSetupQrQuietModules) * 2;
  const int scale = static_cast<int>(kSetupQrSize) / totalModules;
  if (scale <= 0) return false;

  clearSetupQrCanvas();
  const int renderedSize = totalModules * scale;
  const int quietPixels = static_cast<int>(kSetupQrQuietModules) * scale;
  const int origin =
      (static_cast<int>(kSetupQrSize) - renderedSize) / 2 + quietPixels;

  for (int moduleY = 0; moduleY < moduleCount; ++moduleY) {
    for (int moduleX = 0; moduleX < moduleCount; ++moduleX) {
      if (!qrcodegen_getModule(updateQrEncodedBuffer, moduleX, moduleY)) continue;
      const int startX = origin + moduleX * scale;
      const int startY = origin + moduleY * scale;
      for (int pixelY = 0; pixelY < scale; ++pixelY) {
        const size_t row =
            static_cast<size_t>(startY + pixelY) * kSetupQrRowBytes;
        for (int pixelX = 0; pixelX < scale; ++pixelX) {
          const int x = startX + pixelX;
          uint8_t &byte = updateQrCanvasBuffer[
              kSetupQrPaletteBytes + row + static_cast<size_t>(x >> 3)];
          byte = static_cast<uint8_t>(byte & ~(1U << (7 - (x & 0x7))));
        }
      }
    }
  }

  lv_img_cache_invalidate_src(lv_canvas_get_img(updateQrCode));
  lv_obj_invalidate(updateQrCode);
  return true;
}

void slotKey(size_t slot, char out[12]) {
  snprintf(out, 12, "plant%02u", static_cast<unsigned>(slot));
}

void defaultName(size_t slot, char *out, size_t size) {
  snprintf(out, size, "PLANT %u", static_cast<unsigned>(slot + 1));
}

void infrastructureKey(size_t slot, char out[12]) {
  snprintf(out, 12, "rptr%02u", static_cast<unsigned>(slot));
}

void defaultInfrastructureName(size_t slot, char *out, size_t size) {
  snprintf(out, size, "REPEATER %u", static_cast<unsigned>(slot + 1));
}

void copyLegacyPreference(Preferences &legacy, Preferences &target, const char *key) {
  if (!key || target.isKey(key) || !legacy.isKey(key)) return;

  const size_t bytes = legacy.getBytesLength(key);
  if (bytes) {
    uint8_t *buffer = static_cast<uint8_t *>(malloc(bytes));
    if (!buffer) {
      Serial.printf("[storage] could not allocate %u bytes to migrate %s\n",
                    static_cast<unsigned>(bytes), key);
      return;
    }
    if (legacy.getBytes(key, buffer, bytes) == bytes) {
      target.putBytes(key, buffer, bytes);
      Serial.printf("[storage] migrated blob %s (%u bytes)\n", key,
                    static_cast<unsigned>(bytes));
    }
    free(buffer);
    return;
  }

  // Scalar/string keys are copied explicitly by migrateUserPreferences().
}

void migrateUserPreferences() {
  Preferences target;
  if (!target.begin("espplants", false, "plantdata")) {
    Serial.println("[storage] ERROR: could not open dedicated plantdata partition");
    return;
  }

  const uint32_t schema = target.getUInt("_schema", 0);
  if (schema >= 1) {
    target.end();
    return;
  }

  // Phase 1 moves user-owned settings out of the generic NVS partition and
  // into the partition that was reserved for them from the start. The legacy
  // copy is deliberately left untouched as a recovery fallback.
  Preferences legacy;
  if (legacy.begin("espplants", true, "nvs")) {
    if (!target.isKey("fahrenheit") && legacy.isKey("fahrenheit")) {
      target.putBool("fahrenheit", legacy.getBool("fahrenheit", true));
      Serial.println("[storage] migrated fahrenheit preference");
    }
    if (!target.isKey("device_name") && legacy.isKey("device_name")) {
      target.putString("device_name", legacy.getString("device_name", "ESP PLANTS"));
      Serial.println("[storage] migrated device name");
    }

    char key[12]{};
    for (size_t slot = 0; slot < kMaxSensors; ++slot) {
      slotKey(slot, key);
      copyLegacyPreference(legacy, target, key);
    }
    for (size_t slot = 0; slot < kMaxInfrastructure; ++slot) {
      infrastructureKey(slot, key);
      copyLegacyPreference(legacy, target, key);
    }
    legacy.end();
  }

  target.putUInt("_schema", 1);
  target.putBool("_nvs_mig1", true);
  target.end();
  Serial.println("[storage] plantdata schema=1 ready; legacy NVS retained");
}

size_t infrastructureCount() {
  size_t count = 0;
  for (const auto &node : infrastructure) if (node.used) ++count;
  return count;
}

size_t buildInfrastructureSlots(size_t out[kMaxInfrastructure]) {
  size_t count = 0;
  for (size_t slot = 0; slot < kMaxInfrastructure; ++slot) {
    if (!infrastructure[slot].used) continue;
    if (out) out[count] = slot;
    ++count;
  }
  return count;
}

size_t onlineInfrastructureCount() {
  size_t count = 0;
  for (const auto &node : infrastructure) if (node.used && node.online) ++count;
  return count;
}

void saveInfrastructureSlot(size_t slot) {
  if (slot >= kMaxInfrastructure || !infrastructure[slot].used) return;
  PersistedInfrastructure p{};
  memcpy(p.ieee, infrastructure[slot].ieee, sizeof(p.ieee));
  strncpy(p.name, infrastructure[slot].name, sizeof(p.name) - 1);
  char key[12]{};
  infrastructureKey(slot, key);
  preferences.putBytes(key, &p, sizeof(p));
}

void loadInfrastructureRegistry() {
  for (size_t slot = 0; slot < kMaxInfrastructure; ++slot) {
    char key[12]{};
    infrastructureKey(slot, key);
    if (!preferences.isKey(key)) continue;
    if (preferences.getBytesLength(key) != sizeof(PersistedInfrastructure)) continue;

    PersistedInfrastructure p{};
    if (preferences.getBytes(key, &p, sizeof(p)) != sizeof(p)) continue;
    if (p.magic != kInfrastructureRecordMagic ||
        p.version != kInfrastructureRecordVersion ||
        ieeeZero(p.ieee)) continue;

    InfrastructureNode &node = infrastructure[slot];
    node.used = true;
    memcpy(node.ieee, p.ieee, sizeof(node.ieee));
    p.name[sizeof(p.name) - 1] = '\0';
    if (p.name[0]) strncpy(node.name, p.name, sizeof(node.name) - 1);
    else defaultInfrastructureName(slot, node.name, sizeof(node.name));

    if (selectedInfrastructure < 0) selectedInfrastructure = static_cast<int>(slot);

    char ieee[24]{};
    plantlink::formatIeee(node.ieee, ieee, sizeof(ieee));
    Serial.printf("[registry] repeater slot=%u name=\"%s\" ieee=%s\n",
                  static_cast<unsigned>(slot + 1), node.name, ieee);
  }
  Serial.printf("[registry] loaded %u repeater(s)\n",
                static_cast<unsigned>(infrastructureCount()));
}

InfrastructureNode *findInfrastructure(const uint8_t ieee[8], size_t *slotOut = nullptr) {
  if (!ieee || ieeeZero(ieee)) return nullptr;
  for (size_t slot = 0; slot < kMaxInfrastructure; ++slot) {
    if (infrastructure[slot].used && ieeeEqual(infrastructure[slot].ieee, ieee)) {
      if (slotOut) *slotOut = slot;
      return &infrastructure[slot];
    }
  }
  return nullptr;
}

InfrastructureNode *findOrCreateInfrastructure(const plantlink::InfrastructureReportData &report,
                                               size_t *slotOut = nullptr,
                                               bool *createdOut = nullptr) {
  size_t slot = 0;
  InfrastructureNode *node = findInfrastructure(report.ieee, &slot);
  bool created = false;

  if (!node) {
    for (slot = 0; slot < kMaxInfrastructure; ++slot) {
      if (infrastructure[slot].used) continue;
      node = &infrastructure[slot];
      *node = InfrastructureNode{};
      node->used = true;
      memcpy(node->ieee, report.ieee, sizeof(node->ieee));
      defaultInfrastructureName(slot, node->name, sizeof(node->name));
      saveInfrastructureSlot(slot);
      created = true;
      if (selectedInfrastructure < 0) selectedInfrastructure = static_cast<int>(slot);
      break;
    }
  }

  if (!node) return nullptr;
  node->shortAddress = report.shortAddress;
  node->flags = report.flags;
  node->deviceType = report.deviceType;
  node->online = (report.flags & plantlink::InfrastructureOnline) != 0;
  node->lqi = report.lqi;
  node->rssi = report.rssiDbm;
  if (node->online) node->lastSeenMs = millis();

  if (slotOut) *slotOut = slot;
  if (createdOut) *createdOut = created;
  return node;
}

void clearInfrastructureSlot(size_t slot) {
  if (slot >= kMaxInfrastructure || !infrastructure[slot].used) return;
  char key[12]{};
  infrastructureKey(slot, key);
  preferences.remove(key);
  infrastructure[slot] = InfrastructureNode{};

  if (selectedInfrastructure == static_cast<int>(slot)) {
    selectedInfrastructure = -1;
    for (size_t i = 0; i < kMaxInfrastructure; ++i) {
      if (infrastructure[i].used) {
        selectedInfrastructure = static_cast<int>(i);
        break;
      }
    }
  }
  markInfrastructureDirty();
}

size_t registeredCount() {
  size_t count = 0;
  for (const auto &sensor : sensors) if (sensor.used) ++count;
  return count;
}

size_t reportedCount() {
  size_t count = 0;
  for (const auto &sensor : sensors) if (sensor.used && sensor.seenThisBoot) ++count;
  return count;
}

bool hasFreshMoisture(const PlantSensor &sensor) {
  return sensor.used && sensor.seenThisBoot &&
         (sensor.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture);
}

int sensorSortRank(const PlantSensor &sensor) {
  if (hasFreshMoisture(sensor)) return 0;
  if (sensor.used && sensor.seenThisBoot) return 1;
  return 2;
}

size_t buildSortedSlots(size_t out[kMaxSensors]) {
  size_t count = 0;
  for (size_t slot = 0; slot < kMaxSensors; ++slot) {
    if (sensors[slot].used) out[count++] = slot;
  }

  for (size_t i = 1; i < count; ++i) {
    const size_t value = out[i];
    size_t j = i;
    while (j > 0) {
      const size_t previous = out[j - 1];
      const int valueRank = sensorSortRank(sensors[value]);
      const int previousRank = sensorSortRank(sensors[previous]);
      bool before = valueRank < previousRank;

      if (valueRank == previousRank && valueRank == 0) {
        if (sensors[value].soilMoisturePct != sensors[previous].soilMoisturePct) {
          before = sensors[value].soilMoisturePct < sensors[previous].soilMoisturePct;
        } else {
          before = value < previous;
        }
      } else if (valueRank == previousRank) {
        before = value < previous;
      }

      if (!before) break;
      out[j] = previous;
      --j;
    }
    out[j] = value;
  }
  return count;
}

void refreshSortedSensorSlots() {
  if (!dirty.sensorOrder) return;
  sortedSensorCount = buildSortedSlots(sortedSensorSlots);
  dirty.sensorOrder = false;
}

int driestReportedSensor() {
  refreshSortedSensorSlots();
  if (!sortedSensorCount || !hasFreshMoisture(sensors[sortedSensorSlots[0]])) return -1;
  return static_cast<int>(sortedSensorSlots[0]);
}

int featuredHomeSensor() {
  if (manualHomeSensor >= 0 &&
      manualHomeSensor < static_cast<int>(kMaxSensors) &&
      sensors[manualHomeSensor].used &&
      static_cast<int32_t>(manualHomeUntilMs - millis()) > 0) {
    return manualHomeSensor;
  }
  return driestReportedSensor();
}

void formatLastReport(const PlantSensor &sensor, char *out, size_t size) {
  if (!sensor.seenThisBoot || !sensor.lastSeenMs) {
    snprintf(out, size, "WAITING");
    return;
  }
  const uint32_t age = (millis() - sensor.lastSeenMs) / 1000u;
  if (age < 2) snprintf(out, size, "NOW");
  else if (age < 60) snprintf(out, size, "%lus", static_cast<unsigned long>(age));
  else snprintf(out, size, "%lum", static_cast<unsigned long>(age / 60u));
}

void saveSlot(size_t slot) {
  if (slot >= kMaxSensors || !sensors[slot].used) return;
  PersistedPlant p{};
  memcpy(p.ieee, sensors[slot].ieee, sizeof(p.ieee));
  strncpy(p.name, sensors[slot].name, sizeof(p.name) - 1);
  char key[12]{};
  slotKey(slot, key);
  preferences.putBytes(key, &p, sizeof(p));
}

void loadRegistry() {
  for (size_t slot = 0; slot < kMaxSensors; ++slot) {
    char key[12]{};
    slotKey(slot, key);
    if (!preferences.isKey(key)) continue;
    if (preferences.getBytesLength(key) != sizeof(PersistedPlant)) continue;

    PersistedPlant p{};
    if (preferences.getBytes(key, &p, sizeof(p)) != sizeof(p)) continue;
    if (p.magic != kPlantRecordMagic || p.version != kPlantRecordVersion || ieeeZero(p.ieee)) continue;

    PlantSensor &s = sensors[slot];
    s.used = true;
    memcpy(s.ieee, p.ieee, sizeof(s.ieee));
    p.name[sizeof(p.name) - 1] = '\0';
    if (p.name[0]) strncpy(s.name, p.name, sizeof(s.name) - 1);
    else defaultName(slot, s.name, sizeof(s.name));

    char ieee[24]{};
    plantlink::formatIeee(s.ieee, ieee, sizeof(ieee));
    Serial.printf("[registry] slot=%u name=\"%s\" ieee=%s\n",
                  static_cast<unsigned>(slot + 1), s.name, ieee);
    if (selectedSensor < 0) selectedSensor = static_cast<int>(slot);
  }
  Serial.printf("[registry] loaded %u plant(s)\n", static_cast<unsigned>(registeredCount()));
}

PlantSensor *findSensor(const uint8_t ieee[8], size_t *slotOut = nullptr) {
  if (ieeeZero(ieee)) return nullptr;
  for (size_t slot = 0; slot < kMaxSensors; ++slot) {
    if (sensors[slot].used && ieeeEqual(sensors[slot].ieee, ieee)) {
      if (slotOut) *slotOut = slot;
      return &sensors[slot];
    }
  }
  return nullptr;
}

PlantSensor *findOrCreateSensor(const uint8_t ieee[8], uint16_t shortAddress,
                                size_t *slotOut = nullptr) {
  size_t slot = 0;
  PlantSensor *sensor = findSensor(ieee, &slot);
  if (sensor) {
    sensor->shortAddress = shortAddress;
    if (slotOut) *slotOut = slot;
    return sensor;
  }
  if (ieeeZero(ieee)) return nullptr;

  for (slot = 0; slot < kMaxSensors; ++slot) {
    if (sensors[slot].used) continue;
    PlantSensor &s = sensors[slot];
    s = PlantSensor{};
    s.used = true;
    memcpy(s.ieee, ieee, sizeof(s.ieee));
    s.shortAddress = shortAddress;
    defaultName(slot, s.name, sizeof(s.name));
    saveSlot(slot);
    if (selectedSensor < 0) selectedSensor = static_cast<int>(slot);
    if (slotOut) *slotOut = slot;

    char formatted[24]{};
    plantlink::formatIeee(ieee, formatted, sizeof(formatted));
    Serial.printf("[registry] assigned ieee=%s -> slot=%u name=\"%s\"\n",
                  formatted, static_cast<unsigned>(slot + 1), s.name);
    markSensorRegistryDirty();
    return &s;
  }
  Serial.println("[registry] no free plant slots");
  return nullptr;
}

const char *mood(PlantSensor &s) {
  if (!s.seenThisBoot) return "Waiting for this plant to check in";
  if (!(s.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture))
    return "Waiting for a moisture reading";
  const bool warning=(s.reportedFieldFlagsThisBoot & plantlink::SensorHasWaterWarning) && s.waterWarning;
  const auto state=espplants_phrases::stateFor(s.soilMoisturePct,warning);
  const size_t slot=static_cast<size_t>(&s-sensors);
  const uint32_t seed=static_cast<uint32_t>(slot*2654435761u)^static_cast<uint32_t>(s.soilMoisturePct*257u);
  return espplants_phrases::select(s.phraseRotation,phraseTheme,state,seed,false);
}

void sendFrame(plantlink::MessageType type, const uint8_t *payload = nullptr,
               uint16_t payloadLength = 0, uint8_t flags = plantlink::FlagNone) {
  uint8_t encoded[plantlink::kMaxEncodedBytes]{};
  const size_t length = plantlink::encodeFrame(type, flags, nextSequence++, payload,
                                                payloadLength, encoded, sizeof(encoded));
  if (length) Serial0.write(encoded, length);
}

void sendHello() {
  uint8_t payload[8]{};
  plantlink::putU32LE(payload, millis());
  plantlink::putU32LE(payload + 4, 0);
  sendFrame(plantlink::MessageType::Hello, payload, sizeof(payload));
}

void requestJoin(uint8_t seconds) {
  sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);
  permitJoinRemaining = seconds;
  permitJoinGuardUntilMs = seconds ? millis() + 3000u : 0;
  dirty.settings = true;
  dirty.pair = true;
  Serial.printf("[plantlink] permit join requested: %u s\n", seconds);
}

void requestRemoveDevice(const uint8_t ieee[8]) {
  if (!ieee || ieeeZero(ieee)) return;
  sendFrame(plantlink::MessageType::RemoveDevice, ieee, 8);
  char formatted[24]{};
  plantlink::formatIeee(ieee, formatted, sizeof(formatted));
  Serial.printf("[plantlink] remove device requested: %s\n", formatted);
}

void selectFirstRegisteredPlant() {
  selectedSensor = -1;
  for (size_t slot = 0; slot < kMaxSensors; ++slot) {
    if (sensors[slot].used) {
      selectedSensor = static_cast<int>(slot);
      break;
    }
  }
}

void clearPlantSlot(size_t slot) {
  if (slot >= kMaxSensors || !sensors[slot].used) return;

  char key[12]{};
  slotKey(slot, key);
  preferences.remove(key);
  sensors[slot] = PlantSensor{};

  if (manualHomeSensor == static_cast<int>(slot)) {
    manualHomeSensor = -1;
    manualHomeUntilMs = 0;
  }
  if (selectedSensor == static_cast<int>(slot)) selectFirstRegisteredPlant();

  Serial.printf("[registry] cleared slot=%u\n", static_cast<unsigned>(slot + 1));
  markSensorRegistryDirty();
}

PlantSensor *replacePlantIdentity(size_t slot, const uint8_t ieee[8], uint16_t shortAddress) {
  if (slot >= kMaxSensors || !sensors[slot].used || !ieee || ieeeZero(ieee)) return nullptr;

  char preservedName[kPlantNameBytes]{};
  strncpy(preservedName, sensors[slot].name, sizeof(preservedName) - 1);

  PlantSensor replacement{};
  replacement.used = true;
  memcpy(replacement.ieee, ieee, sizeof(replacement.ieee));
  replacement.shortAddress = shortAddress;
  strncpy(replacement.name, preservedName, sizeof(replacement.name) - 1);
  replacement.name[sizeof(replacement.name) - 1] = '\0';
  sensors[slot] = replacement;
  saveSlot(slot);

  selectedSensor = static_cast<int>(slot);
  manualHomeSensor = -1;
  manualHomeUntilMs = 0;
  markSensorRegistryDirty();

  char formatted[24]{};
  plantlink::formatIeee(ieee, formatted, sizeof(formatted));
  Serial.printf("[registry] replaced slot=%u name=\"%s\" new_ieee=%s\n",
                static_cast<unsigned>(slot + 1), sensors[slot].name, formatted);
  return &sensors[slot];
}

lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, 20, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x18231D), 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(0xE5ECE7), 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

void refreshUi(bool force = false, bool alreadyInLvglContext = false);

void metric(lv_obj_t *parent, const char *caption, int x, int y, lv_obj_t **value,
            const lv_font_t *font = &lv_font_montserrat_28) {
  lv_obj_t *c = lv_label_create(parent);
  lv_label_set_text(c, caption);
  lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(c, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(c, x, y);
  *value = lv_label_create(parent);
  lv_label_set_text(*value, "--");
  lv_obj_set_style_text_font(*value, font, 0);
  lv_obj_set_style_text_color(*value, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(*value, x, y + 19);
}

void showPage(Page page) {
  currentPage = page;
  if (homePage) (page == Page::Home) ? lv_obj_clear_flag(homePage, LV_OBJ_FLAG_HIDDEN)
                                     : lv_obj_add_flag(homePage, LV_OBJ_FLAG_HIDDEN);
  if (allPage) (page == Page::All) ? lv_obj_clear_flag(allPage, LV_OBJ_FLAG_HIDDEN)
                                   : lv_obj_add_flag(allPage, LV_OBJ_FLAG_HIDDEN);
  if (plantPage) (page == Page::Plant) ? lv_obj_clear_flag(plantPage, LV_OBJ_FLAG_HIDDEN)
                                       : lv_obj_add_flag(plantPage, LV_OBJ_FLAG_HIDDEN);
  if (settingsPage) (page == Page::Settings) ? lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_HIDDEN)
                                             : lv_obj_add_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);
  if (advancedPage) (page == Page::Advanced) ? lv_obj_clear_flag(advancedPage, LV_OBJ_FLAG_HIDDEN)
                                             : lv_obj_add_flag(advancedPage, LV_OBJ_FLAG_HIDDEN);

  const lv_color_t active = lv_color_hex(0x1E3529);
  const lv_color_t idle = lv_color_hex(0x151F1A);
  if (navHome) lv_obj_set_style_bg_color(navHome, page == Page::Home ? active : idle, 0);
  if (navAll) lv_obj_set_style_bg_color(navAll, page == Page::All ? active : idle, 0);
  if (navPlant) lv_obj_set_style_bg_color(navPlant, page == Page::Plant ? active : idle, 0);
  if (navSettings) lv_obj_set_style_bg_color(navSettings, page == Page::Settings ? active : idle, 0);
  refreshUi(true, true);

  switch (page) {
    case Page::Home: logLvglMemory("page-home"); break;
    case Page::All: logLvglMemory("page-all-sensors"); break;
    case Page::Plant: logLvglMemory("page-plant-detail"); break;
    case Page::Settings: logLvglMemory("page-settings"); break;
    case Page::Advanced: logLvglMemory("page-advanced-zigbee"); break;
  }
}

void navEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  showPage(static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

void refreshHomeVirtualList(bool forceValues = false) {
  if (!homeList || !homeVirtualContent || homeVirtualBinding) return;
  homeVirtualBinding = true;

  refreshSortedSensorSlots();
  size_t logicalSlots[kMaxSensors]{};
  const size_t logicalCount = sortedSensorCount;
  if (logicalCount > 0) {
    memcpy(logicalSlots, sortedSensorSlots, logicalCount * sizeof(size_t));
  }
  const bool logicalChanged =
      logicalCount != homeLogicalCount ||
      (logicalCount > 0 &&
       memcmp(homeLogicalSlots, logicalSlots, logicalCount * sizeof(size_t)) != 0);

  if (logicalChanged) {
    if (logicalCount > 0) {
      memcpy(homeLogicalSlots, logicalSlots, logicalCount * sizeof(size_t));
    }
    homeLogicalCount = logicalCount;
    lv_obj_set_height(homeVirtualContent,
                      espplants_home_virtual_list::contentHeight(logicalCount));
    lv_obj_update_layout(homeList);

    const int32_t scrollY = lv_obj_get_scroll_y(homeList);
    const int32_t clamped =
        espplants_home_virtual_list::clampScrollY(logicalCount, scrollY);
    if (scrollY != clamped) lv_obj_scroll_to_y(homeList, clamped, LV_ANIM_OFF);
    homeFirstLogicalIndex = kMaxSensors;
  }

  const int32_t scrollY = espplants_home_virtual_list::clampScrollY(
      homeLogicalCount, lv_obj_get_scroll_y(homeList));
  const size_t firstLogical = espplants_home_virtual_list::firstPoolLogicalIndex(
      homeLogicalCount, scrollY);
  const bool windowChanged = firstLogical != homeFirstLogicalIndex;
  const int homeSensor = featuredHomeSensor();

  for (size_t poolIndex = 0;
       poolIndex < espplants_home_virtual_list::kPoolSize; ++poolIndex) {
    PlantListRow &row = rows[poolIndex];
    const size_t logicalIndex = firstLogical + poolIndex;
    if (logicalIndex >= homeLogicalCount) {
      row.boundSlot = -1;
      row.boundLogicalIndex = kMaxSensors;
      lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const size_t slot = homeLogicalSlots[logicalIndex];
    const bool rebound = row.boundSlot != static_cast<int>(slot) ||
                         row.boundLogicalIndex != logicalIndex;
    if (rebound || windowChanged) {
      row.boundSlot = static_cast<int>(slot);
      row.boundLogicalIndex = logicalIndex;
      lv_obj_set_pos(row.box, 0, static_cast<lv_coord_t>(logicalIndex *
          static_cast<size_t>(espplants_home_virtual_list::kStride)));
    }

    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    if (rebound || windowChanged || forceValues) {
      staticRowLabel(row.name, row.nameText, sizeof(row.nameText), sensors[slot].name);
      char moisture[8]{};
      if (hasFreshMoisture(sensors[slot])) {
        snprintf(moisture, sizeof(moisture), "%u%%", sensors[slot].soilMoisturePct);
        lv_bar_set_value(row.bar, sensors[slot].soilMoisturePct, LV_ANIM_OFF);
      } else {
        snprintf(moisture, sizeof(moisture), "--%%");
        lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
      }
      staticRowLabel(row.moisture, row.moistureText, sizeof(row.moistureText), moisture);
      lv_obj_set_style_bg_color(
          row.box,
          lv_color_hex(homeSensor == static_cast<int>(slot) ? 0x1E3529 : 0x1D2922), 0);
    }
  }

  homeFirstLogicalIndex = firstLogical;
  homeVirtualBinding = false;
}

void homeListScrollEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_SCROLL) return;
  refreshHomeVirtualList(false);
}

void rowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *row = static_cast<PlantListRow *>(lv_event_get_user_data(event));
  if (!row || row->boundSlot < 0 || row->boundSlot >= static_cast<int>(kMaxSensors) ||
      !sensors[row->boundSlot].used) return;
  manualHomeSensor = row->boundSlot;
  manualHomeUntilMs = millis() + kHomeManualSelectionMs;
  selectedSensor = row->boundSlot;
  dirty.home = true;
  dirty.plant = true;
}

void refreshAllVirtualList(bool forceValues = false) {
  if (!allList || !allVirtualContent || allVirtualBinding) return;
  allVirtualBinding = true;

  refreshSortedSensorSlots();
  size_t logicalSlots[kMaxSensors]{};
  const size_t logicalCount = sortedSensorCount;
  if (logicalCount > 0) {
    memcpy(logicalSlots, sortedSensorSlots, logicalCount * sizeof(size_t));
  }
  const bool logicalChanged =
      logicalCount != allLogicalCount ||
      (logicalCount > 0 &&
       memcmp(allLogicalSlots, logicalSlots, logicalCount * sizeof(size_t)) != 0);

  if (logicalChanged) {
    if (logicalCount > 0) {
      memcpy(allLogicalSlots, logicalSlots, logicalCount * sizeof(size_t));
    }
    allLogicalCount = logicalCount;
    lv_obj_set_height(allVirtualContent,
                      espplants_all_virtual_list::contentHeight(logicalCount));
    lv_obj_update_layout(allList);

    const int32_t scrollY = lv_obj_get_scroll_y(allList);
    const int32_t clamped =
        espplants_all_virtual_list::clampScrollY(logicalCount, scrollY);
    if (scrollY != clamped) lv_obj_scroll_to_y(allList, clamped, LV_ANIM_OFF);
    allFirstLogicalIndex = kMaxSensors;
  }

  const int32_t scrollY = espplants_all_virtual_list::clampScrollY(
      allLogicalCount, lv_obj_get_scroll_y(allList));
  const size_t firstLogical = espplants_all_virtual_list::firstPoolLogicalIndex(
      allLogicalCount, scrollY);
  const bool windowChanged = firstLogical != allFirstLogicalIndex;

  char text[64]{};
  for (size_t poolIndex = 0;
       poolIndex < espplants_all_virtual_list::kPoolSize; ++poolIndex) {
    AllSensorRow &row = allRows[poolIndex];
    const size_t logicalIndex = firstLogical + poolIndex;
    if (logicalIndex >= allLogicalCount) {
      row.boundSlot = -1;
      row.boundLogicalIndex = kMaxSensors;
      lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const size_t slot = allLogicalSlots[logicalIndex];
    const bool rebound = row.boundSlot != static_cast<int>(slot) ||
                         row.boundLogicalIndex != logicalIndex;
    if (rebound || windowChanged) {
      row.boundSlot = static_cast<int>(slot);
      row.boundLogicalIndex = logicalIndex;
      lv_obj_set_pos(row.box, 0, static_cast<lv_coord_t>(logicalIndex *
          static_cast<size_t>(espplants_all_virtual_list::kStride)));
    }

    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    if (rebound || windowChanged || forceValues) {
      label(row.name, sensors[slot].name);

      if (hasFreshMoisture(sensors[slot]))
        snprintf(text, sizeof(text), "%u%%", sensors[slot].soilMoisturePct);
      else
        snprintf(text, sizeof(text), "--%%");
      staticRowLabel(row.moisture, row.moistureText, sizeof(row.moistureText), text);

      if (sensors[slot].seenThisBoot &&
          (sensors[slot].fieldFlags & plantlink::SensorHasBattery))
        snprintf(text, sizeof(text), "%u%%", sensors[slot].batteryPct);
      else
        snprintf(text, sizeof(text), "--%%");
      staticRowLabel(row.battery, row.batteryText, sizeof(row.batteryText), text);

      formatLastReport(sensors[slot], text, sizeof(text));
      label(row.updated, text);

      const bool thirsty =
          sensors[slot].seenThisBoot &&
          ((sensors[slot].fieldFlags & plantlink::SensorHasWaterWarning) &&
           sensors[slot].waterWarning);
      lv_obj_set_style_bg_color(row.box,
                                lv_color_hex(thirsty ? 0x3A2723 : 0x1D2922), 0);
    }
  }

  allFirstLogicalIndex = firstLogical;
  allVirtualBinding = false;
}

void allListScrollEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_SCROLL) return;
  refreshAllVirtualList(false);
}

void allRowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *row = static_cast<AllSensorRow *>(lv_event_get_user_data(event));
  if (!row || row->boundSlot < 0 || row->boundSlot >= static_cast<int>(kMaxSensors) ||
      !sensors[row->boundSlot].used) return;
  selectedSensor = row->boundSlot;
  showPage(Page::Plant);
}

void featuredEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const int slot = featuredHomeSensor();
  if (slot >= 0 && slot < static_cast<int>(kMaxSensors) && sensors[slot].used) {
    selectedSensor = slot;
    showPage(Page::Plant);
  }
}

void unitEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  useFahrenheit = !useFahrenheit;
  preferences.putBool("fahrenheit", useFahrenheit);
  Serial.printf("[settings] temperature units=%s\n", useFahrenheit ? "F" : "C");
  dirty.home = true;
  dirty.plant = true;
  dirty.settings = true;
}

static const char *kPersonalityMap[] = {
    "CLASSIC", "\n",
    "FUNNY", "\n",
    "SARCASTIC", "\n",
    "DRAMATIC", "\n",
    "RUDE", "\n",
    "MIXED - ALL", "\n",
    "CANCEL", "SAVE", ""
};

bool personalityDialogActive = false;

void refreshPersonalitySelection() {
  if (!renameKeyboard) return;
  lv_btnmatrix_set_one_checked(renameKeyboard,true);
  for (uint16_t i=0;i<6;++i) {
    lv_btnmatrix_set_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKABLE);
    if (i==static_cast<uint16_t>(pendingTheme))
      lv_btnmatrix_set_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKED);
    else
      lv_btnmatrix_clear_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKED);
  }
}

void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!renameModal || !renameKeyboard || !renameInput) return;

  pendingTheme=phraseTheme;
  personalityDialogActive=true;
  label(renameTitle,"PLANT PERSONALITY");
  lv_obj_set_pos(renameTitle,32,8);
  lv_obj_set_pos(renameHint,32,39);
  lv_obj_set_width(renameHint,700);

  char hint[96]{};
  snprintf(hint,sizeof(hint),"Selected: %s   SAVE to apply",
           espplants_phrases::themeName(pendingTheme));
  label(renameHint,hint);

  // Reuse the existing rename btnmatrix. No new LVGL objects are allocated.
  // The selector starts 36 px below the 68 px header.
  lv_obj_add_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(renameKeyboard,50,104);
  lv_obj_set_size(renameKeyboard,700,320);
  lv_btnmatrix_set_map(renameKeyboard,kPersonalityMap);
  lv_obj_set_style_bg_color(renameKeyboard,lv_color_hex(0x3F7A4E),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(renameKeyboard,2,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(renameKeyboard,lv_color_hex(0x8DD39C),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  refreshPersonalitySelection();
  lv_obj_clear_flag(renameModal,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
  logLvglMemory("personality-open");
}

void startPairing(bool replacing, int targetSlot);
void startInfrastructurePairing();
void showRemoveConfirm(int targetSlot);
void showInfrastructureRemoveConfirm(int targetSlot);
void openInfrastructureRename(int slot);

void pairEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  startPairing(false, -1);
}

void advancedEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  showPage(Page::Advanced);
}

void advancedBackEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  showPage(Page::Settings);
}

void closeReleaseNotes();

void openUpdateEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !updateModal) return;
  lv_obj_clear_flag(updateModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(updateModal);
  updateModalOpen = true;
  dirty.update = true;
  refreshUi(true, true);
  logLvglMemory("network-updates-open");
}

void closeUpdateEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !updateModal) return;
  if (wifiForgetConfirm) lv_obj_add_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
  closeReleaseNotes();
  lv_obj_add_flag(updateModal, LV_OBJ_FLAG_HIDDEN);
  updateModalOpen = false;
}

void wifiSetupEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (espplants_update::setupPortalActive())
    espplants_update::cancelWifiSetup();
  else
    espplants_update::startWifiSetup();
  dirty.update = true;
}

void wifiDisconnectEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (espplants_update::wifiReconnectSuppressed())
    espplants_update::reconnectWifi();
  else
    espplants_update::disconnectWifi();
  dirty.update = true;
}

void wifiForgetAskEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !wifiForgetConfirm) return;
  lv_obj_clear_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(wifiForgetConfirm);
}

void wifiForgetCancelEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !wifiForgetConfirm) return;
  lv_obj_add_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
}

void wifiForgetConfirmEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  espplants_update::forgetWifi();
  if (wifiForgetConfirm) lv_obj_add_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
  dirty.update = true;
}

void closeReleaseNotes() {
  if (releaseNotesModal) lv_obj_add_flag(releaseNotesModal, LV_OBJ_FLAG_HIDDEN);
}

void releaseNotesBackEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  closeReleaseNotes();
  dirty.update = true;
}

void releaseNotesCheckEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  closeReleaseNotes();
  espplants_update::requestCheck();
  dirty.update = true;
}

void updateCheckEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (espplants_update::updateAvailable() &&
      espplants_update::releaseNotesAvailable() && releaseNotesModal &&
      !espplants_update::checking()) {
    label(releaseNotesLabel, espplants_update::releaseNotes());
    lv_obj_clear_flag(releaseNotesModal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(releaseNotesModal);
    return;
  }
  espplants_update::requestCheck();
  dirty.update = true;
}

void updateInstallEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  espplants_update::requestInstall();
  dirty.update = true;
}

void infrastructureAddEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  startInfrastructurePairing();
}

void refreshAdvancedVirtualList(bool forceValues = false) {
  if (!advancedList || !advancedVirtualContent || advancedVirtualBinding) return;
  advancedVirtualBinding = true;

  size_t logicalSlots[kMaxInfrastructure]{};
  const size_t logicalCount = buildInfrastructureSlots(logicalSlots);
  const bool logicalChanged =
      logicalCount != advancedLogicalCount ||
      (logicalCount > 0 &&
       memcmp(advancedLogicalSlots, logicalSlots, logicalCount * sizeof(size_t)) != 0);

  if (logicalChanged) {
    if (logicalCount > 0) {
      memcpy(advancedLogicalSlots, logicalSlots, logicalCount * sizeof(size_t));
    }
    advancedLogicalCount = logicalCount;
    lv_obj_set_height(advancedVirtualContent,
                      espplants_advanced_virtual_list::contentHeight(logicalCount));
    lv_obj_update_layout(advancedList);

    const int32_t scrollY = lv_obj_get_scroll_y(advancedList);
    const int32_t clamped =
        espplants_advanced_virtual_list::clampScrollY(logicalCount, scrollY);
    if (scrollY != clamped) lv_obj_scroll_to_y(advancedList, clamped, LV_ANIM_OFF);
    advancedFirstLogicalIndex = kMaxInfrastructure;
  }

  const int32_t scrollY = espplants_advanced_virtual_list::clampScrollY(
      advancedLogicalCount, lv_obj_get_scroll_y(advancedList));
  const size_t firstLogical = espplants_advanced_virtual_list::firstPoolLogicalIndex(
      advancedLogicalCount, scrollY);
  const bool windowChanged = firstLogical != advancedFirstLogicalIndex;

  for (size_t poolIndex = 0;
       poolIndex < espplants_advanced_virtual_list::kPoolSize; ++poolIndex) {
    InfrastructureRow &row = infrastructureRows[poolIndex];
    const size_t logicalIndex = firstLogical + poolIndex;
    if (logicalIndex >= advancedLogicalCount) {
      row.boundSlot = -1;
      row.boundLogicalIndex = kMaxInfrastructure;
      lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const size_t slot = advancedLogicalSlots[logicalIndex];
    const bool rebound = row.boundSlot != static_cast<int>(slot) ||
                         row.boundLogicalIndex != logicalIndex;
    if (rebound || windowChanged) {
      row.boundSlot = static_cast<int>(slot);
      row.boundLogicalIndex = logicalIndex;
      lv_obj_set_pos(row.box, 0, static_cast<lv_coord_t>(logicalIndex *
          static_cast<size_t>(espplants_advanced_virtual_list::kStride)));
    }

    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    if (rebound || windowChanged || forceValues) {
      staticRowLabel(row.name, row.nameText, sizeof(row.nameText), infrastructure[slot].name);
      staticRowLabel(row.status, row.statusText, sizeof(row.statusText),
                     infrastructure[slot].online ? "ONLINE" : "OFFLINE");
      char signal[16]{};
      if (infrastructure[slot].online)
        snprintf(signal, sizeof(signal), "LQI %u", infrastructure[slot].lqi);
      else
        snprintf(signal, sizeof(signal), "LQI --");
      staticRowLabel(row.signal, row.signalText, sizeof(row.signalText), signal);
      lv_obj_set_style_bg_color(
          row.box,
          lv_color_hex(selectedInfrastructure == static_cast<int>(slot) ? 0x1E3529
                                                                        : 0x1D2922),
          0);
    }
  }

  advancedFirstLogicalIndex = firstLogical;
  advancedVirtualBinding = false;
}

void advancedListScrollEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_SCROLL) return;
  refreshAdvancedVirtualList(false);
}

void infrastructureRowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto *row = static_cast<InfrastructureRow *>(lv_event_get_user_data(event));
  if (!row || row->boundSlot < 0 ||
      row->boundSlot >= static_cast<int>(kMaxInfrastructure) ||
      !infrastructure[row->boundSlot].used) return;
  selectedInfrastructure = row->boundSlot;
  dirty.advanced = true;
}

void infrastructureRenameEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  openInfrastructureRename(selectedInfrastructure);
}

void infrastructureRemoveEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (selectedInfrastructure < 0 ||
      selectedInfrastructure >= static_cast<int>(kMaxInfrastructure) ||
      !infrastructure[selectedInfrastructure].used) return;
  showInfrastructureRemoveConfirm(selectedInfrastructure);
}

void replaceEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) return;
  startPairing(true, selectedSensor);
}

void removeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) return;
  showRemoveConfirm(selectedSensor);
}

void closeRename() {
  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_btnmatrix_set_one_checked(renameKeyboard,false);
    lv_btnmatrix_clear_btn_ctrl_all(
        renameKeyboard,
        static_cast<lv_btnmatrix_ctrl_t>(
            LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CHECKED));
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_width(renameHint,520);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}

void saveRename() {
  const char *text = lv_textarea_get_text(renameInput);
  if (!text || !text[0]) {
    closeRename();
    return;
  }

  if (renameTarget == RenameTarget::Device) {
    strncpy(deviceName, text, sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';
    preferences.putString("device_name", deviceName);
    label(headerTitle, deviceName);
    label(settingsDeviceName, deviceName);
    Serial.printf("[settings] device name=\"%s\"\n", deviceName);
    dirty.header = true;
    dirty.settings = true;
    closeRename();
    return;
  }

  if (renameTarget == RenameTarget::Infrastructure) {
    if (selectedInfrastructure < 0 ||
        selectedInfrastructure >= static_cast<int>(kMaxInfrastructure) ||
        !infrastructure[selectedInfrastructure].used) {
      closeRename();
      return;
    }
    InfrastructureNode &node = infrastructure[selectedInfrastructure];
    strncpy(node.name, text, sizeof(node.name) - 1);
    node.name[sizeof(node.name) - 1] = '\0';
    saveInfrastructureSlot(static_cast<size_t>(selectedInfrastructure));
    Serial.printf("[registry] renamed repeater slot=%u name=\"%s\"\n",
                  static_cast<unsigned>(selectedInfrastructure + 1), node.name);
    markInfrastructureDirty();
    closeRename();
    return;
  }

  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) {
    closeRename();
    return;
  }

  PlantSensor &s = sensors[selectedSensor];
  strncpy(s.name, text, sizeof(s.name) - 1);
  s.name[sizeof(s.name) - 1] = '\0';
  saveSlot(static_cast<size_t>(selectedSensor));
  Serial.printf("[registry] renamed slot=%u name=\"%s\"\n",
                static_cast<unsigned>(selectedSensor + 1), s.name);
  dirty.home = true;
  dirty.all = true;
  dirty.plant = true;
  closeRename();
}

static const char *kRenameUpperMap[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", "DEL", "\n",
    "SPACE", "CANCEL", "SAVE", ""
};

static const char *kRenameLowerMap[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", "DEL", "\n",
    "SPACE", "CANCEL", "SAVE", ""
};

void renameKeyboardEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  const uint16_t id = lv_btnmatrix_get_selected_btn(renameKeyboard);
  const char *key = lv_btnmatrix_get_btn_text(renameKeyboard, id);
  if (!key) return;

  if (personalityDialogActive) {
    if (strcmp(key,"SAVE")==0) {
      if (pendingTheme!=phraseTheme) {
        phraseTheme=pendingTheme;
        preferences.putUChar("phrase_theme",static_cast<uint8_t>(phraseTheme));
        for (auto &sensor:sensors) sensor.phraseRotation=espplants_phrases::Rotation{};
        Serial.printf("[settings] phrase theme=%s\n",
                      espplants_phrases::themeName(phraseTheme));
      }
      dirty.home = true;
      dirty.plant = true;
      dirty.settings = true;
      closeRename();
      return;
    }

    if (strcmp(key,"CANCEL")==0) {
      pendingTheme=phraseTheme;
      closeRename();
      return;
    }

    if (strcmp(key,"CLASSIC")==0) pendingTheme=espplants_phrases::Theme::CLASSIC;
    else if (strcmp(key,"FUNNY")==0) pendingTheme=espplants_phrases::Theme::FUNNY;
    else if (strcmp(key,"SARCASTIC")==0) pendingTheme=espplants_phrases::Theme::SARCASTIC;
    else if (strcmp(key,"DRAMATIC")==0) pendingTheme=espplants_phrases::Theme::DRAMATIC;
    else if (strcmp(key,"RUDE")==0) pendingTheme=espplants_phrases::Theme::RUDE;
    else if (strcmp(key,"MIXED - ALL")==0) pendingTheme=espplants_phrases::Theme::MIXED;
    else return;

    refreshPersonalitySelection();
    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s   SAVE to apply",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
  }

  if (strcmp(key, "SAVE") == 0) {
    saveRename();
  } else if (strcmp(key, "CANCEL") == 0) {
    closeRename();
  } else if (strcmp(key, "SPACE") == 0) {
    lv_textarea_add_text(renameInput, " ");
  } else if (strcmp(key, "DEL") == 0) {
    lv_textarea_del_char(renameInput);
  } else if (strcmp(key, "abc") == 0) {
    renameUppercase = false;
    lv_btnmatrix_set_map(renameKeyboard, kRenameLowerMap);
  } else if (strcmp(key, "ABC") == 0) {
    renameUppercase = true;
    lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  } else if (strlen(key) == 1) {
    lv_textarea_add_text(renameInput, key);
  }
}

void openPlantRename(int slot) {
  if (slot < 0 || slot >= static_cast<int>(kMaxSensors) || !sensors[slot].used) return;
  selectedSensor = slot;
  renameTarget = RenameTarget::Plant;

  char titleText[48]{};
  snprintf(titleText, sizeof(titleText), "RENAME PLANT %u",
           static_cast<unsigned>(selectedSensor + 1));
  label(renameTitle, titleText);
  label(renameHint, "Name stays tied to this sensor.");

  lv_textarea_set_text(renameInput, sensors[selectedSensor].name);
  lv_textarea_set_cursor_pos(renameInput, LV_TEXTAREA_CURSOR_LAST);
  renameUppercase = true;
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
  logLvglMemory("rename-plant-open");
}

void renameEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  openPlantRename(selectedSensor);
}

void openInfrastructureRename(int slot) {
  if (slot < 0 || slot >= static_cast<int>(kMaxInfrastructure) ||
      !infrastructure[slot].used) return;
  selectedInfrastructure = slot;
  renameTarget = RenameTarget::Infrastructure;

  label(renameTitle, "RENAME REPEATER");
  label(renameHint, "Name stays tied to this Zigbee router.");
  lv_textarea_set_text(renameInput, infrastructure[slot].name);
  lv_textarea_set_cursor_pos(renameInput, LV_TEXTAREA_CURSOR_LAST);
  renameUppercase = true;
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
  logLvglMemory("rename-repeater-open");
}

void deviceNameEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  renameTarget = RenameTarget::Device;
  label(renameTitle, "RENAME ESP PLANTS");
  label(renameHint, "This name appears in the display header.");
  lv_textarea_set_text(renameInput, deviceName);
  lv_textarea_set_cursor_pos(renameInput, LV_TEXTAREA_CURSOR_LAST);
  renameUppercase = true;
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
  logLvglMemory("rename-device-open");
}


void closePairDialog() {
  pairDialogState = PairDialogState::Hidden;
  pairFoundSlot = -1;
  pairFoundInfrastructure = -1;
  pairInfrastructure = false;
  pairRemovingInfrastructure = false;
  if (pairModal) lv_obj_add_flag(pairModal, LV_OBJ_FLAG_HIDDEN);
  dirty.settings = true;
  dirty.pair = false;
}

void startPairing(bool replacing, int targetSlot) {
  pairInfrastructure = false;
  pairRemovingInfrastructure = false;
  pairFoundInfrastructure = -1;
  if (replacing) {
    if (targetSlot < 0 || targetSlot >= static_cast<int>(kMaxSensors) ||
        !sensors[targetSlot].used) return;
  } else if (registeredCount() >= kMaxSensors) {
    pairReplacing = false;
    pairTargetSlot = -1;
    pairFoundSlot = -1;
    pairDialogState = PairDialogState::TimedOut;
    dirty.pair = true;
    return;
  }

  pairReplacing = replacing;
  pairTargetSlot = replacing ? targetSlot : -1;
  pairFoundSlot = -1;
  pairStartedMs = millis();
  pairDialogState = PairDialogState::Pairing;

  if (h2Online && networkReady) {
    requestJoin(120);
  } else {
    permitJoinRemaining = 0;
    pairDialogState = PairDialogState::TimedOut;
  }
  dirty.pair = true;
}

void startInfrastructurePairing() {
  pairInfrastructure = true;
  pairRemovingInfrastructure = false;
  pairReplacing = false;
  pairTargetSlot = -1;
  pairFoundSlot = -1;
  pairFoundInfrastructure = -1;
  pairStartedMs = millis();

  if (infrastructureCount() >= kMaxInfrastructure) {
    pairDialogState = PairDialogState::TimedOut;
    dirty.pair = true;
    return;
  }

  pairDialogState = PairDialogState::Pairing;
  if (h2Online && networkReady) {
    requestJoin(120);
  } else {
    permitJoinRemaining = 0;
    pairDialogState = PairDialogState::TimedOut;
  }
  dirty.pair = true;
}

void showRemoveConfirm(int targetSlot) {
  if (targetSlot < 0 || targetSlot >= static_cast<int>(kMaxSensors) ||
      !sensors[targetSlot].used) return;
  pairInfrastructure = false;
  pairRemovingInfrastructure = false;
  pairReplacing = false;
  pairTargetSlot = targetSlot;
  pairFoundSlot = -1;
  pairDialogState = PairDialogState::RemoveConfirm;
  dirty.pair = true;
}

void showInfrastructureRemoveConfirm(int targetSlot) {
  if (targetSlot < 0 || targetSlot >= static_cast<int>(kMaxInfrastructure) ||
      !infrastructure[targetSlot].used) return;
  pairInfrastructure = true;
  pairRemovingInfrastructure = true;
  pairReplacing = false;
  pairTargetSlot = targetSlot;
  pairFoundSlot = -1;
  pairFoundInfrastructure = -1;
  pairDialogState = PairDialogState::RemoveConfirm;
  dirty.pair = true;
}

void pairPrimaryEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (pairDialogState == PairDialogState::Found) {
    const bool infra = pairInfrastructure;
    const int plantSlot = pairFoundSlot;
    const int infraSlot = pairFoundInfrastructure;
    closePairDialog();
    if (infra) openInfrastructureRename(infraSlot);
    else openPlantRename(plantSlot);
    return;
  }

  if (pairDialogState == PairDialogState::TimedOut) {
    if (pairInfrastructure) startInfrastructurePairing();
    else startPairing(pairReplacing, pairTargetSlot);
    return;
  }

  if (pairDialogState == PairDialogState::RemoveConfirm) {
    const int slot = pairTargetSlot;
    if (pairRemovingInfrastructure) {
      if (slot >= 0 && slot < static_cast<int>(kMaxInfrastructure) &&
          infrastructure[slot].used) {
        uint8_t oldIeee[8]{};
        memcpy(oldIeee, infrastructure[slot].ieee, sizeof(oldIeee));
        requestRemoveDevice(oldIeee);
        clearInfrastructureSlot(static_cast<size_t>(slot));
      }
    } else if (slot >= 0 && slot < static_cast<int>(kMaxSensors) &&
               sensors[slot].used) {
      uint8_t oldIeee[8]{};
      memcpy(oldIeee, sensors[slot].ieee, sizeof(oldIeee));
      requestRemoveDevice(oldIeee);
      clearPlantSlot(static_cast<size_t>(slot));
    }
    closePairDialog();
  }
}

void pairSecondaryEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (pairDialogState == PairDialogState::Pairing) requestJoin(0);
  closePairDialog();
}

void refreshPairDialog() {
  if (!pairModal) return;

  if (pairDialogState == PairDialogState::Hidden) {
    lv_obj_add_flag(pairModal, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const bool opening = lv_obj_has_flag(pairModal, LV_OBJ_FLAG_HIDDEN);

  if (pairDialogState == PairDialogState::Pairing &&
      permitJoinRemaining == 0 &&
      millis() - pairStartedMs > 2500u) {
    pairDialogState = PairDialogState::TimedOut;
  }

  lv_obj_clear_flag(pairModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(pairModal);

  char text[180]{};
  lv_obj_set_style_bg_color(pairPrimary, lv_color_hex(0x3F7A4E), 0);

  if (pairDialogState == PairDialogState::Pairing) {
    if (pairInfrastructure) {
      label(pairTitle, "ADD REPEATER");
      label(pairInstruction,
            "Put the Zigbee repeater/router into its normal pairing mode.");
      snprintf(text, sizeof(text), "PAIRING... %u s", permitJoinRemaining);
    } else {
      label(pairTitle, pairReplacing ? "REPLACE SENSOR" : "ADD SENSOR");
      label(pairInstruction,
            pairReplacing
                ? "Hold the NEW sensor's water/drop button until its red LED begins flashing."
                : "Hold the sensor's water/drop button until its red LED begins flashing.");
      snprintf(text, sizeof(text), "PAIRING... %u s", permitJoinRemaining);
    }
    label(pairStatus, text);
    lv_obj_add_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);
    label(pairSecondaryLabel, "CANCEL");
  } else if (pairDialogState == PairDialogState::Found) {
    if (pairInfrastructure) {
      label(pairTitle, "REPEATER FOUND");
      if (pairFoundInfrastructure >= 0 &&
          pairFoundInfrastructure < static_cast<int>(kMaxInfrastructure) &&
          infrastructure[pairFoundInfrastructure].used) {
        snprintf(text, sizeof(text), "Added as %s.",
                 infrastructure[pairFoundInfrastructure].name);
        label(pairInstruction, text);
      } else {
        label(pairInstruction, "The Zigbee repeater is connected.");
      }
      label(pairStatus, "Name it now, or tap DONE.");
      lv_obj_clear_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);
      label(pairPrimaryLabel, "RENAME");
      label(pairSecondaryLabel, "DONE");
    } else {
      label(pairTitle, "SENSOR FOUND");
      if (pairFoundSlot >= 0 && pairFoundSlot < static_cast<int>(kMaxSensors) &&
          sensors[pairFoundSlot].used) {
        if (pairReplacing) {
          snprintf(text, sizeof(text), "%s now uses the new sensor.",
                   sensors[pairFoundSlot].name);
        } else {
          snprintf(text, sizeof(text), "Added as %s.", sensors[pairFoundSlot].name);
        }
        label(pairInstruction, text);
      } else {
        label(pairInstruction, "The new sensor is connected.");
      }
      label(pairStatus, "Name it now, or tap DONE.");
      lv_obj_clear_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);
      label(pairPrimaryLabel, "NAME PLANT");
      label(pairSecondaryLabel, "DONE");
    }
  } else if (pairDialogState == PairDialogState::TimedOut) {
    if (!h2Online || !networkReady) {
      label(pairTitle, "ZIGBEE NOT READY");
      label(pairInstruction, "The H2 Zigbee gateway is not ready yet.");
      label(pairStatus, "Check the H2 link, then tap TRY AGAIN.");
    } else if (pairInfrastructure && infrastructureCount() >= kMaxInfrastructure) {
      label(pairTitle, "REPEATER LIST FULL");
      label(pairInstruction, "ESP PLANTS already has 32 saved repeaters/routers.");
      label(pairStatus, "Remove one first, then add another.");
    } else if (!pairInfrastructure && !pairReplacing &&
               registeredCount() >= kMaxSensors) {
      label(pairTitle, "NO FREE PLANT SLOTS");
      label(pairInstruction, "ESP PLANTS already has 32 registered plants.");
      label(pairStatus, "Remove a plant first, then add the new sensor.");
    } else if (pairInfrastructure) {
      label(pairTitle, "NO REPEATER FOUND");
      label(pairInstruction, "No new Zigbee router appeared before pairing closed.");
      label(pairStatus, "Put the repeater in pairing mode and try again.");
    } else {
      label(pairTitle, "NO SENSOR FOUND");
      label(pairInstruction, "No new sensor reported before the pairing window closed.");
      label(pairStatus, "Put the sensor in pairing mode and try again.");
    }
    lv_obj_clear_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);
    label(pairPrimaryLabel, "TRY AGAIN");
    label(pairSecondaryLabel, "CLOSE");
  } else if (pairDialogState == PairDialogState::RemoveConfirm) {
    if (pairRemovingInfrastructure) {
      label(pairTitle, "REMOVE REPEATER?");
      if (pairTargetSlot >= 0 &&
          pairTargetSlot < static_cast<int>(kMaxInfrastructure) &&
          infrastructure[pairTargetSlot].used) {
        snprintf(text, sizeof(text), "Remove %s from the Zigbee network?",
                 infrastructure[pairTargetSlot].name);
        label(pairInstruction, text);
      } else {
        label(pairInstruction, "Remove this Zigbee repeater?");
      }
      label(pairStatus, "The H2 will ask the router to leave the Zigbee network.");
    } else {
      label(pairTitle, "REMOVE SENSOR?");
      if (pairTargetSlot >= 0 && pairTargetSlot < static_cast<int>(kMaxSensors) &&
          sensors[pairTargetSlot].used) {
        snprintf(text, sizeof(text), "Remove %s from ESP PLANTS?",
                 sensors[pairTargetSlot].name);
        label(pairInstruction, text);
      } else {
        label(pairInstruction, "Remove this plant sensor?");
      }
      label(pairStatus, "This deletes the plant slot and asks the sensor to leave the Zigbee network.");
    }
    lv_obj_clear_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(pairPrimary, lv_color_hex(0x8E493E), 0);
    label(pairPrimaryLabel, "REMOVE");
    label(pairSecondaryLabel, "CANCEL");
  }

  if (opening) logLvglMemory("pairing-ui-open");
}

void buildHeader(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns header geometry and styling. Runtime text and callbacks
  // remain native ESP PLANTS behavior.
  headerTitle = objects.header_title;
  headerUpdateButton = objects.header_update_button;
  headerCount = objects.header_count;

  label(headerTitle, deviceName);
  lv_obj_add_event_cb(headerUpdateButton, openUpdateEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);
}

void buildHome(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the static Home visual tree. ESP PLANTS keeps all runtime
  // behavior/data binding and the recycled sensor-row pool.
  homePage = objects.home_page;
  homeSummary = objects.home_summary;
  homeName = objects.home_name;
  homeMood = objects.home_mood;
  homeSoil = objects.home_soil;
  homeTemp = objects.home_temp;
  homeHumidity = objects.home_humidity;
  homeBar = objects.home_bar;
  homeWarning = objects.home_warning;
  homeList = objects.home_list;

  lv_obj_add_flag(objects.home_featured_card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(objects.home_featured_card, featuredEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(homeList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(homeList, LV_DIR_VER);
  lv_obj_add_event_cb(homeList, homeListScrollEvent, LV_EVENT_SCROLL, nullptr);

  homeVirtualContent = lv_obj_create(homeList);
  lv_obj_set_pos(homeVirtualContent, 0, 0);
  lv_obj_set_size(homeVirtualContent, 228,
                  espplants_home_virtual_list::contentHeight(0));
  lv_obj_set_style_border_width(homeVirtualContent, 0, 0);
  lv_obj_set_style_bg_opa(homeVirtualContent, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(homeVirtualContent, 0, 0);
  lv_obj_clear_flag(homeVirtualContent, LV_OBJ_FLAG_SCROLLABLE);

  for (size_t i = 0; i < espplants_home_virtual_list::kPoolSize; ++i) {
    PlantListRow &row = rows[i];
    row.box = lv_obj_create(homeVirtualContent);
    lv_obj_set_pos(row.box, 0, 0);
    lv_obj_set_size(row.box, 228, espplants_home_virtual_list::kRowHeight);
    lv_obj_set_style_radius(row.box, 12, 0);
    lv_obj_set_style_border_width(row.box, 0, 0);
    lv_obj_set_style_bg_color(row.box, lv_color_hex(0x1D2922), 0);
    lv_obj_set_style_pad_all(row.box, 8, 0);
    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row.box, rowEvent, LV_EVENT_CLICKED, &row);

    row.name = lv_label_create(row.box);
    row.nameText[0] = '\0';
    lv_label_set_text_static(row.name, row.nameText);
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row.name, lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_width(row.name, 145);
    lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);

    row.moisture = lv_label_create(row.box);
    row.moistureText[0] = '\0';
    lv_label_set_text_static(row.moisture, row.moistureText);
    lv_obj_set_style_text_font(row.moisture, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(row.moisture, lv_color_hex(0xE5ECE7), 0);
    lv_obj_align(row.moisture, LV_ALIGN_TOP_RIGHT, -2, -2);

    row.bar = lv_bar_create(row.box);
    lv_obj_set_pos(row.bar, 2, 30);
    lv_obj_set_size(row.bar, 208, 9);
    lv_bar_set_range(row.bar, 0, 100);
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(0x2A352E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(0x5E9B68), LV_PART_INDICATOR);
  }
}

void buildAll(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the static All Sensors page. ESP PLANTS keeps the
  // recycled row pool and all runtime data binding in native C++.
  allPage = objects.all_page;
  allSummary = objects.all_summary;
  allList = objects.all_list;

  lv_obj_add_flag(allList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(allList, LV_DIR_VER);
  lv_obj_add_event_cb(allList, allListScrollEvent, LV_EVENT_SCROLL, nullptr);

  allVirtualContent = lv_obj_create(allList);
  lv_obj_set_pos(allVirtualContent, 0, 0);
  lv_obj_set_size(allVirtualContent, 742,
                  espplants_all_virtual_list::contentHeight(0));
  lv_obj_set_style_border_width(allVirtualContent, 0, 0);
  lv_obj_set_style_bg_opa(allVirtualContent, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(allVirtualContent, 0, 0);
  lv_obj_clear_flag(allVirtualContent, LV_OBJ_FLAG_SCROLLABLE);

  for (size_t i = 0; i < espplants_all_virtual_list::kPoolSize; ++i) {
    AllSensorRow &row = allRows[i];
    row.box = lv_obj_create(allVirtualContent);
    lv_obj_set_pos(row.box, 0, 0);
    lv_obj_set_size(row.box, 742, espplants_all_virtual_list::kRowHeight);
    lv_obj_set_style_radius(row.box, 10, 0);
    lv_obj_set_style_border_width(row.box, 0, 0);
    lv_obj_set_style_bg_color(row.box, lv_color_hex(0x1D2922), 0);
    lv_obj_set_style_pad_all(row.box, 8, 0);
    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row.box, allRowEvent, LV_EVENT_CLICKED, &row);

    row.name = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(row.name, lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_pos(row.name, 4, 9);
    lv_obj_set_width(row.name, 285);
    lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);

    row.moisture = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.moisture, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(row.moisture, lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_pos(row.moisture, 305, 8);
    lv_obj_set_width(row.moisture, 110);
    lv_obj_set_style_text_align(row.moisture, LV_TEXT_ALIGN_CENTER, 0);

    row.battery = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.battery, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(row.battery, lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_pos(row.battery, 435, 9);
    lv_obj_set_width(row.battery, 115);
    lv_obj_set_style_text_align(row.battery, LV_TEXT_ALIGN_CENTER, 0);

    row.updated = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row.updated, lv_color_hex(0xD1DED5), 0);
    lv_obj_set_pos(row.updated, 565, 10);
    lv_obj_set_width(row.updated, 155);
    lv_obj_set_style_text_align(row.updated, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(row.updated, LV_LABEL_LONG_DOT);
  }
}

void buildPlant(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns Plant Detail geometry and styling. Runtime values and
  // button behavior remain native ESP PLANTS code.
  plantPage = objects.plant_page;
  detailSlot = objects.detail_slot;
  detailName = objects.detail_name;
  detailMood = objects.detail_mood;
  detailIeee = objects.detail_ieee;
  detailSoil = objects.detail_soil;
  detailTemp = objects.detail_temp;
  detailHumidity = objects.detail_humidity;
  detailBattery = objects.detail_battery;
  detailSignal = objects.detail_signal;
  detailBar = objects.detail_bar;
  detailUpdated = objects.detail_updated;
  detailWarning = objects.detail_warning;
  renameButton = objects.rename_button;
  replaceButton = objects.replace_button;
  removeButton = objects.remove_button;

  lv_bar_set_range(detailBar, 0, 100);
  lv_obj_add_event_cb(renameButton, renameEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(replaceButton, replaceEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(removeButton, removeEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
}

void buildSettings(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns Settings geometry and styling. The existing callbacks and
  // runtime status strings remain authoritative in native C++.
  settingsPage = objects.settings_page;
  settingsDeviceName = objects.settings_device_name;
  settingsH2 = objects.settings_h2;
  settingsPlants = objects.settings_plants;
  settingsZigbee = objects.settings_zigbee;
  settingsUnit = objects.settings_unit;
  settingsTheme = objects.settings_theme;
  settingsPair = objects.settings_pair;

  lv_obj_add_event_cb(objects.settings_device_button, deviceNameEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.settings_network_button, openUpdateEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.settings_unit_button, unitEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.settings_theme_button, themeEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.settings_pair_button, pairEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.settings_advanced_button, advancedEvent,
                      LV_EVENT_CLICKED, nullptr);
}

void buildAdvanced(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the static Advanced Zigbee page. Runtime repeater rows
  // remain a recycled native C++ pool so the screen scales without duplicate
  // full-screen LVGL trees.
  advancedPage = objects.advanced_page;
  advancedSummary = objects.advanced_summary;
  advancedAddButton = objects.advanced_add_button;
  advancedList = objects.advanced_list;
  advancedDetail = objects.advanced_detail;
  advancedRenameButton = objects.advanced_rename_button;
  advancedRemoveButton = objects.advanced_remove_button;

  lv_obj_add_event_cb(objects.advanced_back_button, advancedBackEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(advancedAddButton, infrastructureAddEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(advancedList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(advancedList, LV_DIR_VER);
  lv_obj_add_event_cb(advancedList, advancedListScrollEvent,
                      LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(advancedRenameButton, infrastructureRenameEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(advancedRemoveButton, infrastructureRemoveEvent,
                      LV_EVENT_CLICKED, nullptr);

  advancedVirtualContent = lv_obj_create(advancedList);
  lv_obj_set_pos(advancedVirtualContent, 0, 0);
  lv_obj_set_size(advancedVirtualContent, 742,
                  espplants_advanced_virtual_list::kViewportHeight);
  lv_obj_set_style_border_width(advancedVirtualContent, 0, 0);
  lv_obj_set_style_bg_opa(advancedVirtualContent, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(advancedVirtualContent, 0, 0);
  lv_obj_clear_flag(advancedVirtualContent, LV_OBJ_FLAG_SCROLLABLE);

  for (size_t i = 0; i < espplants_advanced_virtual_list::kPoolSize; ++i) {
    InfrastructureRow &row = infrastructureRows[i];
    row.box = lv_obj_create(advancedVirtualContent);
    lv_obj_set_pos(row.box, 0, 0);
    lv_obj_set_size(row.box, 742, 50);
    lv_obj_set_style_radius(row.box, 10, 0);
    lv_obj_set_style_border_width(row.box, 0, 0);
    lv_obj_set_style_bg_color(row.box, lv_color_hex(0x1D2922), 0);
    lv_obj_set_style_pad_all(row.box, 8, 0);
    lv_obj_clear_flag(row.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row.box, infrastructureRowEvent, LV_EVENT_CLICKED, &row);

    row.name = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(row.name, lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_pos(row.name, 2, 6);
    lv_obj_set_width(row.name, 390);
    lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
    lv_label_set_text_static(row.name, row.nameText);

    row.status = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row.status, lv_color_hex(0xD1DED5), 0);
    lv_obj_set_pos(row.status, 430, 7);
    lv_obj_set_width(row.status, 110);
    lv_label_set_text_static(row.status, row.statusText);

    row.signal = lv_label_create(row.box);
    lv_obj_set_style_text_font(row.signal, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row.signal, lv_color_hex(0xD1DED5), 0);
    lv_obj_set_pos(row.signal, 570, 7);
    lv_obj_set_width(row.signal, 145);
    lv_label_set_text_static(row.signal, row.signalText);
  }

  lv_obj_add_state(advancedRenameButton, LV_STATE_DISABLED);
  lv_obj_add_state(advancedRemoveButton, LV_STATE_DISABLED);
}

void buildNav(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the locked bottom-navigation geometry and styling.
  navHome = objects.nav_home;
  navAll = objects.nav_all;
  navPlant = objects.nav_plant;
  navSettings = objects.nav_settings;

  lv_obj_add_event_cb(navHome, navEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(Page::Home)));
  lv_obj_add_event_cb(navAll, navEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(Page::All)));
  lv_obj_add_event_cb(navPlant, navEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(Page::Plant)));
  lv_obj_add_event_cb(navSettings, navEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(Page::Settings)));
}
void buildUpdateDialog(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns all static Network & Updates, Release Notes, and
  // confirmation geometry. The QR bitmap itself stays runtime-generated so it
  // can encode the active setup credentials without duplicating a screen tree.
  updateModal = objects.update_modal;
  updateWifiState = objects.update_wifi_state;
  updateWifiDetail = objects.update_wifi_detail;
  updateSetupButton = objects.update_setup_button;
  updateSetupLabel = objects.update_setup_label;
  updateDisconnectButton = objects.update_disconnect_button;
  updateDisconnectLabel = objects.update_disconnect_label;
  updateForgetButton = objects.update_forget_button;
  updateQrHint = objects.update_qr_hint;
  updateQrCard = objects.update_qr_card;
  updatePortalInfo = objects.update_portal_info;
  updateCurrentVersion = objects.update_current_version;
  updateLatestVersion = objects.update_latest_version;
  updateH2Version = objects.update_h2_version;
  updateStatus = objects.update_status;
  updateCheckButton = objects.update_check_button;
  updateCheckLabel = objects.update_check_label;
  updateInstallButton = objects.update_install_button;
  updateInstallLabel = objects.update_install_label;
  releaseNotesModal = objects.release_notes_modal;
  releaseNotesLabel = objects.release_notes_label;
  wifiForgetConfirm = objects.wifi_forget_confirm;

  lv_obj_add_event_cb(objects.update_close_button, closeUpdateEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(updateSetupButton, wifiSetupEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(updateDisconnectButton, wifiDisconnectEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(updateForgetButton, wifiForgetAskEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(updateCheckButton, updateCheckEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(updateInstallButton, updateInstallEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.release_notes_back_button, releaseNotesBackEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.release_notes_check_button, releaseNotesCheckEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.wifi_forget_cancel_button, wifiForgetCancelEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(objects.wifi_forget_confirm_button, wifiForgetConfirmEvent,
                      LV_EVENT_CLICKED, nullptr);

  lv_obj_set_scroll_dir(objects.release_notes_scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(objects.release_notes_scroll, LV_SCROLLBAR_MODE_AUTO);

  updateQrCode = lv_canvas_create(updateQrCard);
  lv_canvas_set_buffer(updateQrCode, updateQrCanvasBuffer,
                       kSetupQrSize, kSetupQrSize, LV_IMG_CF_INDEXED_1BIT);
  lv_canvas_set_palette(updateQrCode, 0, lv_color_black());
  lv_canvas_set_palette(updateQrCode, 1, lv_color_white());
  clearSetupQrCanvas();
  lv_obj_set_size(updateQrCode, kSetupQrSize, kSetupQrSize);
  lv_obj_center(updateQrCode);

  lv_obj_add_flag(updateQrHint, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(updateQrCard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(releaseNotesModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(updateModal, LV_OBJ_FLAG_HIDDEN);
}

void buildRename(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the full-screen rename/personality layout. Firmware owns
  // text entry behavior, keyboard maps, persistence, and personality state.
  renameModal = objects.rename_modal;
  renameTitle = objects.rename_title;
  renameHint = objects.rename_hint;
  renameInput = objects.rename_input;
  renameKeyboard = objects.rename_keyboard;

  lv_textarea_set_one_line(renameInput, true);
  lv_textarea_set_max_length(renameInput, kPlantNameBytes - 1);
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_add_event_cb(renameKeyboard, renameKeyboardEvent,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}



void buildPairDialog(lv_obj_t *screen) {
  (void)screen;

  // EEZ Studio owns the pair/remove dialog geometry and styling. Firmware
  // keeps the pairing state machine and all Zigbee behavior.
  pairModal = objects.pair_modal;
  pairTitle = objects.pair_title;
  pairInstruction = objects.pair_instruction;
  pairStatus = objects.pair_status;
  pairPrimary = objects.pair_primary;
  pairPrimaryLabel = objects.pair_primary_label;
  pairSecondary = objects.pair_secondary;
  pairSecondaryLabel = objects.pair_secondary_label;

  lv_obj_add_event_cb(pairPrimary, pairPrimaryEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(pairSecondary, pairSecondaryEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(pairModal, LV_OBJ_FLAG_HIDDEN);
}

void buildUi() {
  lv_obj_t *oldScreen = lv_scr_act();
  create_screen_home();
  lv_obj_t *screen = objects.home;
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101814), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xE5ECE7), LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  buildHeader(screen);
  buildHome(screen);
  buildAll(screen);
  buildPlant(screen);
  buildSettings(screen);
  buildAdvanced(screen);
  buildNav(screen);
  buildRename(screen);
  buildPairDialog(screen);
  buildUpdateDialog(screen);
  showPage(Page::Home);
  lv_scr_load(screen);
  if (oldScreen && oldScreen != screen) lv_obj_del(oldScreen);
  logLvglMemory("build-ui");
}

void formatTemp(const PlantSensor &s, char *out, size_t size) {
  if (!(s.fieldFlags & plantlink::SensorHasTemperature)) {
    snprintf(out, size, useFahrenheit ? "--.- F" : "--.- C");
    return;
  }
  const float c = s.temperatureCentiC / 100.0f;
  if (useFahrenheit) snprintf(out, size, "%.1f F", c * 9.0f / 5.0f + 32.0f);
  else snprintf(out, size, "%.1f C", c);
}

void formatHumidity(const PlantSensor &s, char *out, size_t size) {
  if (s.fieldFlags & plantlink::SensorHasHumidity) snprintf(out, size, "%.0f%%", s.humidityCentiPct / 100.0f);
  else snprintf(out, size, "--%%");
}

void formatSoil(const PlantSensor &s, char *out, size_t size) {
  if (s.fieldFlags & plantlink::SensorHasSoilMoisture) snprintf(out, size, "%u%%", s.soilMoisturePct);
  else snprintf(out, size, "--%%");
}

void refreshUi(bool force, bool alreadyInLvglContext) {
  const uint32_t now = millis();
  const bool intervalElapsed = now - lastUiRefreshMs >= kUiRefreshIntervalMs;

  if (currentPage == Page::Home && manualHomeSensor >= 0 &&
      static_cast<int32_t>(manualHomeUntilMs - now) <= 0) {
    manualHomeSensor = -1;
    manualHomeUntilMs = 0;
    dirty.home = true;
  }

  bool pageDirty = false;
  switch (currentPage) {
    case Page::Home: pageDirty = dirty.home; break;
    case Page::All: pageDirty = dirty.all; break;
    case Page::Plant: pageDirty = dirty.plant; break;
    case Page::Settings: pageDirty = dirty.settings; break;
    case Page::Advanced: pageDirty = dirty.advanced; break;
  }

  const bool timedPageWork = intervalElapsed &&
      (currentPage == Page::All || currentPage == Page::Plant);
  const bool timedModalWork = intervalElapsed &&
      (updateModalOpen || pairDialogState != PairDialogState::Hidden);
  const bool modalDirty =
      (updateModalOpen && dirty.update) ||
      (pairDialogState != PairDialogState::Hidden && dirty.pair);

  if (!force && !dirty.header && !pageDirty && !modalDirty &&
      !timedPageWork && !timedModalWork) return;

#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
  const uint64_t refreshStartedUs = esp_timer_get_time();
  const uint64_t lockStartedUs = refreshStartedUs;
  uint32_t lockWaitUs = 0;
#endif

  bool lockedHere = false;
  if (!alreadyInLvglContext) {
    if (!lvgl_port_lock(-1)) return;
    lockedHere = true;
#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
    const uint64_t lockWait64 = esp_timer_get_time() - lockStartedUs;
    lockWaitUs = lockWait64 > UINT32_MAX ? UINT32_MAX
                                         : static_cast<uint32_t>(lockWait64);
#endif
  }

  if (intervalElapsed) lastUiRefreshMs = now;

  char text[200]{};
  const size_t count = registeredCount();
  const size_t reporting = reportedCount();
  const size_t waiting = count >= reporting ? count - reporting : 0;

  if (force || dirty.header) {
    snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(count),
             count == 1 ? "PLANT" : "PLANTS");
    label(headerCount, text);
    if (headerUpdateButton) {
      if (espplants_update::updateAvailable())
        lv_obj_clear_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);
    }
    dirty.header = false;
  }

  switch (currentPage) {
    case Page::Home: {
      if (!force && !dirty.home) break;
      const int homeSensor = featuredHomeSensor();
      snprintf(text, sizeof(text), "%u REPORTING | %u WAITING",
               static_cast<unsigned>(reporting), static_cast<unsigned>(waiting));
      label(homeSummary, text);

      refreshHomeVirtualList(true);

      if (homeSensor < 0 || homeSensor >= static_cast<int>(kMaxSensors) ||
          !sensors[homeSensor].used) {
        label(homeName, count ? "WAITING FOR REPORTS" : "WAITING FOR SENSOR");
        if (count) {
          snprintf(text, sizeof(text), "%u %s waiting to report",
                   static_cast<unsigned>(waiting), waiting == 1 ? "sensor" : "sensors");
          label(homeMood, text);
        } else {
          label(homeMood, "Pair a sensor and I'll keep an eye on it");
        }
        label(homeSoil, "--%");
        label(homeTemp, useFahrenheit ? "--.- F" : "--.- C");
        label(homeHumidity, "--%");
        lv_bar_set_value(homeBar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);
      } else {
        PlantSensor &home = sensors[homeSensor];
        label(homeName, home.name);
        label(homeMood, mood(home));
        formatSoil(home, text, sizeof(text));
        label(homeSoil, text);
        lv_bar_set_value(homeBar, hasFreshMoisture(home) ? home.soilMoisturePct : 0,
                         LV_ANIM_OFF);
        formatTemp(home, text, sizeof(text));
        label(homeTemp, text);
        formatHumidity(home, text, sizeof(text));
        label(homeHumidity, text);
        if (home.seenThisBoot &&
            (home.fieldFlags & plantlink::SensorHasWaterWarning) && home.waterWarning)
          lv_obj_clear_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);
        else
          lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);
      }
      dirty.home = false;
      break;
    }

    case Page::All: {
      if (!force && !dirty.all && !intervalElapsed) break;
      snprintf(text, sizeof(text), "%u REPORTING | %u WAITING",
               static_cast<unsigned>(reporting), static_cast<unsigned>(waiting));
      label(allSummary, text);
      refreshAllVirtualList(true);
      dirty.all = false;
      break;
    }

    case Page::Plant: {
      if (!force && !dirty.plant && !intervalElapsed) break;
      const bool valid = selectedSensor >= 0 &&
                         selectedSensor < static_cast<int>(kMaxSensors) &&
                         sensors[selectedSensor].used;
      if (!valid) {
        label(detailSlot, "PLANT --");
        label(detailName, "NO PLANT SELECTED");
        label(detailMood, "--");
        label(detailIeee, "--");
        label(detailSoil, "--%");
        label(detailTemp, useFahrenheit ? "--.- F" : "--.- C");
        label(detailHumidity, "--%");
        label(detailBattery, "--%");
        label(detailSignal, "LQI --");
        label(detailUpdated, "No sensor data yet");
        lv_bar_set_value(detailBar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(renameButton, LV_STATE_DISABLED);
        lv_obj_add_state(replaceButton, LV_STATE_DISABLED);
        lv_obj_add_state(removeButton, LV_STATE_DISABLED);
      } else {
        PlantSensor &s = sensors[selectedSensor];
        snprintf(text, sizeof(text), "PLANT %u",
                 static_cast<unsigned>(selectedSensor + 1));
        label(detailSlot, text);
        label(detailName, s.name);
        label(detailMood, mood(s));

        char ieee[24]{};
        plantlink::formatIeee(s.ieee, ieee, sizeof(ieee));
        if (s.seenThisBoot)
          snprintf(text, sizeof(text), "%s   short 0x%04X", ieee, s.shortAddress);
        else
          snprintf(text, sizeof(text), "%s   waiting for check-in", ieee);
        label(detailIeee, text);

        formatSoil(s, text, sizeof(text));
        label(detailSoil, text);
        lv_bar_set_value(detailBar,
                         hasFreshMoisture(s) ? s.soilMoisturePct : 0,
                         LV_ANIM_OFF);
        formatTemp(s, text, sizeof(text));
        label(detailTemp, text);
        formatHumidity(s, text, sizeof(text));
        label(detailHumidity, text);

        if (s.seenThisBoot &&
            (s.fieldFlags & plantlink::SensorHasBattery))
          snprintf(text, sizeof(text), "%u%%", s.batteryPct);
        else
          snprintf(text, sizeof(text), "--%%");
        label(detailBattery, text);

        if (s.seenThisBoot)
          snprintf(text, sizeof(text), "LQI %u", s.lqi);
        else
          snprintf(text, sizeof(text), "LQI --");
        label(detailSignal, text);

        char updatedText[64]{};
        if (!s.seenThisBoot || !s.lastSeenMs) {
          snprintf(updatedText, sizeof(updatedText), "Waiting for this plant to check in");
        } else {
          const uint32_t age = (millis() - s.lastSeenMs) / 1000u;
          if (age < 2)
            snprintf(updatedText, sizeof(updatedText), "Updated now");
          else if (age < 60)
            snprintf(updatedText, sizeof(updatedText), "Updated %lus ago",
                     static_cast<unsigned long>(age));
          else
            snprintf(updatedText, sizeof(updatedText), "Updated %lum ago",
                     static_cast<unsigned long>(age / 60u));
        }

        char routeText[64]{};
        sensor_route_view::format(s.route, infrastructure, millis(),
                                  h2Online && networkReady, routeText,
                                  sizeof(routeText));
        if (routeText[0])
          snprintf(text, sizeof(text), "%s  |  %s", updatedText, routeText);
        else
          snprintf(text, sizeof(text), "%s", updatedText);
        label(detailUpdated, text);

        if (s.seenThisBoot &&
            (s.fieldFlags & plantlink::SensorHasWaterWarning) &&
            s.waterWarning)
          lv_obj_clear_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
        else
          lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(renameButton, LV_STATE_DISABLED);
        lv_obj_clear_state(replaceButton, LV_STATE_DISABLED);
        lv_obj_clear_state(removeButton, LV_STATE_DISABLED);
      }
      dirty.plant = false;
      break;
    }

    case Page::Settings: {
      if (!force && !dirty.settings) break;
      label(settingsDeviceName, deviceName);
      label(settingsH2, h2Online ? "ONLINE" : "OFFLINE");
      if (networkReady)
        snprintf(text, sizeof(text), "READY CH %u | P %u | R %u", zigbeeChannel,
                 h2SensorCount, h2InfrastructureCount);
      else if (h2Online)
        snprintf(text, sizeof(text), "STARTING");
      else
        snprintf(text, sizeof(text), "H2 OFFLINE");
      label(settingsZigbee, text);
      snprintf(text, sizeof(text), "%u", static_cast<unsigned>(count));
      label(settingsPlants, text);
      label(settingsUnit, useFahrenheit ? "°F" : "°C");
      snprintf(text, sizeof(text), "%s  >", espplants_phrases::themeName(phraseTheme));
      label(settingsTheme, text);
      if (permitJoinRemaining)
        snprintf(text, sizeof(text), "PAIR %us", permitJoinRemaining);
      else
        snprintf(text, sizeof(text), "ADD SENSOR");
      label(settingsPair, text);
      dirty.settings = false;
      break;
    }

    case Page::Advanced: {
      if (!force && !dirty.advanced) break;
      const size_t repeaterCount = infrastructureCount();
      const size_t repeaterOnline = onlineInfrastructureCount();
      snprintf(text, sizeof(text), "%u REPEATERS | %u ONLINE",
               static_cast<unsigned>(repeaterCount),
               static_cast<unsigned>(repeaterOnline));
      label(advancedSummary, text);

      refreshAdvancedVirtualList(true);

      const bool validInfrastructure =
          selectedInfrastructure >= 0 &&
          selectedInfrastructure < static_cast<int>(kMaxInfrastructure) &&
          infrastructure[selectedInfrastructure].used;

      if (validInfrastructure) {
        InfrastructureNode &node = infrastructure[selectedInfrastructure];
        char ieee[24]{};
        plantlink::formatIeee(node.ieee, ieee, sizeof(ieee));
        if (node.online)
          snprintf(text, sizeof(text), "%s  |  %s  |  short 0x%04X", node.name,
                   ieee, node.shortAddress);
        else
          snprintf(text, sizeof(text), "%s  |  %s  |  offline", node.name, ieee);
        label(advancedDetail, text);
        lv_obj_clear_state(advancedRenameButton, LV_STATE_DISABLED);
        lv_obj_clear_state(advancedRemoveButton, LV_STATE_DISABLED);
      } else {
        label(advancedDetail, repeaterCount ? "Select a repeater to manage it."
                                           : "No repeaters paired yet.");
        lv_obj_add_state(advancedRenameButton, LV_STATE_DISABLED);
        lv_obj_add_state(advancedRemoveButton, LV_STATE_DISABLED);
      }
      dirty.advanced = false;
      break;
    }
  }

  if (updateModalOpen && (force || dirty.update || intervalElapsed)) {
    if (espplants_update::wifiConnected()) {
      label(updateWifiState, "CONNECTED");
    } else if (espplants_update::wifiConfigured() &&
               espplants_update::wifiReconnectSuppressed()) {
      label(updateWifiState, "DISCONNECTED");
    } else if (espplants_update::wifiConfigured()) {
      label(updateWifiState, "OFFLINE / CONNECTING");
    } else {
      label(updateWifiState, "NOT CONFIGURED");
    }
    snprintf(text, sizeof(text), "SSID  %s\nIP       %s",
             espplants_update::wifiSsid(), espplants_update::wifiAddress());
    label(updateWifiDetail, text);

    const bool wifiBusy =
        espplants_update::checking() || espplants_update::installing();
    const bool setupActive = espplants_update::setupPortalActive();
    label(updateSetupLabel, setupActive ? "CANCEL SETUP" : "SET UP / CHANGE WI-FI");
    if (wifiBusy)
      lv_obj_add_state(updateSetupButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(updateSetupButton, LV_STATE_DISABLED);
    if (espplants_update::wifiReconnectSuppressed())
      label(updateDisconnectLabel, "RECONNECT WI-FI");
    else
      label(updateDisconnectLabel, "DISCONNECT WI-FI");
    if ((!espplants_update::wifiConnected() &&
         !espplants_update::wifiReconnectSuppressed()) ||
        !espplants_update::wifiConfigured() || wifiBusy)
      lv_obj_add_state(updateDisconnectButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(updateDisconnectButton, LV_STATE_DISABLED);
    if (!espplants_update::wifiConfigured() || wifiBusy)
      lv_obj_add_state(updateForgetButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(updateForgetButton, LV_STATE_DISABLED);

    if (espplants_update::setupPortalActive()) {
      char qr[sizeof(updateQrPayload)]{};
      snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;",
               espplants_update::setupSsid(), espplants_update::setupPassword());
      if (strcmp(updateQrPayload, qr) != 0) {
        if (renderSetupQr(qr)) {
          strncpy(updateQrPayload, qr, sizeof(updateQrPayload) - 1);
          updateQrPayload[sizeof(updateQrPayload) - 1] = '\0';
          label(updateQrHint, "SCAN QR");
          lv_obj_set_style_text_color(updateQrHint, lv_color_hex(0xA5C3AD), 0);
        } else {
          updateQrPayload[0] = '\0';
          label(updateQrHint, "QR ERROR - JOIN MANUALLY");
          lv_obj_set_style_text_color(updateQrHint, lv_color_hex(0xE2B276), 0);
        }
      }
      lv_obj_clear_flag(updateQrHint, LV_OBJ_FLAG_HIDDEN);
      if (updateQrPayload[0])
        lv_obj_clear_flag(updateQrCard, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(updateQrCard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(updatePortalInfo, 150, 268);
      lv_obj_set_width(updatePortalInfo, 196);
      snprintf(text, sizeof(text),
               "PHONE SETUP READY\nSSID: %s\nPassword: %s\n\nScan QR. If the captive page does not open, use 192.168.4.1",
               espplants_update::setupSsid(), espplants_update::setupPassword());
      label(updatePortalInfo, text);
    } else {
      updateQrPayload[0] = '\0';
      lv_obj_add_flag(updateQrHint, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(updateQrCard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(updatePortalInfo, 20, 260);
      lv_obj_set_width(updatePortalInfo, 326);
      label(updatePortalInfo,
            "Tap SET UP / CHANGE WI-FI. Scan the QR when it appears.");
    }

    snprintf(text, sizeof(text), "v%s", espplants_update::currentVersion());
    label(updateCurrentVersion, text);

    if (!h2Online) {
      label(updateH2Version, "H2 GATEWAY: unavailable");
    } else if (strncmp(h2BuildId, "ESPPLANTS-H2-", 13) == 0 && h2BuildId[13]) {
      snprintf(text, sizeof(text), "H2 GATEWAY: v%s", h2BuildId + 13);
      label(updateH2Version, text);
    } else if (h2BuildId[0]) {
      snprintf(text, sizeof(text), "H2 GATEWAY: %s", h2BuildId);
      label(updateH2Version, text);
    } else {
      label(updateH2Version, "H2 GATEWAY: reading version...");
    }

    if (strcmp(espplants_update::latestVersion(), "--") == 0) {
      label(updateLatestVersion, "--");
    } else {
      snprintf(text, sizeof(text), "v%s", espplants_update::latestVersion());
      label(updateLatestVersion, text);
    }
    label(updateStatus, espplants_update::statusText());
    const bool actionableUpdate = espplants_update::updateAvailable();
    const bool h2OnlyVisual = actionableUpdate &&
        strstr(espplants_update::statusText(), "H2 update available") != nullptr;
    lv_obj_set_style_text_color(updateStatus,
        actionableUpdate ? lv_color_hex(0xF2C66D) : lv_color_hex(0xA5C3AD), 0);
    lv_obj_set_style_text_color(updateLatestVersion,
        actionableUpdate && !h2OnlyVisual ? lv_color_hex(0xF2C66D) : lv_color_hex(0xE5ECE7), 0);
    lv_obj_set_style_text_color(updateH2Version,
        h2OnlyVisual ? lv_color_hex(0xF2C66D) : lv_color_hex(0x9DB5A5), 0);

    const bool updateBusy =
        espplants_update::checking() || espplants_update::installing();
    if (espplants_update::checking())
      label(updateCheckLabel, "CHECKING...");
    else if (actionableUpdate && espplants_update::releaseNotesAvailable())
      label(updateCheckLabel, "RELEASE NOTES");
    else
      label(updateCheckLabel, "CHECK NOW");
    if (!espplants_update::wifiConnected() || updateBusy)
      lv_obj_add_state(updateCheckButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(updateCheckButton, LV_STATE_DISABLED);

    if (espplants_update::installing()) {
      snprintf(text, sizeof(text), "INSTALLING... %d%%",
               espplants_update::updateProgress());
      label(updateInstallLabel, text);
    } else if (espplants_update::updateAvailable()) {
      label(updateInstallLabel, "INSTALL UPDATE");
    } else {
      label(updateInstallLabel, "NO UPDATE READY");
    }
    if (!espplants_update::wifiConnected() ||
        !espplants_update::updateAvailable() || updateBusy)
      lv_obj_add_state(updateInstallButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(updateInstallButton, LV_STATE_DISABLED);

    dirty.update = false;
  }

  if (pairDialogState != PairDialogState::Hidden &&
      (force || dirty.pair || intervalElapsed)) {
    refreshPairDialog();
    dirty.pair = false;
  }

  if (lockedHere) lvgl_port_unlock();
#if !defined(ESP_PLANTS_DISTRIBUTION_BUILD)
  recordUiRefreshRuntime(refreshStartedUs, lockWaitUs, force,
                         timedPageWork || timedModalWork);
#endif
}

void handleNetworkStatus(const plantlink::Frame &frame) {
  if (frame.payloadLength < 4) return;

  const bool previousNetworkReady = networkReady;
  const uint8_t previousChannel = zigbeeChannel;
  const uint8_t previousSensorCount = h2SensorCount;
  const uint8_t previousInfrastructureCount = h2InfrastructureCount;
  const uint8_t previousPermitJoin = permitJoinRemaining;

  networkReady = frame.payload[0] != 0;
  zigbeeChannel = frame.payload[1];
  h2SensorCount = frame.payload[2];
  const uint8_t reportedPermitJoin = frame.payload[3];
  const bool joinGuardActive =
      permitJoinGuardUntilMs != 0 &&
      static_cast<int32_t>(permitJoinGuardUntilMs - millis()) > 0;
  if (reportedPermitJoin > 0) {
    permitJoinRemaining = reportedPermitJoin;
    permitJoinGuardUntilMs = 0;
  } else if (!joinGuardActive || permitJoinRemaining == 0) {
    permitJoinRemaining = 0;
    permitJoinGuardUntilMs = 0;
  } else {
    Serial.println("[plantlink] ignored stale permit-join=0 while awaiting H2 acknowledgement");
  }
  h2InfrastructureCount = frame.payloadLength >= 5 ? frame.payload[4] : 0;

  if (networkReady != previousNetworkReady) {
    dirty.plant = true;
    dirty.pair = true;
  }
  if (networkReady != previousNetworkReady ||
      zigbeeChannel != previousChannel ||
      h2SensorCount != previousSensorCount ||
      h2InfrastructureCount != previousInfrastructureCount ||
      permitJoinRemaining != previousPermitJoin) {
    dirty.settings = true;
  }
  if (permitJoinRemaining != previousPermitJoin) dirty.pair = true;
}

PlantSensor *acceptPairingSensor(const uint8_t ieee[8], uint16_t shortAddress,
                                  size_t *slotOut) {
  if (pairDialogState != PairDialogState::Pairing || !ieee || ieeeZero(ieee)) return nullptr;

  size_t existingSlot = 0;
  if (findSensor(ieee, &existingSlot)) return nullptr;

  PlantSensor *s = nullptr;
  size_t slot = 0;

  if (pairReplacing &&
      pairTargetSlot >= 0 &&
      pairTargetSlot < static_cast<int>(kMaxSensors) &&
      sensors[pairTargetSlot].used) {
    slot = static_cast<size_t>(pairTargetSlot);
    uint8_t oldIeee[8]{};
    memcpy(oldIeee, sensors[slot].ieee, sizeof(oldIeee));

    s = replacePlantIdentity(slot, ieee, shortAddress);
    if (s) requestRemoveDevice(oldIeee);
  } else {
    s = findOrCreateSensor(ieee, shortAddress, &slot);
  }

  if (!s) return nullptr;

  requestJoin(0);
  selectedSensor = static_cast<int>(slot);
  pairFoundSlot = static_cast<int>(slot);
  pairDialogState = PairDialogState::Found;
  dirty.pair = true;
  dirty.plant = true;
  if (slotOut) *slotOut = slot;
  return s;
}

void handleInfrastructureReport(const plantlink::Frame &frame) {
  plantlink::InfrastructureReportData report;
  if (!plantlink::parseInfrastructureReport(frame.payload, frame.payloadLength, report)) return;

  size_t slot = 0;
  bool created = false;
  InfrastructureNode *node = findOrCreateInfrastructure(report, &slot, &created);
  if (!node) {
    Serial.println("[zigbee] repeater registry full; infrastructure report ignored");
    return;
  }

  if (created) {
    char ieee[24]{};
    plantlink::formatIeee(report.ieee, ieee, sizeof(ieee));
    Serial.printf("[zigbee] repeater discovered slot=%u ieee=%s short=0x%04X\n",
                  static_cast<unsigned>(slot + 1), ieee, report.shortAddress);
  }

  if (pairInfrastructure &&
      pairDialogState == PairDialogState::Pairing &&
      created) {
    requestJoin(0);
    selectedInfrastructure = static_cast<int>(slot);
    pairFoundInfrastructure = static_cast<int>(slot);
    pairDialogState = PairDialogState::Found;
  }

  markInfrastructureDirty();
  if (pairDialogState != PairDialogState::Hidden) dirty.pair = true;
}

void handleDeviceJoined(const plantlink::Frame &frame) {
  if (frame.payloadLength < 10) return;

  const uint16_t shortAddress = plantlink::getU16LE(frame.payload + 8);
  size_t slot = 0;
  PlantSensor *s = findSensor(frame.payload, &slot);

  if (!s) {
    s = acceptPairingSensor(frame.payload, shortAddress, &slot);
  }

  char ieee[24]{};
  plantlink::formatIeee(frame.payload, ieee, sizeof(ieee));

  if (!s) {
    Serial.printf("[zigbee] ignored unregistered device: %s short=0x%04X\n",
                  ieee, shortAddress);
    return;
  }

  s->shortAddress = shortAddress;
  Serial.printf("[zigbee] device seen: %s short=0x%04X slot=%u\n", ieee,
                shortAddress, static_cast<unsigned>(slot + 1));
  dirty.plant = true;
}

void handleDeviceLeft(const plantlink::Frame &frame) {
  if (frame.payloadLength < 8) return;

  size_t slot = 0;
  PlantSensor *s = findSensor(frame.payload, &slot);
  char ieee[24]{};
  plantlink::formatIeee(frame.payload, ieee, sizeof(ieee));

  if (!s) {
    size_t infrastructureSlot = 0;
    InfrastructureNode *node = findInfrastructure(frame.payload, &infrastructureSlot);
    if (node) {
      node->online = false;
      node->shortAddress = 0xffff;
      Serial.printf("[zigbee] repeater left: %s slot=%u now offline\n",
                    ieee, static_cast<unsigned>(infrastructureSlot + 1));
      markInfrastructureDirty();
      return;
    }

    Serial.printf("[zigbee] unregistered device left: %s\n", ieee);
    return;
  }

  s->seenThisBoot = false;
  s->route = {};
  s->lastSeenMs = 0;
  s->shortAddress = 0xffff;
  Serial.printf("[zigbee] registered sensor left: %s slot=%u now waiting\n",
                ieee, static_cast<unsigned>(slot + 1));
  markSensorValuesDirty(true);
}

void handleSensorReport(const plantlink::Frame &frame) {
  plantlink::SensorReportData report;
  if (!plantlink::parseSensorReport(frame.payload, frame.payloadLength, report)) return;

  size_t slot = 0;
  PlantSensor *s = findSensor(report.ieee, &slot);
  if (frame.flags & plantlink::FlagRouteOnly) {
    if (s && report.fieldFlags == 0 && s->route.update(report)) {
      dirty.plant = true;
    }
    return;
  }
  if (!s) s = acceptPairingSensor(report.ieee, report.shortAddress, &slot);

  if (!s) {
    char ieee[24]{};
    plantlink::formatIeee(report.ieee, ieee, sizeof(ieee));
    Serial.printf("[sensor] ignored report from unregistered ieee=%s\n", ieee);
    return;
  }

  const bool wasSeenThisBoot = s->seenThisBoot;
  const bool hadFreshMoisture = hasFreshMoisture(*s);
  const uint8_t previousSoilMoisture = s->soilMoisturePct;

  s->seenThisBoot = true;
  s->route.update(report);
  s->shortAddress = report.shortAddress;

  // ZG-303Z reports may contain only a subset of measurements.
  const bool moistureReported=(report.fieldFlags & plantlink::SensorHasSoilMoisture)!=0;
  s->fieldFlags |= report.fieldFlags;
  s->reportedFieldFlagsThisBoot |= report.fieldFlags;
  if (report.fieldFlags & plantlink::SensorHasTemperature) s->temperatureCentiC=report.temperatureCentiC;
  if (report.fieldFlags & plantlink::SensorHasHumidity) s->humidityCentiPct=report.humidityCentiPct;
  if (moistureReported) {
    s->soilMoisturePct=report.soilMoisturePct;
    // ONLY a soil-moisture report advances/selects a phrase.
    // Temperature, humidity, battery, LQI/signal, etc. never touch it.
    const bool warning=(s->reportedFieldFlagsThisBoot & plantlink::SensorHasWaterWarning) && s->waterWarning;
    const auto state=espplants_phrases::stateFor(report.soilMoisturePct,warning);
    const size_t phraseSlot=static_cast<size_t>(s-sensors);
    const uint32_t seed=static_cast<uint32_t>(phraseSlot*2654435761u)^
                        static_cast<uint32_t>(report.soilMoisturePct*257u)^millis();
    (void)espplants_phrases::select(s->phraseRotation,phraseTheme,state,seed,true);
  }
  if (report.fieldFlags & plantlink::SensorHasBattery) s->batteryPct=report.batteryPct;
  if (report.fieldFlags & plantlink::SensorHasWaterWarning) s->waterWarning=report.waterWarning;
  s->lqi = report.lqi;
  s->rssi = report.rssiDbm;
  s->lastSeenMs = millis();

  const bool orderMayChange =
      !wasSeenThisBoot ||
      (moistureReported &&
       (!hadFreshMoisture || previousSoilMoisture != report.soilMoisturePct));
  markSensorValuesDirty(orderMayChange);

  char ieee[24]{}; plantlink::formatIeee(report.ieee, ieee, sizeof(ieee));
  Serial.printf("[sensor] slot=%u name=\"%s\" %s", static_cast<unsigned>(slot + 1), s->name, ieee);
  if (report.fieldFlags & plantlink::SensorHasSoilMoisture) Serial.printf(" soil=%u%%", report.soilMoisturePct);
  if (report.fieldFlags & plantlink::SensorHasTemperature) {
    const float c = report.temperatureCentiC / 100.0f;
    Serial.printf(" temp=%.1fC/%.1fF", c, c * 9.0f / 5.0f + 32.0f);
  }
  if (report.fieldFlags & plantlink::SensorHasHumidity) Serial.printf(" rh=%.1f%%", report.humidityCentiPct / 100.0f);
  if (report.fieldFlags & plantlink::SensorHasBattery) Serial.printf(" batt=%u%%", report.batteryPct);
  if (report.fieldFlags & plantlink::SensorHasWaterWarning) Serial.printf(" water_warning=%s", report.waterWarning ? "ON" : "OFF");
  Serial.printf(" lqi=%u", report.lqi);
  if (report.rssiDbm == plantlink::kRssiUnavailableDbm) Serial.print(" rssi=n/a"); else Serial.printf(" rssi=%d", report.rssiDbm);
  Serial.println();
}

void clearRoutes() {
  for (auto &sensor : sensors) sensor.route = {};
  dirty.plant = true;
}

void handleFrame(const plantlink::Frame &frame) {
  lastH2RxMs = millis();
  if (!h2Online) {
    h2Online = true;
    markNetworkStateDirty();
  }
  switch (frame.type) {
    case plantlink::MessageType::Heartbeat:
      if (frame.payloadLength == 8) {
        const uint32_t uptime = plantlink::getU32LE(frame.payload);
        if (haveH2Uptime && uptime < lastH2Uptime) clearRoutes();
        lastH2Uptime = uptime;
        haveH2Uptime = true;
      }
      break;
    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1 ? frame.payloadLength : sizeof(build) - 1;
      memcpy(build, frame.payload, n);
      strncpy(h2BuildId, build, sizeof(h2BuildId) - 1);
      h2BuildId[sizeof(h2BuildId) - 1] = '\0';
      Serial.printf("[plantlink] H2 hello: %s\n", build);
      dirty.update = true;
      break;
    }
    case plantlink::MessageType::NetworkStatus: handleNetworkStatus(frame); break;
    case plantlink::MessageType::DeviceJoined: handleDeviceJoined(frame); break;
    case plantlink::MessageType::DeviceLeft: handleDeviceLeft(frame); break;
    case plantlink::MessageType::InfrastructureReport: handleInfrastructureReport(frame); break;
    case plantlink::MessageType::SensorReport: handleSensorReport(frame); break;
    default: break;
  }
}

void servicePlantLink() {
  plantlink::Frame frame;
  while (Serial0.available()) if (decoder.feed(static_cast<uint8_t>(Serial0.read()), frame)) handleFrame(frame);
  const uint32_t now = millis();
  if ((!h2Online || !h2BuildId[0]) && now - lastHelloMs >= kHelloIntervalMs) {
    lastHelloMs = now;
    sendHello();
  }
  if (h2Online && now - lastH2RxMs > kLinkTimeoutMs) {
    h2Online = false;
    networkReady = false;
    permitJoinRemaining = 0;
    markNetworkStateDirty();
    clearRoutes();
    haveH2Uptime = false;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t usbWaitStart = millis();
  while (!Serial && millis() - usbWaitStart < 1500u) delay(10);
  Serial.println();
  Serial.println("ESP PLANTS Waveshare multi-page dashboard");
  Serial.println("[usb] native USB CDC console online");

  migrateUserPreferences();
  if (!preferences.begin("espplants", false, "plantdata")) {
    Serial.println("[storage] ERROR: plantdata unavailable; falling back to default NVS");
    preferences.begin("espplants", false);
  }
  useFahrenheit = preferences.getBool("fahrenheit", true);
  {
    const uint8_t savedTheme=preferences.getUChar(
        "phrase_theme", static_cast<uint8_t>(espplants_phrases::Theme::MIXED));
    phraseTheme=static_cast<espplants_phrases::Theme>(
        savedTheme<=static_cast<uint8_t>(espplants_phrases::Theme::MIXED)
            ? savedTheme : static_cast<uint8_t>(espplants_phrases::Theme::MIXED));
  }
  String savedDeviceName = preferences.getString("device_name", "ESP PLANTS");
  savedDeviceName.toCharArray(deviceName, sizeof(deviceName));
  Serial.printf("[settings] temperature units=%s\n", useFahrenheit ? "F" : "C");
  Serial.printf("[settings] device name=\"%s\"\n", deviceName);
  loadRegistry();
  loadInfrastructureRegistry();

  Serial0.begin(kPlantLinkBaud, SERIAL_8N1, kPlantLinkRxPin, kPlantLinkTxPin);
  Serial.printf("[plantlink] UART0 RX=%d TX=%d baud=%lu\n", kPlantLinkRxPin, kPlantLinkTxPin,
                static_cast<unsigned long>(kPlantLinkBaud));

  Serial.println("[display] initializing Waveshare 800x480...");
  logEspMemory("display-pre-init");
  lcd_init();
  logEspMemory("display-post-init");
  if (lvgl_port_lock(-1)) {
    logDisplayRuntimeConfig();
    logLvglMemory("lvgl-init-pre-ui");
    buildUi();
    lvgl_port_unlock();
  }
  Serial.println("[display] ready");

  espplants_update::begin();
  Serial.println("[plantlink] waiting for H2 frames");
}

void loop() {
  servicePlantLink();
  static bool lastActionableUpdate = false;
  espplants_update::service();
  const bool actionableUpdate = espplants_update::updateAvailable();
  if (actionableUpdate != lastActionableUpdate) {
    lastActionableUpdate = actionableUpdate;
    dirty.header = true;
    dirty.update = true;
  }
  refreshUi();
  serviceRuntimeTelemetry();
  delay(2);
}
