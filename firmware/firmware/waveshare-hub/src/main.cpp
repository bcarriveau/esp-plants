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
  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, 800, 66);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x0C2518), 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  headerTitle = lv_label_create(header);
  lv_label_set_text(headerTitle, deviceName);
  lv_obj_set_style_text_font(headerTitle, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(headerTitle, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(headerTitle, 22, 9);
  lv_obj_set_width(headerTitle, 500);
  lv_label_set_long_mode(headerTitle, LV_LABEL_LONG_DOT);

  lv_obj_t *tag = lv_label_create(header);
  lv_label_set_text(tag, "keep 'em alive");
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tag, lv_color_hex(0x9DB5A5), 0);
  lv_obj_set_pos(tag, 24, 43);

  headerUpdateButton = lv_btn_create(header);
  lv_obj_set_size(headerUpdateButton, 104, 34);
  lv_obj_set_pos(headerUpdateButton, 548, 16);
  lv_obj_set_style_radius(headerUpdateButton, 10, 0);
  lv_obj_set_style_bg_color(headerUpdateButton, lv_color_hex(0xB86E2F), 0);
  lv_obj_set_style_shadow_width(headerUpdateButton, 0, 0);
  lv_obj_add_event_cb(headerUpdateButton, openUpdateEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *updateLabel = lv_label_create(headerUpdateButton);
  lv_label_set_text(updateLabel, "UPDATE");
  lv_obj_set_style_text_font(updateLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updateLabel, lv_color_hex(0xFFF4E6), 0);
  lv_obj_center(updateLabel);
  lv_obj_add_flag(headerUpdateButton, LV_OBJ_FLAG_HIDDEN);

  headerCount = lv_label_create(header);
  lv_label_set_text(headerCount, "0 PLANTS");
  lv_obj_set_style_text_font(headerCount, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(headerCount, lv_color_hex(0xD1DED5), 0);
  lv_obj_align(headerCount, LV_ALIGN_RIGHT_MID, -22, 0);
}

void buildHome(lv_obj_t *screen) {
  homePage = lv_obj_create(screen);
  lv_obj_set_pos(homePage, 0, 66);
  lv_obj_set_size(homePage, 800, 356);
  lv_obj_set_style_border_width(homePage, 0, 0);
  lv_obj_set_style_bg_opa(homePage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(homePage, 0, 0);
  lv_obj_clear_flag(homePage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *featured = card(homePage, 14, 10, 500, 334);
  lv_obj_add_event_cb(featured, featuredEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *section = lv_label_create(featured);
  lv_label_set_text(section, "WHO NEEDS WATER?");
  lv_obj_set_style_text_font(section, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(section, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(section, 22, 14);

  homeSummary = lv_label_create(featured);
  lv_label_set_text(homeSummary, "0 REPORTING | 0 WAITING");
  lv_obj_set_style_text_font(homeSummary, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(homeSummary, lv_color_hex(0xAABBAF), 0);
  lv_obj_align(homeSummary, LV_ALIGN_TOP_RIGHT, -18, 14);

  homeName = lv_label_create(featured);
  lv_label_set_text(homeName, "WAITING FOR SENSOR");
  lv_obj_set_style_text_font(homeName, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(homeName, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(homeName, 22, 38);
  lv_obj_set_width(homeName, 440);
  lv_label_set_long_mode(homeName, LV_LABEL_LONG_DOT);

  homeMood = lv_label_create(featured);
  lv_label_set_text(homeMood, "Pair a sensor and I'll keep an eye on it");
  lv_obj_set_style_text_font(homeMood, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(homeMood, lv_color_hex(0xCBE6D2), 0);
  lv_obj_set_pos(homeMood, 24, 82);
  lv_obj_set_width(homeMood, 445);
  lv_label_set_long_mode(homeMood, LV_LABEL_LONG_WRAP);

  metric(featured, "SOIL", 24, 126, &homeSoil, &lv_font_montserrat_32);
  metric(featured, "TEMP", 180, 126, &homeTemp);
  metric(featured, "AIR RH", 334, 126, &homeHumidity);

  homeBar = lv_bar_create(featured);
  lv_obj_set_pos(homeBar, 24, 201);
  lv_obj_set_size(homeBar, 448, 22);
  lv_bar_set_range(homeBar, 0, 100);
  lv_obj_set_style_bg_color(homeBar, lv_color_hex(0x2A352E), LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeBar, lv_color_hex(0x5E9B68), LV_PART_INDICATOR);

  lv_obj_t *hint = lv_label_create(featured);
  lv_label_set_text(hint, "Tap card for full plant details");
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0xAABBAF), 0);
  lv_obj_set_pos(hint, 24, 248);

  homeWarning = lv_label_create(featured);
  lv_label_set_text(homeWarning, "WATER ME!");
  lv_obj_set_style_text_font(homeWarning, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(homeWarning, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_style_bg_color(homeWarning, lv_color_hex(0x8E493E), 0);
  lv_obj_set_style_pad_hor(homeWarning, 14, 0);
  lv_obj_set_style_pad_ver(homeWarning, 8, 0);
  lv_obj_set_style_radius(homeWarning, 10, 0);
  lv_obj_set_pos(homeWarning, 24, 278);
  lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *listCard = card(homePage, 528, 10, 258, 334);
  lv_obj_t *listTitle = lv_label_create(listCard);
  lv_label_set_text(listTitle, "YOUR PLANTS");
  lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(listTitle, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(listTitle, 10, 8);

  homeList = lv_obj_create(listCard);
  lv_obj_set_pos(homeList, 0, 42);
  lv_obj_set_size(homeList, 234, espplants_home_virtual_list::kViewportHeight);
  lv_obj_set_style_border_width(homeList, 0, 0);
  lv_obj_set_style_bg_opa(homeList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(homeList, 0, 0);
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
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(0x5E9B68), LV_PART_INDICATOR);
  }
}

void buildAll(lv_obj_t *screen) {
  allPage = lv_obj_create(screen);
  lv_obj_set_pos(allPage, 0, 66);
  lv_obj_set_size(allPage, 800, 356);
  lv_obj_set_style_border_width(allPage, 0, 0);
  lv_obj_set_style_bg_opa(allPage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(allPage, 0, 0);
  lv_obj_clear_flag(allPage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *p = card(allPage, 14, 10, 772, 334);

  lv_obj_t *title = lv_label_create(p);
  lv_label_set_text(title, "ALL SENSORS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 20, 14);

  allSummary = lv_label_create(p);
  lv_label_set_text(allSummary, "0 REPORTING | 0 WAITING");
  lv_obj_set_style_text_font(allSummary, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(allSummary, lv_color_hex(0xC1D0C6), 0);
  lv_obj_align(allSummary, LV_ALIGN_TOP_RIGHT, -20, 18);

  // Fixed column headers instead of a space-padded sentence. This keeps every
  // heading aligned with its values regardless of font metrics.
  lv_obj_t *headPlant = lv_label_create(p);
  lv_label_set_text(headPlant, "PLANT");
  lv_obj_set_style_text_font(headPlant, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(headPlant, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(headPlant, 20, 52);
  lv_obj_set_width(headPlant, 285);

  lv_obj_t *headSoil = lv_label_create(p);
  lv_label_set_text(headSoil, "SOIL");
  lv_obj_set_style_text_font(headSoil, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(headSoil, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(headSoil, 320, 52);
  lv_obj_set_width(headSoil, 105);
  lv_obj_set_style_text_align(headSoil, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *headBattery = lv_label_create(p);
  lv_label_set_text(headBattery, "BATTERY");
  lv_obj_set_style_text_font(headBattery, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(headBattery, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(headBattery, 445, 52);
  lv_obj_set_width(headBattery, 115);
  lv_obj_set_style_text_align(headBattery, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *headUpdated = lv_label_create(p);
  lv_label_set_text(headUpdated, "LAST REPORT");
  lv_obj_set_style_text_font(headUpdated, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(headUpdated, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(headUpdated, 580, 52);
  lv_obj_set_width(headUpdated, 150);
  lv_obj_set_style_text_align(headUpdated, LV_TEXT_ALIGN_CENTER, 0);

  allList = lv_obj_create(p);
  lv_obj_set_pos(allList, 10, 72);
  lv_obj_set_size(allList, 752, espplants_all_virtual_list::kViewportHeight);
  lv_obj_set_style_border_width(allList, 0, 0);
  lv_obj_set_style_bg_opa(allList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(allList, 0, 0);
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
  plantPage = lv_obj_create(screen);
  lv_obj_set_pos(plantPage, 0, 66);
  lv_obj_set_size(plantPage, 800, 356);
  lv_obj_set_style_border_width(plantPage, 0, 0);
  lv_obj_set_style_bg_opa(plantPage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(plantPage, 0, 0);
  lv_obj_clear_flag(plantPage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *p = card(plantPage, 14, 10, 772, 334);
  detailSlot = lv_label_create(p);
  lv_obj_set_style_text_font(detailSlot, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailSlot, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(detailSlot, 22, 14);

  detailName = lv_label_create(p);
  lv_obj_set_style_text_font(detailName, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(detailName, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(detailName, 22, 38);
  lv_obj_set_width(detailName, 345);
  lv_label_set_long_mode(detailName, LV_LABEL_LONG_DOT);

  detailMood = lv_label_create(p);
  lv_obj_set_style_text_font(detailMood, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(detailMood, lv_color_hex(0xCBE6D2), 0);
  lv_obj_set_pos(detailMood, 24, 80);

  detailIeee = lv_label_create(p);
  lv_obj_set_style_text_font(detailIeee, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailIeee, lv_color_hex(0xAABBAF), 0);
  lv_obj_set_pos(detailIeee, 24, 108);

  renameButton = lv_btn_create(p);
  lv_obj_set_size(renameButton, 110, 48);
  lv_obj_set_pos(renameButton, 390, 24);
  lv_obj_set_style_radius(renameButton, 12, 0);
  lv_obj_set_style_bg_color(renameButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(renameButton, renameEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *renameLabel = lv_label_create(renameButton);
  lv_label_set_text(renameLabel, "RENAME");
  lv_obj_set_style_text_font(renameLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(renameLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(renameLabel);

  replaceButton = lv_btn_create(p);
  lv_obj_set_size(replaceButton, 110, 48);
  lv_obj_set_pos(replaceButton, 510, 24);
  lv_obj_set_style_radius(replaceButton, 12, 0);
  lv_obj_set_style_bg_color(replaceButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(replaceButton, replaceEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *replaceLabel = lv_label_create(replaceButton);
  lv_label_set_text(replaceLabel, "REPLACE");
  lv_obj_set_style_text_font(replaceLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(replaceLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(replaceLabel);

  removeButton = lv_btn_create(p);
  lv_obj_set_size(removeButton, 110, 48);
  lv_obj_set_pos(removeButton, 630, 24);
  lv_obj_set_style_radius(removeButton, 12, 0);
  lv_obj_set_style_bg_color(removeButton, lv_color_hex(0x7A4037), 0);
  lv_obj_add_event_cb(removeButton, removeEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *removeLabel = lv_label_create(removeButton);
  lv_label_set_text(removeLabel, "REMOVE");
  lv_obj_set_style_text_font(removeLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(removeLabel, lv_color_hex(0xF7EDE9), 0);
  lv_obj_center(removeLabel);

  metric(p, "SOIL", 24, 150, &detailSoil, &lv_font_montserrat_32);
  metric(p, "TEMP", 182, 150, &detailTemp);
  metric(p, "AIR RH", 342, 150, &detailHumidity);
  metric(p, "BATTERY", 502, 150, &detailBattery);
  metric(p, "SIGNAL", 640, 150, &detailSignal, &lv_font_montserrat_24);

  detailBar = lv_bar_create(p);
  lv_obj_set_pos(detailBar, 24, 226);
  lv_obj_set_size(detailBar, 718, 22);
  lv_bar_set_range(detailBar, 0, 100);
  lv_obj_set_style_bg_color(detailBar, lv_color_hex(0x2A352E), LV_PART_MAIN);
  lv_obj_set_style_bg_color(detailBar, lv_color_hex(0x5E9B68), LV_PART_INDICATOR);

  detailUpdated = lv_label_create(p);
  lv_obj_set_style_text_font(detailUpdated, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailUpdated, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(detailUpdated, 24, 276);
  lv_obj_set_width(detailUpdated, 540);
  lv_label_set_long_mode(detailUpdated, LV_LABEL_LONG_CLIP);

  detailWarning = lv_label_create(p);
  lv_label_set_text(detailWarning, "WATER ME!");
  lv_obj_set_style_text_font(detailWarning, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(detailWarning, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_style_bg_color(detailWarning, lv_color_hex(0x8E493E), 0);
  lv_obj_set_style_pad_hor(detailWarning, 14, 0);
  lv_obj_set_style_pad_ver(detailWarning, 8, 0);
  lv_obj_set_style_radius(detailWarning, 10, 0);
  lv_obj_set_pos(detailWarning, 600, 272);
  lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
}

void buildSettings(lv_obj_t *screen) {
  settingsPage = lv_obj_create(screen);
  lv_obj_set_pos(settingsPage, 0, 66);
  lv_obj_set_size(settingsPage, 800, 356);
  lv_obj_set_style_border_width(settingsPage, 0, 0);
  lv_obj_set_style_bg_opa(settingsPage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(settingsPage, 0, 0);
  lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_SCROLLABLE);

  // SYSTEM: identity and status. The status values are grouped instead of
  // being stacked down the full height of the card.
  lv_obj_t *system = card(settingsPage, 14, 10, 380, 334);
  lv_obj_t *title = lv_label_create(system);
  lv_label_set_text(title, "SYSTEM");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 22, 18);

  lv_obj_t *cap = lv_label_create(system);
  lv_label_set_text(cap, "DEVICE NAME");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 22, 62);

  lv_obj_t *deviceButton = lv_btn_create(system);
  lv_obj_set_size(deviceButton, 330, 48);
  lv_obj_set_pos(deviceButton, 22, 82);
  lv_obj_set_style_radius(deviceButton, 12, 0);
  lv_obj_set_style_bg_color(deviceButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(deviceButton, deviceNameEvent, LV_EVENT_CLICKED, nullptr);
  settingsDeviceName = lv_label_create(deviceButton);
  lv_obj_set_style_text_font(settingsDeviceName, &lv_font_montserrat_16, 0);
  lv_obj_set_width(settingsDeviceName, 292);
  lv_label_set_long_mode(settingsDeviceName, LV_LABEL_LONG_DOT);
  lv_obj_center(settingsDeviceName);

  cap = lv_label_create(system);
  lv_label_set_text(cap, "H2 LINK");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 22, 151);

  settingsH2 = lv_label_create(system);
  lv_obj_set_pos(settingsH2, 22, 171);
  lv_obj_set_style_text_font(settingsH2, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(settingsH2, lv_color_hex(0xE5ECE7), 0);

  cap = lv_label_create(system);
  lv_label_set_text(cap, "REGISTERED");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 200, 151);

  settingsPlants = lv_label_create(system);
  lv_obj_set_pos(settingsPlants, 200, 171);
  lv_obj_set_style_text_font(settingsPlants, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(settingsPlants, lv_color_hex(0xE5ECE7), 0);

  cap = lv_label_create(system);
  lv_label_set_text(cap, "ZIGBEE NETWORK");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 22, 224);

  settingsZigbee = lv_label_create(system);
  lv_obj_set_pos(settingsZigbee, 22, 246);
  lv_obj_set_width(settingsZigbee, 330);
  lv_obj_set_style_text_font(settingsZigbee, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(settingsZigbee, lv_color_hex(0xE5ECE7), 0);
  lv_label_set_long_mode(settingsZigbee, LV_LABEL_LONG_DOT);

  lv_obj_t *legend = lv_label_create(system);
  lv_label_set_text(legend, "P = PLANTS     R = REPEATERS");
  lv_obj_set_style_text_font(legend, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(legend, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(legend, 22, 282);

  lv_obj_t *networkButton = lv_btn_create(system);
  lv_obj_set_size(networkButton, 162, 36);
  lv_obj_set_pos(networkButton, 190, 14);
  lv_obj_set_style_radius(networkButton, 10, 0);
  lv_obj_set_style_bg_color(networkButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(networkButton, openUpdateEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *networkLabel = lv_label_create(networkButton);
  lv_label_set_text(networkLabel, "NETWORK & UPDATES  >");
  lv_obj_set_style_text_font(networkLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(networkLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(networkLabel);

  // PLANT SETUP: three deliberate rows with breathing room.
  lv_obj_t *setup = card(settingsPage, 408, 10, 378, 334);
  title = lv_label_create(setup);
  lv_label_set_text(title, "PLANT SETUP");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 22, 18);

  cap = lv_label_create(setup);
  lv_label_set_text(cap, "PERSONALITY");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 22, 50);

  lv_obj_t *tempCap = lv_label_create(setup);
  lv_label_set_text(tempCap, "TEMP UNIT");
  lv_obj_set_style_text_font(tempCap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tempCap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(tempCap, 242, 50);

  lv_obj_t *unitButton = lv_btn_create(setup);
  lv_obj_set_size(unitButton, 92, 40);
  lv_obj_set_pos(unitButton, 242, 66);
  lv_obj_set_style_radius(unitButton, 12, 0);
  lv_obj_set_style_bg_color(unitButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(unitButton, unitEvent, LV_EVENT_CLICKED, nullptr);
  settingsUnit = lv_label_create(unitButton);
  lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_14, 0);
  lv_obj_center(settingsUnit);

  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 200, 40);
  lv_obj_set_pos(themeButton, 22, 66);
  lv_obj_set_style_radius(themeButton, 12, 0);
  lv_obj_set_style_bg_color(themeButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(themeButton, themeEvent, LV_EVENT_CLICKED, nullptr);
  settingsTheme = lv_label_create(themeButton);
  lv_obj_set_style_text_font(settingsTheme, &lv_font_montserrat_14, 0);
  lv_obj_set_width(settingsTheme, 180);
  lv_obj_set_style_text_align(settingsTheme, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(settingsTheme, LV_LABEL_LONG_DOT);
  lv_obj_center(settingsTheme);

  cap = lv_label_create(setup);
  lv_label_set_text(cap, "PLANT SENSORS");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(cap, 22, 124);

  lv_obj_t *sensorHint = lv_label_create(setup);
  lv_label_set_text(sensorHint, "Pair a HOBEIAN ZG-303Z plant sensor");
  lv_obj_set_style_text_font(sensorHint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sensorHint, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(sensorHint, 22, 148);

  lv_obj_t *pairButton = lv_btn_create(setup);
  lv_obj_set_size(pairButton, 334, 50);
  lv_obj_set_pos(pairButton, 22, 170);
  lv_obj_set_style_radius(pairButton, 12, 0);
  lv_obj_set_style_bg_color(pairButton, lv_color_hex(0x3F7A4E), 0);
  lv_obj_add_event_cb(pairButton, pairEvent, LV_EVENT_CLICKED, nullptr);
  settingsPair = lv_label_create(pairButton);
  lv_label_set_text(settingsPair, "ADD SENSOR");
  lv_obj_set_style_text_font(settingsPair, &lv_font_montserrat_16, 0);
  lv_obj_center(settingsPair);

  cap = lv_label_create(setup);
  lv_label_set_text(cap, "ZIGBEE NETWORK");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(cap, 22, 225);

  lv_obj_t *networkHint = lv_label_create(setup);
  lv_label_set_text(networkHint, "Repeaters, routers and mesh tools");
  lv_obj_set_style_text_font(networkHint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(networkHint, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(networkHint, 22, 247);

  lv_obj_t *advancedButton = lv_btn_create(setup);
  lv_obj_set_size(advancedButton, 334, 42);
  lv_obj_set_pos(advancedButton, 22, 266);
  lv_obj_set_style_radius(advancedButton, 12, 0);
  lv_obj_set_style_bg_color(advancedButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(advancedButton, advancedEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *advancedLabel = lv_label_create(advancedButton);
  lv_label_set_text(advancedLabel, "ADVANCED ZIGBEE  >");
  lv_obj_set_style_text_font(advancedLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(advancedLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(advancedLabel);
}

void buildAdvanced(lv_obj_t *screen) {
  advancedPage = lv_obj_create(screen);
  lv_obj_set_pos(advancedPage, 0, 66);
  lv_obj_set_size(advancedPage, 800, 356);
  lv_obj_set_style_border_width(advancedPage, 0, 0);
  lv_obj_set_style_bg_opa(advancedPage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(advancedPage, 0, 0);
  lv_obj_clear_flag(advancedPage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *p = card(advancedPage, 14, 10, 772, 334);
  // Advanced Zigbee uses explicit zero card padding so its measured child
  // geometry is deterministic inside the locked 772x334 content card.
  lv_obj_set_style_pad_all(p, 0, 0);

  lv_obj_t *title = lv_label_create(p);
  lv_label_set_text(title, "ZIGBEE NETWORK");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 18, 14);

  advancedSummary = lv_label_create(p);
  lv_label_set_text(advancedSummary, "0 REPEATERS | 0 ONLINE");
  lv_obj_set_style_text_font(advancedSummary, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(advancedSummary, lv_color_hex(0xC1D0C6), 0);
  lv_obj_set_pos(advancedSummary, 18, 48);

  lv_obj_t *back = lv_btn_create(p);
  lv_obj_set_size(back, 92, 42);
  lv_obj_set_pos(back, 530, 12);
  lv_obj_set_style_radius(back, 11, 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x233029), 0);
  lv_obj_add_event_cb(back, advancedBackEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *backLabel = lv_label_create(back);
  lv_label_set_text(backLabel, "BACK");
  lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(backLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(backLabel);

  advancedAddButton = lv_btn_create(p);
  lv_obj_set_size(advancedAddButton, 118, 42);
  lv_obj_set_pos(advancedAddButton, 632, 12);
  lv_obj_set_style_radius(advancedAddButton, 11, 0);
  lv_obj_set_style_bg_color(advancedAddButton, lv_color_hex(0x3F7A4E), 0);
  lv_obj_add_event_cb(advancedAddButton, infrastructureAddEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *addLabel = lv_label_create(advancedAddButton);
  lv_label_set_text(addLabel, "ADD REPEATER");
  lv_obj_set_style_text_font(addLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(addLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(addLabel);

  lv_obj_t *repeaterHead = lv_label_create(p);
  lv_label_set_text(repeaterHead, "REPEATER / ROUTER");
  lv_obj_set_style_text_font(repeaterHead, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(repeaterHead, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(repeaterHead, 20, 76);
  lv_obj_set_width(repeaterHead, 390);

  lv_obj_t *statusHead = lv_label_create(p);
  lv_label_set_text(statusHead, "STATUS");
  lv_obj_set_style_text_font(statusHead, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(statusHead, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(statusHead, 448, 76);
  lv_obj_set_width(statusHead, 110);

  lv_obj_t *signalHead = lv_label_create(p);
  lv_label_set_text(signalHead, "SIGNAL");
  lv_obj_set_style_text_font(signalHead, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(signalHead, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(signalHead, 588, 76);
  lv_obj_set_width(signalHead, 145);

  advancedList = lv_obj_create(p);
  lv_obj_set_pos(advancedList, 10, 96);
  lv_obj_set_size(advancedList, 752, 150);
  lv_obj_set_style_border_width(advancedList, 0, 0);
  lv_obj_set_style_bg_opa(advancedList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(advancedList, 0, 0);
  lv_obj_set_scroll_dir(advancedList, LV_DIR_VER);
  lv_obj_add_event_cb(advancedList, advancedListScrollEvent, LV_EVENT_SCROLL, nullptr);

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

  advancedDetail = lv_label_create(p);
  lv_label_set_text(advancedDetail, "Select a repeater to manage it.");
  lv_obj_set_style_text_font(advancedDetail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(advancedDetail, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(advancedDetail, 18, 263);
  lv_obj_set_width(advancedDetail, 440);
  lv_label_set_long_mode(advancedDetail, LV_LABEL_LONG_DOT);

  advancedRenameButton = lv_btn_create(p);
  lv_obj_set_size(advancedRenameButton, 126, 42);
  lv_obj_set_pos(advancedRenameButton, 478, 255);
  lv_obj_set_style_radius(advancedRenameButton, 11, 0);
  lv_obj_set_style_bg_color(advancedRenameButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(advancedRenameButton, infrastructureRenameEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *renameLabel = lv_label_create(advancedRenameButton);
  lv_label_set_text(renameLabel, "RENAME");
  lv_obj_set_style_text_font(renameLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(renameLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(renameLabel);

  advancedRemoveButton = lv_btn_create(p);
  lv_obj_set_size(advancedRemoveButton, 126, 42);
  lv_obj_set_pos(advancedRemoveButton, 616, 255);
  lv_obj_set_style_radius(advancedRemoveButton, 11, 0);
  lv_obj_set_style_bg_color(advancedRemoveButton, lv_color_hex(0x7A4037), 0);
  lv_obj_add_event_cb(advancedRemoveButton, infrastructureRemoveEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *removeLabel = lv_label_create(advancedRemoveButton);
  lv_label_set_text(removeLabel, "REMOVE");
  lv_obj_set_style_text_font(removeLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(removeLabel, lv_color_hex(0xF7EDE9), 0);
  lv_obj_center(removeLabel);

  lv_obj_add_state(advancedRenameButton, LV_STATE_DISABLED);
  lv_obj_add_state(advancedRemoveButton, LV_STATE_DISABLED);
}

void buildNav(lv_obj_t *screen) {
  lv_obj_t *bar = lv_obj_create(screen);
  lv_obj_set_pos(bar, 0, 422);
  lv_obj_set_size(bar, 800, 58);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x111A16), 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  auto add = [&](int x, const char *text, Page page, lv_obj_t **button) {
    *button = lv_btn_create(bar);
    lv_obj_set_size(*button, 184, 44);
    lv_obj_set_pos(*button, x, 7);
    lv_obj_set_style_radius(*button, 12, 0);
    lv_obj_set_style_shadow_width(*button, 0, 0);
    lv_obj_set_style_border_width(*button, 1, 0);
    lv_obj_set_style_border_color(*button, lv_color_hex(0x304138), 0);
    lv_obj_add_event_cb(*button, navEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(page)));
    lv_obj_t *l = lv_label_create(*button);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE5ECE7), 0);
    lv_obj_center(l);
  };
  add(14, "HOME", Page::Home, &navHome);
  add(210, "ALL SENSORS", Page::All, &navAll);
  add(406, "PLANT", Page::Plant, &navPlant);
  add(602, "SETTINGS", Page::Settings, &navSettings);
}
void buildUpdateDialog(lv_obj_t *screen) {
  updateModal = lv_obj_create(screen);
  lv_obj_set_pos(updateModal, 0, 0);
  lv_obj_set_size(updateModal, 800, 480);
  lv_obj_set_style_radius(updateModal, 0, 0);
  lv_obj_set_style_border_width(updateModal, 0, 0);
  lv_obj_set_style_bg_color(updateModal, lv_color_hex(0x101814), 0);
  lv_obj_set_style_pad_all(updateModal, 0, 0);
  lv_obj_clear_flag(updateModal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *top = lv_obj_create(updateModal);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_size(top, 800, 70);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_bg_color(top, lv_color_hex(0x0C2518), 0);
  lv_obj_set_style_pad_all(top, 0, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(top);
  lv_label_set_text(title, "NETWORK & UPDATES");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 22, 18);

  lv_obj_t *close = lv_btn_create(top);
  lv_obj_set_size(close, 94, 42);
  lv_obj_set_pos(close, 684, 14);
  lv_obj_set_style_radius(close, 11, 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(0x233029), 0);
  lv_obj_add_event_cb(close, closeUpdateEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, "CLOSE");
  lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(closeLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(closeLabel);

  // These two cards intentionally use zero padding so every child bound below
  // is measured directly against the 370x388 card rectangle.
  lv_obj_t *network = card(updateModal, 18, 82, 370, 388);
  lv_obj_set_style_pad_all(network, 0, 0);
  title = lv_label_create(network);
  lv_label_set_text(title, "WI-FI");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 20, 14);

  lv_obj_t *cap = lv_label_create(network);
  lv_label_set_text(cap, "STATUS");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 20, 54);

  updateWifiState = lv_label_create(network);
  lv_label_set_text(updateWifiState, "NOT CONFIGURED");
  lv_obj_set_style_text_font(updateWifiState, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(updateWifiState, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(updateWifiState, 20, 72);

  updateWifiDetail = lv_label_create(network);
  lv_label_set_text(updateWifiDetail, "SSID --\nIP --");
  lv_obj_set_style_text_font(updateWifiDetail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updateWifiDetail, lv_color_hex(0xC1D0C6), 0);
  lv_obj_set_pos(updateWifiDetail, 20, 106);
  lv_obj_set_width(updateWifiDetail, 326);
  lv_label_set_long_mode(updateWifiDetail, LV_LABEL_LONG_WRAP);

  updateSetupButton = lv_btn_create(network);
  lv_obj_set_size(updateSetupButton, 326, 42);
  lv_obj_set_pos(updateSetupButton, 20, 150);
  lv_obj_set_style_radius(updateSetupButton, 12, 0);
  lv_obj_set_style_bg_color(updateSetupButton, lv_color_hex(0x3F7A4E), 0);
  lv_obj_add_event_cb(updateSetupButton, wifiSetupEvent, LV_EVENT_CLICKED, nullptr);
  updateSetupLabel = lv_label_create(updateSetupButton);
  lv_label_set_text(updateSetupLabel, "SET UP / CHANGE WI-FI");
  lv_obj_set_style_text_font(updateSetupLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(updateSetupLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(updateSetupLabel);

  updateDisconnectButton = lv_btn_create(network);
  lv_obj_set_size(updateDisconnectButton, 158, 40);
  lv_obj_set_pos(updateDisconnectButton, 20, 202);
  lv_obj_set_style_radius(updateDisconnectButton, 11, 0);
  lv_obj_set_style_bg_color(updateDisconnectButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(updateDisconnectButton, wifiDisconnectEvent, LV_EVENT_CLICKED, nullptr);
  updateDisconnectLabel = lv_label_create(updateDisconnectButton);
  lv_label_set_text(updateDisconnectLabel, "DISCONNECT WI-FI");
  lv_obj_set_style_text_font(updateDisconnectLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(updateDisconnectLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(updateDisconnectLabel);

  updateForgetButton = lv_btn_create(network);
  lv_obj_set_size(updateForgetButton, 158, 40);
  lv_obj_set_pos(updateForgetButton, 188, 202);
  lv_obj_set_style_radius(updateForgetButton, 11, 0);
  lv_obj_set_style_bg_color(updateForgetButton, lv_color_hex(0x7A4037), 0);
  lv_obj_add_event_cb(updateForgetButton, wifiForgetAskEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *forgetLabel = lv_label_create(updateForgetButton);
  lv_label_set_text(forgetLabel, "FORGET WI-FI");
  lv_obj_set_style_text_font(forgetLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(forgetLabel, lv_color_hex(0xF7EDE9), 0);
  lv_obj_center(forgetLabel);

  updateQrHint = lv_label_create(network);
  lv_label_set_text(updateQrHint, "SCAN QR");
  lv_obj_set_style_text_font(updateQrHint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(updateQrHint, lv_color_hex(0xA5C3AD), 0);
  lv_obj_set_pos(updateQrHint, 20, 248);
  lv_obj_set_width(updateQrHint, kSetupQrOuterSize);
  lv_obj_set_style_text_align(updateQrHint, LV_TEXT_ALIGN_CENTER, 0);

  updateQrCard = lv_obj_create(network);
  lv_obj_set_size(updateQrCard, kSetupQrOuterSize, kSetupQrOuterSize);
  lv_obj_set_pos(updateQrCard, 20, 268);
  lv_obj_set_style_bg_color(updateQrCard, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(updateQrCard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(updateQrCard, 0, 0);
  lv_obj_set_style_radius(updateQrCard, 0, 0);
  lv_obj_set_style_pad_all(updateQrCard, 0, 0);
  lv_obj_clear_flag(updateQrCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(updateQrCard, LV_OBJ_FLAG_CLICKABLE);

  updateQrCode = lv_canvas_create(updateQrCard);
  lv_canvas_set_buffer(updateQrCode, updateQrCanvasBuffer,
                       kSetupQrSize, kSetupQrSize, LV_IMG_CF_INDEXED_1BIT);
  lv_canvas_set_palette(updateQrCode, 0, lv_color_black());
  lv_canvas_set_palette(updateQrCode, 1, lv_color_white());
  clearSetupQrCanvas();
  lv_obj_set_size(updateQrCode, kSetupQrSize, kSetupQrSize);
  lv_obj_center(updateQrCode);

  updatePortalInfo = lv_label_create(network);
  lv_label_set_text(updatePortalInfo,
                    "Tap SET UP / CHANGE WI-FI. Scan the QR when it appears.");
  lv_obj_set_style_text_font(updatePortalInfo, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(updatePortalInfo, lv_color_hex(0x9DB5A5), 0);
  lv_obj_set_pos(updatePortalInfo, 20, 260);
  lv_obj_set_width(updatePortalInfo, 326);
  lv_obj_set_height(updatePortalInfo, 116);
  lv_label_set_long_mode(updatePortalInfo, LV_LABEL_LONG_WRAP);

  lv_obj_add_flag(updateQrHint, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(updateQrCard, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *software = card(updateModal, 412, 82, 370, 388);
  lv_obj_set_style_pad_all(software, 0, 0);
  title = lv_label_create(software);
  lv_label_set_text(title, "SOFTWARE");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(title, 20, 14);

  cap = lv_label_create(software);
  lv_label_set_text(cap, "CURRENT");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 20, 58);

  updateCurrentVersion = lv_label_create(software);
  lv_obj_set_style_text_font(updateCurrentVersion, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(updateCurrentVersion, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(updateCurrentVersion, 20, 78);

  cap = lv_label_create(software);
  lv_label_set_text(cap, "LATEST");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 190, 58);

  updateLatestVersion = lv_label_create(software);
  lv_obj_set_style_text_font(updateLatestVersion, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(updateLatestVersion, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(updateLatestVersion, 190, 78);
  lv_obj_set_width(updateLatestVersion, 150);
  lv_label_set_long_mode(updateLatestVersion, LV_LABEL_LONG_DOT);

  // Alpha.21 H2 firmware identity display.  y=104, font 14 => bottom ~121;
  // updateStatus begins at y=128, so the rows do not overlap.
  updateH2Version = lv_label_create(software);
  lv_label_set_text(updateH2Version, "H2 GATEWAY: unavailable");
  lv_obj_set_style_text_font(updateH2Version, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updateH2Version, lv_color_hex(0x9DB5A5), 0);
  lv_obj_set_pos(updateH2Version, 20, 104);
  lv_obj_set_width(updateH2Version, 326);
  lv_label_set_long_mode(updateH2Version, LV_LABEL_LONG_DOT);

  updateStatus = lv_label_create(software);
  lv_label_set_text(updateStatus, "Connect Wi-Fi to check for updates.");
  lv_obj_set_style_text_font(updateStatus, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updateStatus, lv_color_hex(0xA5C3AD), 0);
  lv_obj_set_pos(updateStatus, 20, 128);
  lv_obj_set_width(updateStatus, 326);
  lv_obj_set_height(updateStatus, 60);
  lv_label_set_long_mode(updateStatus, LV_LABEL_LONG_WRAP);

  updateCheckButton = lv_btn_create(software);
  lv_obj_set_size(updateCheckButton, 326, 46);
  lv_obj_set_pos(updateCheckButton, 20, 194);
  lv_obj_set_style_radius(updateCheckButton, 12, 0);
  lv_obj_set_style_bg_color(updateCheckButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(updateCheckButton, updateCheckEvent, LV_EVENT_CLICKED, nullptr);
  updateCheckLabel = lv_label_create(updateCheckButton);
  lv_label_set_text(updateCheckLabel, "CHECK NOW");
  lv_obj_set_style_text_font(updateCheckLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(updateCheckLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(updateCheckLabel);

  updateInstallButton = lv_btn_create(software);
  lv_obj_set_size(updateInstallButton, 326, 54);
  lv_obj_set_pos(updateInstallButton, 20, 252);
  lv_obj_set_style_radius(updateInstallButton, 12, 0);
  lv_obj_set_style_bg_color(updateInstallButton, lv_color_hex(0x3F7A4E), 0);
  lv_obj_add_event_cb(updateInstallButton, updateInstallEvent, LV_EVENT_CLICKED, nullptr);
  updateInstallLabel = lv_label_create(updateInstallButton);
  lv_label_set_text(updateInstallLabel, "INSTALL UPDATE");
  lv_obj_set_style_text_font(updateInstallLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(updateInstallLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(updateInstallLabel);

  lv_obj_t *note = lv_label_create(software);
  lv_label_set_text(note, "Verified .plantsota writes only the inactive app slot; plant data is preserved.");
  lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(note, lv_color_hex(0x8DA695), 0);
  lv_obj_set_pos(note, 20, 320);
  lv_obj_set_width(note, 326);
  lv_obj_set_height(note, 50);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

  releaseNotesModal = lv_obj_create(updateModal);
  lv_obj_set_pos(releaseNotesModal, 0, 0);
  lv_obj_set_size(releaseNotesModal, 800, 480);
  lv_obj_set_style_radius(releaseNotesModal, 0, 0);
  lv_obj_set_style_border_width(releaseNotesModal, 0, 0);
  lv_obj_set_style_bg_color(releaseNotesModal, lv_color_hex(0x101814), 0);
  lv_obj_set_style_pad_all(releaseNotesModal, 0, 0);
  lv_obj_clear_flag(releaseNotesModal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *notesTop = lv_obj_create(releaseNotesModal);
  lv_obj_set_pos(notesTop, 0, 0);
  lv_obj_set_size(notesTop, 800, 70);
  lv_obj_set_style_radius(notesTop, 0, 0);
  lv_obj_set_style_border_width(notesTop, 0, 0);
  lv_obj_set_style_bg_color(notesTop, lv_color_hex(0x0C2518), 0);
  lv_obj_set_style_pad_all(notesTop, 0, 0);
  lv_obj_clear_flag(notesTop, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *notesTitle = lv_label_create(notesTop);
  lv_label_set_text(notesTitle, "RELEASE NOTES");
  lv_obj_set_style_text_font(notesTitle, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(notesTitle, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(notesTitle, 22, 18);

  lv_obj_t *notesBack = lv_btn_create(notesTop);
  lv_obj_set_size(notesBack, 94, 42);
  lv_obj_set_pos(notesBack, 684, 14);
  lv_obj_set_style_radius(notesBack, 11, 0);
  lv_obj_set_style_bg_color(notesBack, lv_color_hex(0x233029), 0);
  lv_obj_add_event_cb(notesBack, releaseNotesBackEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *notesBackLabel = lv_label_create(notesBack);
  lv_label_set_text(notesBackLabel, "BACK");
  lv_obj_set_style_text_font(notesBackLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(notesBackLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(notesBackLabel);

  lv_obj_t *notesScroll = card(releaseNotesModal, 18, 82, 764, 300);
  lv_obj_set_style_pad_all(notesScroll, 18, 0);
  lv_obj_set_scroll_dir(notesScroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(notesScroll, LV_SCROLLBAR_MODE_AUTO);
  releaseNotesLabel = lv_label_create(notesScroll);
  lv_label_set_text(releaseNotesLabel, "");
  lv_obj_set_width(releaseNotesLabel, 724);
  lv_obj_set_style_text_font(releaseNotesLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(releaseNotesLabel, lv_color_hex(0xD1DED5), 0);
  lv_label_set_long_mode(releaseNotesLabel, LV_LABEL_LONG_WRAP);

  lv_obj_t *notesCheck = lv_btn_create(releaseNotesModal);
  lv_obj_set_size(notesCheck, 180, 48);
  lv_obj_set_pos(notesCheck, 602, 400);
  lv_obj_set_style_radius(notesCheck, 11, 0);
  lv_obj_set_style_bg_color(notesCheck, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(notesCheck, releaseNotesCheckEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *notesCheckLabel = lv_label_create(notesCheck);
  lv_label_set_text(notesCheckLabel, "CHECK AGAIN");
  lv_obj_set_style_text_font(notesCheckLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(notesCheckLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(notesCheckLabel);
  lv_obj_add_flag(releaseNotesModal, LV_OBJ_FLAG_HIDDEN);

  wifiForgetConfirm = lv_obj_create(updateModal);
  lv_obj_set_pos(wifiForgetConfirm, 180, 120);
  lv_obj_set_size(wifiForgetConfirm, 440, 240);
  lv_obj_set_style_radius(wifiForgetConfirm, 18, 0);
  lv_obj_set_style_border_width(wifiForgetConfirm, 2, 0);
  lv_obj_set_style_border_color(wifiForgetConfirm, lv_color_hex(0x7A4037), 0);
  lv_obj_set_style_bg_color(wifiForgetConfirm, lv_color_hex(0x18231D), 0);
  lv_obj_set_style_pad_all(wifiForgetConfirm, 0, 0);
  lv_obj_clear_flag(wifiForgetConfirm, LV_OBJ_FLAG_SCROLLABLE);

  title = lv_label_create(wifiForgetConfirm);
  lv_label_set_text(title, "FORGET WI-FI?");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF7EDE9), 0);
  lv_obj_set_pos(title, 24, 22);

  lv_obj_t *warning = lv_label_create(wifiForgetConfirm);
  lv_label_set_text(warning,
                    "This erases the saved SSID and password and disconnects Wi-Fi. ESP PLANTS will keep working offline.");
  lv_obj_set_style_text_font(warning, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(warning, lv_color_hex(0xC1D0C6), 0);
  lv_obj_set_pos(warning, 24, 64);
  lv_obj_set_width(warning, 392);
  lv_obj_set_height(warning, 76);
  lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);

  lv_obj_t *cancel = lv_btn_create(wifiForgetConfirm);
  lv_obj_set_size(cancel, 180, 48);
  lv_obj_set_pos(cancel, 24, 164);
  lv_obj_set_style_radius(cancel, 11, 0);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(0x233029), 0);
  lv_obj_add_event_cb(cancel, wifiForgetCancelEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelLabel = lv_label_create(cancel);
  lv_label_set_text(cancelLabel, "CANCEL");
  lv_obj_set_style_text_font(cancelLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cancelLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(cancelLabel);

  lv_obj_t *forget = lv_btn_create(wifiForgetConfirm);
  lv_obj_set_size(forget, 180, 48);
  lv_obj_set_pos(forget, 236, 164);
  lv_obj_set_style_radius(forget, 11, 0);
  lv_obj_set_style_bg_color(forget, lv_color_hex(0x7A4037), 0);
  lv_obj_add_event_cb(forget, wifiForgetConfirmEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *forgetConfirmLabel = lv_label_create(forget);
  lv_label_set_text(forgetConfirmLabel, "FORGET WI-FI");
  lv_obj_set_style_text_font(forgetConfirmLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(forgetConfirmLabel, lv_color_hex(0xF7EDE9), 0);
  lv_obj_center(forgetConfirmLabel);

  lv_obj_add_flag(wifiForgetConfirm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(updateModal, LV_OBJ_FLAG_HIDDEN);
}

void buildRename(lv_obj_t *screen) {
  renameModal = lv_obj_create(screen);
  lv_obj_set_pos(renameModal, 0, 0);
  lv_obj_set_size(renameModal, 800, 480);
  lv_obj_set_style_radius(renameModal, 0, 0);
  lv_obj_set_style_border_width(renameModal, 0, 0);
  lv_obj_set_style_bg_color(renameModal, lv_color_hex(0x101814), 0);
  lv_obj_set_style_pad_all(renameModal, 0, 0);
  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *topBar = lv_obj_create(renameModal);
  lv_obj_set_pos(topBar, 0, 0);
  lv_obj_set_size(topBar, 800, 68);
  lv_obj_set_style_radius(topBar, 0, 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_style_bg_color(topBar, lv_color_hex(0x0C2518), 0);
  lv_obj_set_style_pad_all(topBar, 0, 0);
  lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

  renameTitle = lv_label_create(topBar);
  lv_obj_set_style_text_font(renameTitle, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(renameTitle, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(renameTitle, 18, 8);

  renameHint = lv_label_create(topBar);
  lv_label_set_text(renameHint, "Name stays tied to this sensor.");
  lv_obj_set_style_text_font(renameHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(renameHint, lv_color_hex(0x9DB5A5), 0);
  lv_obj_set_pos(renameHint, 20, 36);

  renameInput = lv_textarea_create(renameModal);
  lv_obj_set_pos(renameInput, 18, 82);
  lv_obj_set_size(renameInput, 764, 62);
  lv_textarea_set_one_line(renameInput, true);
  lv_textarea_set_max_length(renameInput, kPlantNameBytes - 1);
  lv_obj_set_style_text_font(renameInput, &lv_font_montserrat_28, 0);
  lv_obj_set_style_radius(renameInput, 12, 0);
  lv_obj_set_style_border_width(renameInput, 2, 0);
  lv_obj_set_style_border_color(renameInput, lv_color_hex(0x3F7A4E), 0);
  lv_obj_set_style_bg_color(renameInput, lv_color_hex(0x131C17), 0);
  lv_obj_set_style_text_color(renameInput, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_style_border_width(renameInput, 3, LV_PART_CURSOR);
  lv_obj_set_style_border_color(renameInput, lv_color_hex(0xF2C66D), LV_PART_CURSOR);
  lv_obj_set_style_bg_color(renameInput, lv_color_hex(0xF2C66D), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(renameInput, LV_OPA_COVER, LV_PART_CURSOR);

  renameKeyboard = lv_btnmatrix_create(renameModal);
  lv_obj_set_pos(renameKeyboard, 18, 158);
  lv_obj_set_size(renameKeyboard, 764, 304);
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_set_style_radius(renameKeyboard, 14, 0);
  lv_obj_set_style_border_width(renameKeyboard, 0, 0);
  lv_obj_set_style_bg_color(renameKeyboard, lv_color_hex(0x18231D), 0);
  lv_obj_set_style_pad_all(renameKeyboard, 8, 0);
  lv_obj_set_style_pad_row(renameKeyboard, 7, 0);
  lv_obj_set_style_pad_column(renameKeyboard, 7, 0);

  lv_obj_set_style_radius(renameKeyboard, 9, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(renameKeyboard, lv_color_hex(0x233029), LV_PART_ITEMS);
  lv_obj_set_style_text_color(renameKeyboard, lv_color_hex(0xE5ECE7), LV_PART_ITEMS);
  lv_obj_set_style_text_font(renameKeyboard, &lv_font_montserrat_18, LV_PART_ITEMS);

  lv_obj_add_event_cb(renameKeyboard, renameKeyboardEvent, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}



void buildPairDialog(lv_obj_t *screen) {
  pairModal = lv_obj_create(screen);
  lv_obj_set_pos(pairModal, 0, 0);
  lv_obj_set_size(pairModal, 800, 480);
  lv_obj_set_style_radius(pairModal, 0, 0);
  lv_obj_set_style_border_width(pairModal, 0, 0);
  lv_obj_set_style_bg_color(pairModal, lv_color_hex(0x101814), 0);
  lv_obj_set_style_pad_all(pairModal, 0, 0);
  lv_obj_clear_flag(pairModal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *top = lv_obj_create(pairModal);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_size(top, 800, 74);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_bg_color(top, lv_color_hex(0x0C2518), 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

  pairTitle = lv_label_create(top);
  lv_obj_set_style_text_font(pairTitle, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(pairTitle, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(pairTitle, 22, 18);

  lv_obj_t *cardObj = lv_obj_create(pairModal);
  lv_obj_set_pos(cardObj, 24, 94);
  lv_obj_set_size(cardObj, 752, 350);
  lv_obj_set_style_radius(cardObj, 20, 0);
  lv_obj_set_style_border_width(cardObj, 1, 0);
  lv_obj_set_style_border_color(cardObj, lv_color_hex(0x304138), 0);
  lv_obj_set_style_bg_color(cardObj, lv_color_hex(0x18231D), 0);
  lv_obj_clear_flag(cardObj, LV_OBJ_FLAG_SCROLLABLE);

  pairInstruction = lv_label_create(cardObj);
  lv_obj_set_style_text_font(pairInstruction, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(pairInstruction, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(pairInstruction, 28, 28);
  lv_obj_set_width(pairInstruction, 680);
  lv_label_set_long_mode(pairInstruction, LV_LABEL_LONG_WRAP);

  pairStatus = lv_label_create(cardObj);
  lv_obj_set_style_text_font(pairStatus, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(pairStatus, lv_color_hex(0xA5C3AD), 0);
  lv_obj_set_pos(pairStatus, 28, 126);
  lv_obj_set_width(pairStatus, 680);
  lv_label_set_long_mode(pairStatus, LV_LABEL_LONG_WRAP);

  pairPrimary = lv_btn_create(cardObj);
  lv_obj_set_size(pairPrimary, 220, 58);
  lv_obj_set_pos(pairPrimary, 140, 250);
  lv_obj_set_style_radius(pairPrimary, 12, 0);
  lv_obj_set_style_bg_color(pairPrimary, lv_color_hex(0x3F7A4E), 0);
  lv_obj_add_event_cb(pairPrimary, pairPrimaryEvent, LV_EVENT_CLICKED, nullptr);
  pairPrimaryLabel = lv_label_create(pairPrimary);
  lv_obj_set_style_text_font(pairPrimaryLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(pairPrimaryLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(pairPrimaryLabel);

  pairSecondary = lv_btn_create(cardObj);
  lv_obj_set_size(pairSecondary, 220, 58);
  lv_obj_set_pos(pairSecondary, 392, 250);
  lv_obj_set_style_radius(pairSecondary, 12, 0);
  lv_obj_set_style_bg_color(pairSecondary, lv_color_hex(0x233029), 0);
  lv_obj_set_style_border_width(pairSecondary, 1, 0);
  lv_obj_set_style_border_color(pairSecondary, lv_color_hex(0x405348), 0);
  lv_obj_add_event_cb(pairSecondary, pairSecondaryEvent, LV_EVENT_CLICKED, nullptr);
  pairSecondaryLabel = lv_label_create(pairSecondary);
  lv_obj_set_style_text_font(pairSecondaryLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(pairSecondaryLabel, lv_color_hex(0xE5ECE7), 0);
  lv_obj_center(pairSecondaryLabel);

  lv_obj_add_flag(pairModal, LV_OBJ_FLAG_HIDDEN);
}

void buildUi() {
  lv_obj_t *screen = lv_scr_act();
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
