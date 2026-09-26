#include "plants_ota_installer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_app_format.h>
#include <esp_attr.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <xtensa/xtensa_api.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#include <strings.h>

#include "update_policy.h"

namespace espplants_ota_installer {
namespace {

using namespace espplants_update_policy;

constexpr uint16_t kPackageFormatVersion = 1;
constexpr uint16_t kPackageHeaderSize = 512;
constexpr uint8_t kEspApplicationMagic = 0xE9;
constexpr uint16_t kEsp32S3ImageChipId = 9;
constexpr char kPackageMagic[16] = "ESP-PLANTS-OTA";
constexpr char kPackageHardwareId[32] = "WAVESHARE-ESP32-S3-LCD-7";
constexpr char kPackageProductId[32] = "ESP-PLANTS-WAVESHARE";
constexpr char kDistributionMarker[] = "ESP-PLANTS-DISTRIBUTION-BUILD";
constexpr char kReleaseDownloadPrefix[] =
    "https://github.com/bcarriveau/esp-plants/releases/download/";
constexpr char kUserAgent[] = "ESP-PLANTS-Waveshare-Installer/1";
constexpr uint8_t kMaxRedirects = 3;
constexpr uint32_t kInstallTotalTimeoutMs = 3UL * 60UL * 1000UL;
constexpr uint32_t kHttpConnectTimeoutMs = 8000UL;
constexpr uint32_t kHttpBodyIdleTimeoutMs = 15000UL;
constexpr uint32_t kMinimumHttpBudgetMs = 500UL;
constexpr uint32_t kTransportReleaseDelayMs = 75UL;
constexpr size_t kDownloadBufferBytes = 4096U;
// Network/TLS producer and flash consumer are deliberately separated. The ring
// lives in PSRAM; only this bounded buffer and the flash-writer task stack live
// in internal RAM while esp_ota_write() is executing.
constexpr size_t kOtaRingSlotBytes = 4096U;
constexpr size_t kOtaRingSlotCount = 8U;
constexpr size_t kOtaRingBytes = kOtaRingSlotBytes * kOtaRingSlotCount;
constexpr size_t kInternalFlashWriteBufferBytes = 1024U;
constexpr uint32_t kFlashWriterStackBytes = 6144U;
constexpr uint32_t kFlashWriterWaitMs = 15000U;
constexpr uint32_t kProgressGranularityBytes = 64U * 1024U;

constexpr uint32_t kRestartDelayMs = 1500U;
constexpr uint32_t kRestartSettleMs = 500U;
constexpr uint32_t kRestartTaskStackBytes = 4096U;
constexpr uint32_t kRestartLoopQuiesceTimeoutMs = 1000U;
constexpr BaseType_t kRestartTaskCore = 0;
constexpr BaseType_t kRestartLoopCore = 1;
constexpr UBaseType_t kRestartTaskPriority = configMAX_PRIORITIES - 1;
constexpr char kRestartTaskName[] = "plants_ota_restart";
constexpr uint32_t kRestartLoopWaiting = 0;
constexpr uint32_t kRestartLoopQuiesced = 1;
constexpr uint32_t kRestartLoopAborted = 2;

#pragma pack(push, 1)
struct PackageHeader {
  char magic[16];
  uint16_t formatVersion;
  uint16_t headerSize;
  char hardwareId[32];
  char productId[32];
  char buildId[96];
  uint32_t firmwareSize;
  uint8_t firmwareSha256[32];
  uint8_t reserved[296];
};
#pragma pack(pop)

static_assert(sizeof(PackageHeader) == kPackageHeaderSize,
              "ESP PLANTS OTA package header must remain 512 bytes");
static_assert(sizeof(esp_image_header_t) == 24,
              "Unexpected ESP application image header size");

enum class HeaderFailure : uint8_t {
  NONE = 0,
  TOTAL_BYTES,
  CONTENT_LENGTH,
  TRANSFER_ENCODING,
  LOCATION_TOO_LONG,
  CONFLICTING_FRAMING,
};

struct HttpHeaderState {
  size_t totalBytes = 0;
  HeaderFailure failure = HeaderFailure::NONE;
  bool contentLengthSeen = false;
  bool transferEncodingSeen = false;
  bool chunkedOnly = false;
  uint64_t contentLength = 0;
  char location[kMaxRedirectUrlLength + 1U]{};
};

struct StreamMatcher {
  const char *pattern = nullptr;
  size_t patternLength = 0;
  size_t matchLength = 0;
  bool seen = false;
  uint8_t failure[kMaxBuildIdLength + 1U]{};
};

enum class FlashWriterState : uint32_t {
  STARTING = 0,
  RUNNING,
  COMMIT_REQUESTED,
  ABORT_REQUESTED,
  COMPLETE,
  ABORTED,
  FAILED,
};

struct FlashWriterContext {
  const esp_partition_t *updatePartition = nullptr;
  uint8_t *psramRing = nullptr;
  TaskHandle_t producerTask = nullptr;
  TaskHandle_t writerTask = nullptr;
  volatile uint32_t writeSequence = 0;
  volatile uint32_t readSequence = 0;
  volatile uint32_t state = static_cast<uint32_t>(FlashWriterState::STARTING);
  uint16_t slotLengths[kOtaRingSlotCount]{};
  uint32_t expectedFirmwareBytes = 0;
  uint32_t writtenBytes = 0;
  esp_ota_handle_t otaHandle = 0;
  bool otaHandleActive = false;
  char error[128]{};
  uint8_t internalWriteBuffer[kInternalFlashWriteBufferBytes]{};
};

static_assert(sizeof(FlashWriterContext) <= 2048U,
              "Flash writer internal control block exceeded its DRAM budget");

struct InstallWorkspace {
  HttpHeaderState headers;
  char currentUrl[kMaxRedirectUrlLength + 1U]{};
  char host[96]{};
  char redirectHost[96]{};
  uint8_t downloadBuffer[kDownloadBufferBytes]{};
  uint8_t packageHeaderBytes[kPackageHeaderSize]{};
  PackageHeader packageHeader{};
  uint8_t imagePrefix[sizeof(esp_image_header_t)]{};
  size_t packageHeaderReceived = 0;
  size_t imagePrefixReceived = 0;
  uint32_t packageReceived = 0;
  uint32_t payloadReceived = 0;
  uint32_t lastProgressBytes = 0;
  const esp_partition_t *updatePartition = nullptr;
  uint8_t *psramRing = nullptr;
  FlashWriterContext *writer = nullptr;
  mbedtls_sha256_context packageSha;
  mbedtls_sha256_context firmwareSha;
  bool packageShaActive = false;
  bool firmwareShaActive = false;
  StreamMatcher buildIdMatcher;
  StreamMatcher distributionMatcher;
};

static_assert(sizeof(InstallWorkspace) <= 16U * 1024U,
              "ESP PLANTS remote OTA workspace exceeded PSRAM budget");

void abortAndDestroyFlashWriter(InstallWorkspace &workspace);

class WorkspaceGuard {
 public:
  WorkspaceGuard() {
    workspace_ = static_cast<InstallWorkspace *>(heap_caps_malloc(
        sizeof(InstallWorkspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (workspace_) new (workspace_) InstallWorkspace{};
  }

  ~WorkspaceGuard() {
    if (!workspace_) return;
    abortAndDestroyFlashWriter(*workspace_);
    if (workspace_->packageShaActive) mbedtls_sha256_free(&workspace_->packageSha);
    if (workspace_->firmwareShaActive) mbedtls_sha256_free(&workspace_->firmwareSha);
    if (workspace_->psramRing) heap_caps_free(workspace_->psramRing);
    workspace_->~InstallWorkspace();
    heap_caps_free(workspace_);
  }

  InstallWorkspace *get() const { return workspace_; }

 private:
  InstallWorkspace *workspace_ = nullptr;
};

portMUX_TYPE restartMux = portMUX_INITIALIZER_UNLOCKED;
RestartState currentRestartState = RestartState::IDLE;
char restartStatusMessage[128]{};
uint32_t restartAtMs = 0;
uint32_t restartExecuteAtMs = 0;
TaskHandle_t restartTaskHandle = nullptr;
DRAM_ATTR uint32_t restartLoopState = kRestartLoopWaiting;
bool restartTaskCreationAttempted = false;

void copyText(char *destination, size_t capacity, const char *source) {
  if (!destination || capacity == 0) return;
  snprintf(destination, capacity, "%s", source ? source : "");
}

bool deadlineReached(uint32_t deadlineMs) {
  return static_cast<int32_t>(millis() - deadlineMs) >= 0;
}

uint32_t remainingToDeadline(uint32_t deadlineMs) {
  const uint32_t now = millis();
  return static_cast<int32_t>(deadlineMs - now) > 0 ? deadlineMs - now : 0U;
}

const char *skipWhitespace(const char *text) {
  if (!text) return nullptr;
  while (*text == ' ' || *text == '\t') ++text;
  return text;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  return left && right && strcasecmp(left, right) == 0;
}

bool equalsHeaderToken(const char *text, const char *expected) {
  text = skipWhitespace(text);
  if (!text || !expected) return false;
  const size_t expectedLength = strlen(expected);
  if (strncasecmp(text, expected, expectedLength) != 0) return false;
  text += expectedLength;
  while (*text == ' ' || *text == '\t') ++text;
  return *text == 0;
}

bool parseUnsignedHeader(const char *text, uint64_t &value) {
  text = skipWhitespace(text);
  if (!text || !*text) return false;
  uint64_t parsed = 0;
  while (*text >= '0' && *text <= '9') {
    const uint8_t digit = static_cast<uint8_t>(*text - '0');
    if (parsed > (UINT64_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
    ++text;
  }
  while (*text == ' ' || *text == '\t') ++text;
  if (*text != 0) return false;
  value = parsed;
  return true;
}

void setHeaderFailure(HttpHeaderState &state, HeaderFailure failure) {
  if (state.failure == HeaderFailure::NONE) state.failure = failure;
}

const char *headerFailureMessage(HeaderFailure failure) {
  switch (failure) {
    case HeaderFailure::TOTAL_BYTES:
      return "Release response headers exceeded the 16 KB limit";
    case HeaderFailure::CONTENT_LENGTH:
      return "Release Content-Length was invalid or conflicting";
    case HeaderFailure::TRANSFER_ENCODING:
      return "Release Transfer-Encoding was invalid or repeated";
    case HeaderFailure::LOCATION_TOO_LONG:
      return "Release redirect URL exceeded the safety limit";
    case HeaderFailure::CONFLICTING_FRAMING:
      return "Release response contained conflicting framing headers";
    case HeaderFailure::NONE:
    default:
      return "Release response headers were invalid";
  }
}

esp_err_t httpEventHandler(esp_http_client_event_t *event) {
  if (!event || !event->user_data) return ESP_OK;
  HttpHeaderState &state = *static_cast<HttpHeaderState *>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key ||
      !event->header_value) {
    return state.failure == HeaderFailure::NONE ? ESP_OK : ESP_FAIL;
  }

  size_t updatedTotal = state.totalBytes;
  if (!accumulateHeaderBytes(state.totalBytes, strlen(event->header_key),
                             strlen(event->header_value), updatedTotal)) {
    setHeaderFailure(state, HeaderFailure::TOTAL_BYTES);
    return ESP_FAIL;
  }
  state.totalBytes = updatedTotal;

  if (equalsIgnoreCase(event->header_key, "Content-Length")) {
    uint64_t parsed = 0;
    if (!parseUnsignedHeader(event->header_value, parsed) ||
        (state.contentLengthSeen && state.contentLength != parsed)) {
      setHeaderFailure(state, HeaderFailure::CONTENT_LENGTH);
      return ESP_FAIL;
    }
    state.contentLengthSeen = true;
    state.contentLength = parsed;
  } else if (equalsIgnoreCase(event->header_key, "Transfer-Encoding")) {
    if (state.transferEncodingSeen ||
        !equalsHeaderToken(event->header_value, "chunked")) {
      setHeaderFailure(state, HeaderFailure::TRANSFER_ENCODING);
      return ESP_FAIL;
    }
    state.transferEncodingSeen = true;
    state.chunkedOnly = true;
  } else if (equalsIgnoreCase(event->header_key, "Location")) {
    const char *value = skipWhitespace(event->header_value);
    if (!redirectUrlLengthValid(value)) {
      setHeaderFailure(state, HeaderFailure::LOCATION_TOO_LONG);
      return ESP_FAIL;
    }
    copyText(state.location, sizeof(state.location), value);
  }

  if (state.contentLengthSeen && state.transferEncodingSeen) {
    setHeaderFailure(state, HeaderFailure::CONFLICTING_FRAMING);
    return ESP_FAIL;
  }
  return ESP_OK;
}

void releaseHttpClient(esp_http_client_handle_t client, bool opened) {
  if (!client) return;
  if (opened) esp_http_client_close(client);
  esp_http_client_cleanup(client);
  delay(kTransportReleaseDelayMs);
}

bool makeAssetUrl(const Release &release, char *destination, size_t capacity) {
  if (!destination || capacity == 0 || !tagValid(release.tag) ||
      !assetNameValid(release.asset)) return false;
  const int written = snprintf(destination, capacity, "%s%s/%s",
                               kReleaseDownloadPrefix, release.tag, release.asset);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

void prepareMatcher(StreamMatcher &matcher, const char *pattern) {
  matcher = StreamMatcher{};
  matcher.pattern = pattern;
  matcher.patternLength = pattern ? strlen(pattern) : 0U;
  if (!matcher.patternLength || matcher.patternLength > sizeof(matcher.failure)) return;
  for (size_t index = 1, prefix = 0; index < matcher.patternLength; ++index) {
    while (prefix > 0 && pattern[index] != pattern[prefix]) {
      prefix = matcher.failure[prefix - 1U];
    }
    if (pattern[index] == pattern[prefix]) ++prefix;
    matcher.failure[index] = static_cast<uint8_t>(prefix);
  }
}

void observeMatcher(StreamMatcher &matcher, const uint8_t *data, size_t length) {
  if (matcher.seen || !matcher.pattern || !matcher.patternLength) return;
  for (size_t index = 0; index < length; ++index) {
    const char current = static_cast<char>(data[index]);
    while (matcher.matchLength > 0 &&
           current != matcher.pattern[matcher.matchLength]) {
      matcher.matchLength = matcher.failure[matcher.matchLength - 1U];
    }
    if (current == matcher.pattern[matcher.matchLength]) {
      ++matcher.matchLength;
      if (matcher.matchLength == matcher.patternLength) {
        matcher.seen = true;
        return;
      }
    }
  }
}

bool validatePackageHeader(InstallWorkspace &workspace, const Release &release,
                           char *message, size_t messageCapacity) {
  memcpy(&workspace.packageHeader, workspace.packageHeaderBytes,
         sizeof(workspace.packageHeader));
  const PackageHeader &header = workspace.packageHeader;

  if (memcmp(header.magic, kPackageMagic, sizeof(kPackageMagic)) != 0) {
    copyText(message, messageCapacity,
             "Downloaded file is not an ESP PLANTS OTA package");
    return false;
  }
  if (header.formatVersion != kPackageFormatVersion ||
      header.headerSize != kPackageHeaderSize) {
    copyText(message, messageCapacity, "Downloaded package format is unsupported");
    return false;
  }
  if (header.hardwareId[sizeof(header.hardwareId) - 1U] != 0 ||
      strcmp(header.hardwareId, kPackageHardwareId) != 0) {
    copyText(message, messageCapacity, "Downloaded package is for different hardware");
    return false;
  }
  if (header.productId[sizeof(header.productId) - 1U] != 0 ||
      strcmp(header.productId, kPackageProductId) != 0) {
    copyText(message, messageCapacity, "Downloaded package is for a different product");
    return false;
  }
  if (header.buildId[sizeof(header.buildId) - 1U] != 0 ||
      strcmp(header.buildId, release.buildId) != 0) {
    copyText(message, messageCapacity,
             "Downloaded package build does not match the manifest");
    return false;
  }
  if (header.firmwareSize != release.firmwareSize ||
      memcmp(header.firmwareSha256, release.firmwareSha256,
             sizeof(header.firmwareSha256)) != 0) {
    copyText(message, messageCapacity,
             "Downloaded package firmware identity does not match the manifest");
    return false;
  }
  if (!workspace.updatePartition ||
      !packageLayoutValid(release.packageSize, header.firmwareSize) ||
      header.firmwareSize > workspace.updatePartition->size) {
    copyText(message, messageCapacity, "Firmware does not fit the inactive OTA partition");
    return false;
  }

  mbedtls_sha256_init(&workspace.firmwareSha);
  if (mbedtls_sha256_starts(&workspace.firmwareSha, 0) != 0) {
    copyText(message, messageCapacity, "Firmware SHA-256 initialization failed");
    return false;
  }
  workspace.firmwareShaActive = true;
  prepareMatcher(workspace.buildIdMatcher, workspace.packageHeader.buildId);
  prepareMatcher(workspace.distributionMatcher, kDistributionMarker);

  Serial.printf("[update] package header accepted: build=%s firmware=%lu bytes slot=%s\n",
                header.buildId, static_cast<unsigned long>(header.firmwareSize),
                workspace.updatePartition->label);
  return true;
}

bool validateImagePrefix(InstallWorkspace &workspace,
                         char *message, size_t messageCapacity) {
  esp_image_header_t imageHeader{};
  memcpy(&imageHeader, workspace.imagePrefix, sizeof(imageHeader));
  if (imageHeader.magic != kEspApplicationMagic) {
    copyText(message, messageCapacity, "Downloaded firmware has invalid ESP image magic");
    return false;
  }
  if (imageHeader.chip_id != kEsp32S3ImageChipId) {
    copyText(message, messageCapacity, "Downloaded firmware is not for ESP32-S3");
    return false;
  }
  return true;
}

FlashWriterState flashWriterState(const FlashWriterContext &writer) {
  return static_cast<FlashWriterState>(
      __atomic_load_n(&writer.state, __ATOMIC_ACQUIRE));
}

void setFlashWriterState(FlashWriterContext &writer, FlashWriterState state) {
  __atomic_store_n(&writer.state, static_cast<uint32_t>(state), __ATOMIC_RELEASE);
}

void failFlashWriter(FlashWriterContext &writer, const char *message) {
  copyText(writer.error, sizeof(writer.error), message);
  if (writer.otaHandleActive) {
    esp_ota_abort(writer.otaHandle);
    writer.otaHandleActive = false;
  }
  setFlashWriterState(writer, FlashWriterState::FAILED);
  if (writer.producerTask) xTaskNotifyGive(writer.producerTask);
}

void flashWriterTask(void *parameter) {
  FlashWriterContext &writer = *static_cast<FlashWriterContext *>(parameter);
  esp_ota_handle_t handle = 0;
  const esp_err_t beginResult = esp_ota_begin(
      writer.updatePartition, writer.expectedFirmwareBytes, &handle);
  if (beginResult != ESP_OK) {
    char text[128]{};
    snprintf(text, sizeof(text), "OTA partition begin failed: %s",
             esp_err_to_name(beginResult));
    failFlashWriter(writer, text);
  } else {
    writer.otaHandle = handle;
    writer.otaHandleActive = true;
    setFlashWriterState(writer, FlashWriterState::RUNNING);
    if (writer.producerTask) xTaskNotifyGive(writer.producerTask);
  }

  for (;;) {
    const FlashWriterState state = flashWriterState(writer);
    if (state == FlashWriterState::FAILED ||
        state == FlashWriterState::COMPLETE ||
        state == FlashWriterState::ABORTED) {
      // The PSRAM network worker deletes this task only after it is blocked here.
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    if (state == FlashWriterState::ABORT_REQUESTED) {
      if (writer.otaHandleActive) {
        esp_ota_abort(writer.otaHandle);
        writer.otaHandleActive = false;
      }
      setFlashWriterState(writer, FlashWriterState::ABORTED);
      if (writer.producerTask) xTaskNotifyGive(writer.producerTask);
      continue;
    }

    const uint32_t readSequence =
        __atomic_load_n(&writer.readSequence, __ATOMIC_ACQUIRE);
    const uint32_t writeSequence =
        __atomic_load_n(&writer.writeSequence, __ATOMIC_ACQUIRE);
    if (readSequence < writeSequence) {
      const size_t slot = readSequence % kOtaRingSlotCount;
      const size_t length = writer.slotLengths[slot];
      const uint8_t *source = writer.psramRing + slot * kOtaRingSlotBytes;
      size_t offset = 0;
      while (offset < length) {
        const size_t chunk = std::min(
            kInternalFlashWriteBufferBytes, length - offset);
        // PSRAM is touched only while copying into this internal buffer. The
        // subsequent flash operation depends only on internal stack/data.
        memcpy(writer.internalWriteBuffer, source + offset, chunk);
        const esp_err_t writeResult =
            esp_ota_write(writer.otaHandle, writer.internalWriteBuffer, chunk);
        if (writeResult != ESP_OK) {
          char text[128]{};
          snprintf(text, sizeof(text), "Firmware write failed: %s",
                   esp_err_to_name(writeResult));
          failFlashWriter(writer, text);
          break;
        }
        writer.writtenBytes += static_cast<uint32_t>(chunk);
        offset += chunk;
      }
      if (flashWriterState(writer) == FlashWriterState::FAILED) continue;
      __atomic_store_n(&writer.readSequence, readSequence + 1U, __ATOMIC_RELEASE);
      if (writer.producerTask) xTaskNotifyGive(writer.producerTask);
      continue;
    }

    if (state == FlashWriterState::COMMIT_REQUESTED) {
      if (writer.writtenBytes != writer.expectedFirmwareBytes) {
        failFlashWriter(writer, "Flash writer did not receive the complete firmware image");
        continue;
      }
      const esp_err_t endResult = esp_ota_end(writer.otaHandle);
      writer.otaHandleActive = false;
      if (endResult != ESP_OK) {
        char text[128]{};
        snprintf(text, sizeof(text), "ESP image validation failed: %s",
                 esp_err_to_name(endResult));
        failFlashWriter(writer, text);
        continue;
      }
      const esp_err_t bootResult =
          esp_ota_set_boot_partition(writer.updatePartition);
      if (bootResult != ESP_OK) {
        char text[128]{};
        snprintf(text, sizeof(text), "Boot partition update failed: %s",
                 esp_err_to_name(bootResult));
        failFlashWriter(writer, text);
        continue;
      }
      setFlashWriterState(writer, FlashWriterState::COMPLETE);
      if (writer.producerTask) xTaskNotifyGive(writer.producerTask);
      continue;
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
  }
}

bool waitForWriterState(FlashWriterContext &writer, FlashWriterState wanted,
                        uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    const FlashWriterState state = flashWriterState(writer);
    if (state == wanted) return true;
    if (state == FlashWriterState::FAILED || state == FlashWriterState::ABORTED)
      return false;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
  }
  return flashWriterState(writer) == wanted;
}

bool startFlashWriter(InstallWorkspace &workspace,
                      char *message, size_t messageCapacity) {
  if (workspace.writer) return true;
  if (!workspace.psramRing || !workspace.updatePartition) {
    copyText(message, messageCapacity, "OTA producer buffer was not initialized");
    return false;
  }

  auto *writer = static_cast<FlashWriterContext *>(heap_caps_calloc(
      1, sizeof(FlashWriterContext), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!writer) {
    copyText(message, messageCapacity, "Internal flash-writer control allocation failed");
    return false;
  }
  writer->updatePartition = workspace.updatePartition;
  writer->psramRing = workspace.psramRing;
  writer->producerTask = xTaskGetCurrentTaskHandle();
  writer->expectedFirmwareBytes = workspace.packageHeader.firmwareSize;
  workspace.writer = writer;

  const BaseType_t created = xTaskCreateWithCaps(
      flashWriterTask, "espplants-ota-flash", kFlashWriterStackBytes, writer, 2,
      &writer->writerTask, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (created != pdPASS) {
    workspace.writer = nullptr;
    heap_caps_free(writer);
    copyText(message, messageCapacity, "Could not start internal OTA flash writer");
    return false;
  }
  if (!waitForWriterState(*writer, FlashWriterState::RUNNING,
                          kFlashWriterWaitMs)) {
    copyText(message, messageCapacity,
             writer->error[0] ? writer->error : "Internal OTA flash writer did not start");
    return false;
  }
  return true;
}

bool queueFirmwareBytes(InstallWorkspace &workspace, const uint8_t *data,
                        size_t length, char *message, size_t messageCapacity) {
  if (!length) return true;
  if (!workspace.writer) {
    copyText(message, messageCapacity, "Internal OTA flash writer is unavailable");
    return false;
  }
  FlashWriterContext &writer = *workspace.writer;

  while (length) {
    while (__atomic_load_n(&writer.writeSequence, __ATOMIC_ACQUIRE) -
               __atomic_load_n(&writer.readSequence, __ATOMIC_ACQUIRE) >=
           kOtaRingSlotCount) {
      if (flashWriterState(writer) == FlashWriterState::FAILED) {
        copyText(message, messageCapacity,
                 writer.error[0] ? writer.error : "Internal OTA flash writer failed");
        return false;
      }
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
    }

    if (flashWriterState(writer) != FlashWriterState::RUNNING) {
      copyText(message, messageCapacity,
               writer.error[0] ? writer.error : "Internal OTA flash writer stopped");
      return false;
    }
    const uint32_t sequence =
        __atomic_load_n(&writer.writeSequence, __ATOMIC_ACQUIRE);
    const size_t slot = sequence % kOtaRingSlotCount;
    const size_t chunk = std::min(length, kOtaRingSlotBytes);
    memcpy(workspace.psramRing + slot * kOtaRingSlotBytes, data, chunk);
    writer.slotLengths[slot] = static_cast<uint16_t>(chunk);
    __atomic_store_n(&writer.writeSequence, sequence + 1U, __ATOMIC_RELEASE);
    xTaskNotifyGive(writer.writerTask);
    data += chunk;
    length -= chunk;
  }
  return true;
}

void abortAndDestroyFlashWriter(InstallWorkspace &workspace) {
  FlashWriterContext *writer = workspace.writer;
  if (!writer) return;

  const FlashWriterState state = flashWriterState(*writer);
  if (state != FlashWriterState::COMPLETE &&
      state != FlashWriterState::FAILED &&
      state != FlashWriterState::ABORTED) {
    setFlashWriterState(*writer, FlashWriterState::ABORT_REQUESTED);
    if (writer->writerTask) xTaskNotifyGive(writer->writerTask);
    const uint32_t started = millis();
    while (millis() - started < kFlashWriterWaitMs) {
      const FlashWriterState now = flashWriterState(*writer);
      if (now == FlashWriterState::ABORTED || now == FlashWriterState::FAILED) break;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
    }
  }
  if (writer->writerTask) {
    vTaskDeleteWithCaps(writer->writerTask);
    writer->writerTask = nullptr;
  }
  heap_caps_free(writer);
  workspace.writer = nullptr;
}

bool processPayload(InstallWorkspace &workspace, const uint8_t *data,
                    size_t length, char *message, size_t messageCapacity) {
  if (!length) return true;
  if (!workspace.firmwareShaActive) {
    copyText(message, messageCapacity, "Firmware validator was not initialized");
    return false;
  }
  if (workspace.payloadReceived > workspace.packageHeader.firmwareSize ||
      length > workspace.packageHeader.firmwareSize - workspace.payloadReceived) {
    copyText(message, messageCapacity, "Downloaded package contains excess firmware data");
    return false;
  }

  observeMatcher(workspace.buildIdMatcher, data, length);
  observeMatcher(workspace.distributionMatcher, data, length);
  if (mbedtls_sha256_update(&workspace.firmwareSha, data, length) != 0) {
    copyText(message, messageCapacity, "Firmware SHA-256 update failed");
    return false;
  }
  workspace.payloadReceived += static_cast<uint32_t>(length);

  if (workspace.imagePrefixReceived < sizeof(workspace.imagePrefix)) {
    const size_t needed = sizeof(workspace.imagePrefix) - workspace.imagePrefixReceived;
    const size_t copyLength = std::min(needed, length);
    memcpy(workspace.imagePrefix + workspace.imagePrefixReceived, data, copyLength);
    workspace.imagePrefixReceived += copyLength;
    data += copyLength;
    length -= copyLength;
    if (workspace.imagePrefixReceived < sizeof(workspace.imagePrefix)) return true;
    if (!validateImagePrefix(workspace, message, messageCapacity) ||
        !startFlashWriter(workspace, message, messageCapacity) ||
        !queueFirmwareBytes(workspace, workspace.imagePrefix,
                            sizeof(workspace.imagePrefix), message, messageCapacity)) {
      return false;
    }
  }
  return queueFirmwareBytes(workspace, data, length, message, messageCapacity);
}

bool processPackageBytes(InstallWorkspace &workspace, const Release &release,
                         const uint8_t *data, size_t length,
                         char *message, size_t messageCapacity) {
  if (!workspace.packageShaActive) {
    copyText(message, messageCapacity, "Package SHA-256 was not initialized");
    return false;
  }
  if (workspace.packageReceived > release.packageSize ||
      length > release.packageSize - workspace.packageReceived) {
    copyText(message, messageCapacity, "Downloaded package exceeded manifest size");
    return false;
  }
  if (mbedtls_sha256_update(&workspace.packageSha, data, length) != 0) {
    copyText(message, messageCapacity, "Package SHA-256 update failed");
    return false;
  }
  workspace.packageReceived += static_cast<uint32_t>(length);

  if (workspace.packageHeaderReceived < kPackageHeaderSize) {
    const size_t needed = kPackageHeaderSize - workspace.packageHeaderReceived;
    const size_t copyLength = std::min(needed, length);
    memcpy(workspace.packageHeaderBytes + workspace.packageHeaderReceived,
           data, copyLength);
    workspace.packageHeaderReceived += copyLength;
    data += copyLength;
    length -= copyLength;
    if (workspace.packageHeaderReceived == kPackageHeaderSize &&
        !validatePackageHeader(workspace, release, message, messageCapacity)) {
      return false;
    }
  }
  return !length || processPayload(workspace, data, length, message, messageCapacity);
}

bool finishPackage(InstallWorkspace &workspace, const Release &release,
                   char *message, size_t messageCapacity) {
  if (workspace.packageReceived != release.packageSize ||
      workspace.packageHeaderReceived != kPackageHeaderSize ||
      workspace.payloadReceived != release.firmwareSize ||
      !workspace.writer || !workspace.packageShaActive ||
      !workspace.firmwareShaActive) {
    copyText(message, messageCapacity, "Downloaded firmware package was incomplete");
    return false;
  }
  if (!workspace.buildIdMatcher.seen) {
    copyText(message, messageCapacity, "Firmware does not contain the declared build ID");
    return false;
  }
  if (!workspace.distributionMatcher.seen) {
    copyText(message, messageCapacity,
             "Firmware is not an ESP PLANTS public distribution build");
    return false;
  }

  uint8_t actualPackageSha[32]{};
  uint8_t actualFirmwareSha[32]{};
  if (mbedtls_sha256_finish(&workspace.packageSha, actualPackageSha) != 0 ||
      mbedtls_sha256_finish(&workspace.firmwareSha, actualFirmwareSha) != 0) {
    copyText(message, messageCapacity, "Remote OTA SHA-256 finalization failed");
    return false;
  }
  mbedtls_sha256_free(&workspace.packageSha);
  workspace.packageShaActive = false;
  mbedtls_sha256_free(&workspace.firmwareSha);
  workspace.firmwareShaActive = false;

  if (memcmp(actualPackageSha, release.packageSha256,
             sizeof(actualPackageSha)) != 0) {
    copyText(message, messageCapacity,
             "Downloaded package SHA-256 does not match the manifest");
    return false;
  }
  if (memcmp(actualFirmwareSha, release.firmwareSha256,
             sizeof(actualFirmwareSha)) != 0 ||
      memcmp(actualFirmwareSha, workspace.packageHeader.firmwareSha256,
             sizeof(actualFirmwareSha)) != 0) {
    copyText(message, messageCapacity,
             "Downloaded firmware SHA-256 does not match the manifest");
    return false;
  }

  FlashWriterContext &writer = *workspace.writer;
  while (__atomic_load_n(&writer.readSequence, __ATOMIC_ACQUIRE) !=
         __atomic_load_n(&writer.writeSequence, __ATOMIC_ACQUIRE)) {
    if (flashWriterState(writer) == FlashWriterState::FAILED) {
      copyText(message, messageCapacity,
               writer.error[0] ? writer.error : "Internal OTA flash writer failed");
      return false;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
  }

  setFlashWriterState(writer, FlashWriterState::COMMIT_REQUESTED);
  xTaskNotifyGive(writer.writerTask);
  if (!waitForWriterState(writer, FlashWriterState::COMPLETE,
                          kFlashWriterWaitMs)) {
    copyText(message, messageCapacity,
             writer.error[0] ? writer.error : "Internal OTA flash finalization failed");
    return false;
  }
  return true;
}

void setRestartState(RestartState state, const char *message) {
  char localMessage[sizeof(restartStatusMessage)]{};
  copyText(localMessage, sizeof(localMessage), message);
  portENTER_CRITICAL(&restartMux);
  currentRestartState = state;
  memcpy(restartStatusMessage, localMessage, sizeof(restartStatusMessage));
  portEXIT_CRITICAL(&restartMux);
}

void scheduleRestart(const Release &release) {
  char localMessage[sizeof(restartStatusMessage)]{};
  snprintf(localMessage, sizeof(localMessage), "%s verified; restarting ESP PLANTS",
           release.buildId);
  const uint32_t scheduledAt = millis() + kRestartDelayMs;
  portENTER_CRITICAL(&restartMux);
  currentRestartState = RestartState::PENDING;
  memcpy(restartStatusMessage, localMessage, sizeof(restartStatusMessage));
  restartAtMs = scheduledAt;
  restartExecuteAtMs = 0;
  restartTaskHandle = nullptr;
  restartTaskCreationAttempted = false;
  __atomic_store_n(&restartLoopState, kRestartLoopWaiting, __ATOMIC_RELEASE);
  portEXIT_CRITICAL(&restartMux);
  Serial.printf("[update] verified %s (%lu bytes); restart scheduled\n",
                release.buildId, static_cast<unsigned long>(release.firmwareSize));
}

__attribute__((noinline)) bool IRAM_ATTR parkCoreOneForRestart() {
  const uint32_t enabledInterrupts = xthal_get_intenable();
  xt_ints_off(0xFFFFFFFFU);
  uint32_t expectedState = kRestartLoopWaiting;
  if (!__atomic_compare_exchange_n(&restartLoopState, &expectedState,
                                   kRestartLoopQuiesced, false,
                                   __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
    xt_ints_on(enabledInterrupts);
    return false;
  }
  for (;;) __asm__ __volatile__("nop");
}

void restartTask(void *) {
  const TickType_t waitStarted = xTaskGetTickCount();
  const TickType_t waitTicks = pdMS_TO_TICKS(kRestartLoopQuiesceTimeoutMs);
  for (;;) {
    const uint32_t state = __atomic_load_n(&restartLoopState, __ATOMIC_ACQUIRE);
    if (state == kRestartLoopQuiesced) break;
    if (state == kRestartLoopAborted) {
      restartTaskHandle = nullptr;
      vTaskDeleteWithCaps(nullptr);
      return;
    }
    if (xTaskGetTickCount() - waitStarted >= waitTicks) {
      uint32_t expectedState = kRestartLoopWaiting;
      if (__atomic_compare_exchange_n(&restartLoopState, &expectedState,
                                      kRestartLoopAborted, false,
                                      __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        restartTaskHandle = nullptr;
        setRestartState(RestartState::FAILED,
                        "Firmware verified; restart handoff failed. Power-cycle ESP PLANTS.");
        Serial.println("[update] restart stopped: Core-1 loop did not quiesce");
        vTaskDeleteWithCaps(nullptr);
        return;
      }
      continue;
    }
    vTaskDelay(1);
  }
  Serial.flush();
  esp_restart();
}

void createRestartTaskOnce() {
  if (restartTaskCreationAttempted) return;
  restartTaskCreationAttempted = true;

  const bool heapIntegrityOk = heap_caps_check_integrity_all(true);
  if (!heapIntegrityOk) {
    setRestartState(RestartState::FAILED,
                    "Firmware verified; heap integrity failed. Power-cycle ESP PLANTS.");
    return;
  }
  if (xPortGetCoreID() != kRestartLoopCore) {
    setRestartState(RestartState::FAILED,
                    "Firmware verified; restart caller core is unsafe. Power-cycle ESP PLANTS.");
    return;
  }

  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      restartTask, kRestartTaskName, kRestartTaskStackBytes, nullptr,
      kRestartTaskPriority, &restartTaskHandle, kRestartTaskCore,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    restartTaskHandle = nullptr;
    setRestartState(RestartState::FAILED,
                    "Firmware verified; automatic restart failed. Power-cycle ESP PLANTS.");
    return;
  }
  Serial.println("[update] restart task created; parking Core 1");
  Serial.flush();
  if (!parkCoreOneForRestart()) {
    Serial.println("[update] restart loop park cancelled; firmware remains verified");
  }
}

}  // namespace

Result install(const Release &release, ProgressCallback progress,
               char *message, size_t messageCapacity) {
  if (!message || messageCapacity == 0) return Result::FAILED;
  message[0] = 0;

  if (!packageLayoutValid(release.packageSize, release.firmwareSize) ||
      !boundedPrintableAscii(release.buildId, kMaxBuildIdLength) ||
      !tagValid(release.tag) || !assetNameValid(release.asset)) {
    copyText(message, messageCapacity, "Validated release metadata is incomplete");
    return Result::FAILED;
  }

  WorkspaceGuard guard;
  InstallWorkspace *workspace = guard.get();
  if (!workspace) {
    copyText(message, messageCapacity, "Remote OTA PSRAM workspace allocation failed");
    return Result::FAILED;
  }
  workspace->updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!workspace->updatePartition) {
    copyText(message, messageCapacity, "No inactive OTA partition is available");
    return Result::FAILED;
  }
  workspace->psramRing = static_cast<uint8_t *>(heap_caps_malloc(
      kOtaRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!workspace->psramRing) {
    copyText(message, messageCapacity, "OTA PSRAM ring allocation failed");
    return Result::FAILED;
  }
  if (!makeAssetUrl(release, workspace->currentUrl, sizeof(workspace->currentUrl))) {
    copyText(message, messageCapacity, "Release asset URL could not be constructed safely");
    return Result::FAILED;
  }

  mbedtls_sha256_init(&workspace->packageSha);
  if (mbedtls_sha256_starts(&workspace->packageSha, 0) != 0) {
    copyText(message, messageCapacity, "Package SHA-256 initialization failed");
    return Result::FAILED;
  }
  workspace->packageShaActive = true;

  const uint32_t absoluteDeadlineMs = millis() + kInstallTotalTimeoutMs;
  if (progress) progress(0, release.packageSize);

  for (uint8_t redirect = 0; redirect <= kMaxRedirects; ++redirect) {
    if (WiFi.status() != WL_CONNECTED) {
      copyText(message, messageCapacity, "Wi-Fi disconnected during update");
      return Result::TRANSPORT_FAILED;
    }
    if (deadlineReached(absoluteDeadlineMs)) {
      copyText(message, messageCapacity, "Remote OTA exceeded its three-minute deadline");
      return Result::TRANSPORT_FAILED;
    }

    workspace->host[0] = 0;
    if (!parseAllowedHttpsUrl(workspace->currentUrl, workspace->host,
                              sizeof(workspace->host))) {
      copyText(message, messageCapacity, "GitHub returned an unsafe release URL");
      return Result::FAILED;
    }
    workspace->headers = HttpHeaderState{};

    const uint32_t connectBudgetMs = std::min<uint32_t>(
        kHttpConnectTimeoutMs, remainingToDeadline(absoluteDeadlineMs));
    if (connectBudgetMs < kMinimumHttpBudgetMs) {
      copyText(message, messageCapacity, "Remote OTA had insufficient HTTPS time remaining");
      return Result::TRANSPORT_FAILED;
    }
    const size_t urlLength = strlen(workspace->currentUrl);
    const size_t txBufferBytes = httpTransmitBufferBytes(urlLength);
    if (!txBufferBytes) {
      copyText(message, messageCapacity, "Release URL exceeded transport limits");
      return Result::FAILED;
    }

    esp_http_client_config_t config{};
    config.url = workspace->currentUrl;
    config.user_agent = kUserAgent;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = static_cast<int>(connectBudgetMs);
    config.disable_auto_redirect = true;
    config.max_redirection_count = 0;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.buffer_size = 2048;
    config.buffer_size_tx = static_cast<int>(txBufferBytes);
    config.keep_alive_enable = false;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.event_handler = httpEventHandler;
    config.user_data = &workspace->headers;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      copyText(message, messageCapacity, "GitHub HTTPS client allocation failed");
      return Result::TRANSPORT_FAILED;
    }
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    bool opened = false;
    const esp_err_t openResult = esp_http_client_open(client, 0);
    if (openResult != ESP_OK) {
      snprintf(message, messageCapacity, "GitHub HTTPS open failed: %s",
               esp_err_to_name(openResult));
      releaseHttpClient(client, false);
      return Result::TRANSPORT_FAILED;
    }
    opened = true;
    const int64_t fetchedLength = esp_http_client_fetch_headers(client);
    if (workspace->headers.failure != HeaderFailure::NONE || fetchedLength < 0) {
      const bool transportFailure =
          workspace->headers.failure == HeaderFailure::NONE;
      if (!transportFailure) {
        copyText(message, messageCapacity,
                 headerFailureMessage(workspace->headers.failure));
      } else {
        copyText(message, messageCapacity, "GitHub release header fetch failed");
      }
      releaseHttpClient(client, opened);
      return transportFailure ? Result::TRANSPORT_FAILED : Result::FAILED;
    }

    const int statusCode = esp_http_client_get_status_code(client);
    const bool redirectStatus = statusCode == 301 || statusCode == 302 ||
                                statusCode == 303 || statusCode == 307 ||
                                statusCode == 308;
    if (redirectStatus) {
      if (redirect >= kMaxRedirects || !workspace->headers.location[0]) {
        copyText(message, messageCapacity, "GitHub release redirect policy rejected response");
        releaseHttpClient(client, opened);
        return Result::FAILED;
      }
      workspace->redirectHost[0] = 0;
      if (!parseAllowedHttpsUrl(workspace->headers.location, workspace->redirectHost,
                                sizeof(workspace->redirectHost))) {
        copyText(message, messageCapacity, "GitHub release redirect host was rejected");
        releaseHttpClient(client, opened);
        return Result::FAILED;
      }
      copyText(workspace->currentUrl, sizeof(workspace->currentUrl),
               workspace->headers.location);
      releaseHttpClient(client, opened);
      continue;
    }

    if (statusCode != 200) {
      snprintf(message, messageCapacity, "GitHub release asset returned HTTP %d", statusCode);
      releaseHttpClient(client, opened);
      return Result::FAILED;
    }
    const bool chunked = esp_http_client_is_chunked_response(client);
    if (!framingIsUnambiguous(workspace->headers.contentLengthSeen,
                              workspace->headers.transferEncodingSeen,
                              workspace->headers.chunkedOnly, chunked)) {
      copyText(message, messageCapacity, "GitHub release response framing was ambiguous");
      releaseHttpClient(client, opened);
      return Result::FAILED;
    }
    if (workspace->headers.contentLengthSeen &&
        workspace->headers.contentLength != release.packageSize) {
      copyText(message, messageCapacity,
               "GitHub release package length does not match the manifest");
      releaseHttpClient(client, opened);
      return Result::FAILED;
    }

    esp_http_client_set_timeout_ms(client, kHttpBodyIdleTimeoutMs);
    uint32_t lastProgressMs = millis();
    bool readFailed = false;
    bool transportFailure = false;
    while (workspace->packageReceived < release.packageSize) {
      if (WiFi.status() != WL_CONNECTED) {
        copyText(message, messageCapacity, "Wi-Fi disconnected during update");
        readFailed = true;
        transportFailure = true;
        break;
      }
      if (deadlineReached(absoluteDeadlineMs)) {
        copyText(message, messageCapacity, "Remote OTA exceeded its three-minute deadline");
        readFailed = true;
        transportFailure = true;
        break;
      }
      if (millis() - lastProgressMs >= kHttpBodyIdleTimeoutMs) {
        copyText(message, messageCapacity, "GitHub release download stalled for 15 seconds");
        readFailed = true;
        transportFailure = true;
        break;
      }

      const size_t remaining = release.packageSize - workspace->packageReceived;
      const int requestBytes = static_cast<int>(
          std::min<size_t>(remaining, kDownloadBufferBytes));
      const int bytesRead = esp_http_client_read(
          client, reinterpret_cast<char *>(workspace->downloadBuffer), requestBytes);
      if (bytesRead > 0) {
        if (!processPackageBytes(*workspace, release, workspace->downloadBuffer,
                                 static_cast<size_t>(bytesRead), message,
                                 messageCapacity)) {
          readFailed = true;
          break;
        }
        lastProgressMs = millis();
        if (progress &&
            (workspace->packageReceived - workspace->lastProgressBytes >=
                 kProgressGranularityBytes ||
             workspace->packageReceived == release.packageSize)) {
          workspace->lastProgressBytes = workspace->packageReceived;
          progress(workspace->packageReceived, release.packageSize);
        }
        vTaskDelay(1);
      } else if (bytesRead == 0) {
        if (esp_http_client_is_complete_data_received(client)) break;
        copyText(message, messageCapacity, "GitHub release download ended early");
        readFailed = true;
        transportFailure = true;
        break;
      } else {
        const int socketError = esp_http_client_get_errno(client);
        if ((bytesRead == -ESP_ERR_HTTP_EAGAIN || socketError == EAGAIN ||
             socketError == EWOULDBLOCK || socketError == ETIMEDOUT) &&
            !deadlineReached(absoluteDeadlineMs)) {
          continue;
        }
        snprintf(message, messageCapacity,
                 "GitHub release body read failed: result=%d errno=%d",
                 bytesRead, socketError);
        readFailed = true;
        transportFailure = true;
        break;
      }
    }

    const bool complete = esp_http_client_is_complete_data_received(client);
    releaseHttpClient(client, opened);
    if (readFailed || !complete || workspace->packageReceived != release.packageSize) {
      if (!message[0]) copyText(message, messageCapacity, "GitHub release download was incomplete");
      return transportFailure || !complete
                 ? Result::TRANSPORT_FAILED
                 : Result::FAILED;
    }
    if (!finishPackage(*workspace, release, message, messageCapacity)) {
      return Result::FAILED;
    }
    scheduleRestart(release);
    copyText(message, messageCapacity, "Firmware verified; ESP PLANTS restart is pending");
    return Result::RESTART_PENDING;
  }

  copyText(message, messageCapacity, "GitHub release redirect count exceeded limit");
  return Result::FAILED;
}

void serviceRestart() {
  const uint32_t now = millis();
  bool beginSettle = false;
  bool beginRestart = false;
  portENTER_CRITICAL(&restartMux);
  if (currentRestartState == RestartState::PENDING) {
    if (restartAtMs && static_cast<int32_t>(now - restartAtMs) >= 0) {
      restartAtMs = 0;
      restartExecuteAtMs = now + kRestartSettleMs;
      beginSettle = true;
    } else if (restartExecuteAtMs &&
               static_cast<int32_t>(now - restartExecuteAtMs) >= 0) {
      restartExecuteAtMs = 0;
      beginRestart = true;
    }
  }
  portEXIT_CRITICAL(&restartMux);
  if (beginSettle) Serial.println("[update] restart shutdown: settling network tasks");
  if (beginRestart) createRestartTaskOnce();
}

RestartState restartState() {
  portENTER_CRITICAL(&restartMux);
  const RestartState state = currentRestartState;
  portEXIT_CRITICAL(&restartMux);
  return state;
}

void copyRestartMessage(char *destination, size_t capacity) {
  if (!destination || capacity == 0) return;
  portENTER_CRITICAL(&restartMux);
  copyText(destination, capacity, restartStatusMessage);
  portEXIT_CRITICAL(&restartMux);
}

}  // namespace espplants_ota_installer
