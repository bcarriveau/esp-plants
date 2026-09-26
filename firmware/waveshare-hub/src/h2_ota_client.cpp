#include "h2_ota_client.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>
#include <strings.h>

#include "plantlink.h"
#include "plantlink_ota.h"
#include "update_policy.h"

namespace espplants_h2_ota {
namespace {

constexpr char kReleasePrefix[] =
    "https://github.com/bcarriveau/esp-plants/releases/download/";
constexpr char kH2Prefix[] = "ESPPLANTS-H2-";
constexpr size_t kMaxH2Bytes = 0xE0000u;
constexpr uint32_t kAckTimeoutMs = 2500;
constexpr uint32_t kRebootTimeoutMs = 15000;
constexpr uint32_t kHelloTimeoutMs = 1200;

volatile uint8_t lastStatus = 0;
volatile uint8_t lastError = 0;
volatile uint32_t nextOffset = 0;
volatile uint32_t eventCounter = 0;
char lastHello[96]{};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint16_t sequence = 0x5000;

void copyText(char *destination, size_t capacity, const char *source) {
  if (destination && capacity) snprintf(destination, capacity, "%s", source ? source : "");
}

void observer(const plantlink::Frame &frame) {
  portENTER_CRITICAL(&mux);
  if (frame.type == plantlink::MessageType::H2OtaStatus && frame.payloadLength == 6) {
    lastStatus = frame.payload[0];
    lastError = frame.payload[1];
    nextOffset = plantlink::getU32LE(frame.payload + 2);
    ++eventCounter;
  } else if (frame.type == plantlink::MessageType::HelloAck) {
    const size_t length = std::min<size_t>(frame.payloadLength, sizeof(lastHello) - 1);
    memcpy(lastHello, frame.payload, length);
    lastHello[length] = 0;
    ++eventCounter;
  }
  portEXIT_CRITICAL(&mux);
}

void sendFrame(plantlink::MessageType type, const uint8_t *payload, uint16_t length) {
  uint8_t encoded[plantlink::kMaxEncodedBytes]{};
  const size_t encodedLength = plantlink::encodeFrame(
      type, plantlink::FlagNone, sequence++, payload, length, encoded, sizeof(encoded));
  if (encodedLength) Serial0.write(encoded, encodedLength);
}

uint32_t counter() {
  portENTER_CRITICAL(&mux);
  const uint32_t value = eventCounter;
  portEXIT_CRITICAL(&mux);
  return value;
}

bool waitStatus(uint32_t before, uint32_t timeout, uint8_t &statusValue,
                uint8_t &errorValue, uint32_t &nextValue) {
  const uint32_t started = millis();
  while (millis() - started < timeout) {
    portENTER_CRITICAL(&mux);
    const uint32_t now = eventCounter;
    statusValue = lastStatus;
    errorValue = lastError;
    nextValue = nextOffset;
    portEXIT_CRITICAL(&mux);
    if (now != before && statusValue) return true;
    delay(2);
  }
  return false;
}

bool allowedUrl(const char *url) {
  char host[96]{};
  return espplants_update_policy::parseAllowedHttpsUrl(url, host, sizeof(host));
}

bool releaseMetadataValid(const espplants_ota_installer::Release &release) {
  if (!release.tag[0] || !release.h2Version[0] || !release.h2Asset[0] ||
      !release.h2BuildId[0]) {
    return false;
  }
  char expectedBuild[96]{};
  const int written = snprintf(expectedBuild, sizeof(expectedBuild), "%s%s",
                               kH2Prefix, release.h2Version);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(expectedBuild) ||
      strcmp(release.h2BuildId, expectedBuild) != 0) {
    return false;
  }
  int versionComparison = 0;
  if (!espplants_update_policy::compareSemanticVersions(
          release.h2Version, release.h2Version, versionComparison)) {
    return false;
  }
  if (!espplants_update_policy::h2AssetNameValid(release.h2Asset)) return false;
  if (release.h2FirmwareSize < 65536u || release.h2FirmwareSize > kMaxH2Bytes) return false;
  return true;
}

TargetState classifyTargetHello(const char *hello,
                                const espplants_ota_installer::Release &release) {
  return targetStateForIdentity(hello, release.h2Version, release.h2BuildId);
}

TargetState probeTarget(const espplants_ota_installer::Release &release) {
  if (!releaseMetadataValid(release)) return TargetState::UNKNOWN;

  portENTER_CRITICAL(&mux);
  lastHello[0] = 0;
  portEXIT_CRITICAL(&mux);

  uint8_t helloPayload[8]{};
  plantlink::putU32LE(helloPayload, millis());
  const uint32_t before = counter();
  sendFrame(plantlink::MessageType::Hello, helloPayload, sizeof(helloPayload));

  const uint32_t started = millis();
  while (millis() - started < kHelloTimeoutMs) {
    char hello[sizeof(lastHello)]{};
    uint32_t events = 0;
    portENTER_CRITICAL(&mux);
    memcpy(hello, lastHello, sizeof(hello));
    events = eventCounter;
    portEXIT_CRITICAL(&mux);

    if (events != before && strncmp(hello, kH2Prefix, strlen(kH2Prefix)) == 0) {
      return classifyTargetHello(hello, release);
    }
    delay(5);
  }
  return TargetState::UNKNOWN;
}

struct AssetHeaderState {
  size_t totalBytes = 0;
  bool invalid = false;
  char location[espplants_update_policy::kMaxRedirectUrlLength + 1]{};
};

esp_err_t assetHeaderEvent(esp_http_client_event_t *event) {
  if (!event || !event->user_data) return ESP_OK;
  AssetHeaderState &state = *static_cast<AssetHeaderState *>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key ||
      !event->header_value) {
    return state.invalid ? ESP_FAIL : ESP_OK;
  }

  size_t updated = state.totalBytes;
  if (!espplants_update_policy::accumulateHeaderBytes(
          state.totalBytes, strlen(event->header_key), strlen(event->header_value), updated)) {
    state.invalid = true;
    return ESP_FAIL;
  }
  state.totalBytes = updated;

  if (strcasecmp(event->header_key, "Location") == 0) {
    const char *value = event->header_value;
    while (*value == ' ' || *value == '\t') ++value;
    if (!espplants_update_policy::redirectUrlLengthValid(value)) {
      state.invalid = true;
      return ESP_FAIL;
    }
    copyText(state.location, sizeof(state.location), value);
  }
  return ESP_OK;
}

bool getAsset(const char *url, uint8_t *destination, size_t capacity, size_t &out,
              char *message, size_t messageCapacity) {
  char current[espplants_update_policy::kMaxRedirectUrlLength + 1]{};
  copyText(current, sizeof(current), url);

  for (int redirects = 0; redirects <= 3; ++redirects) {
    if (!allowedUrl(current)) {
      copyText(message, messageCapacity, "H2 release URL was rejected");
      return false;
    }

    AssetHeaderState headers{};
    esp_http_client_config_t config{};
    config.url = current;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.disable_auto_redirect = true;
    config.max_redirection_count = 0;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    config.keep_alive_enable = false;
    config.event_handler = assetHeaderEvent;
    config.user_data = &headers;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      copyText(message, messageCapacity, "H2 HTTPS client allocation failed");
      return false;
    }
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");
    if (esp_http_client_open(client, 0) != ESP_OK) {
      esp_http_client_cleanup(client);
      copyText(message, messageCapacity, "H2 HTTPS open failed");
      return false;
    }

    const int64_t headerLength = esp_http_client_fetch_headers(client);
    const int statusCode = esp_http_client_get_status_code(client);
    if (headerLength < 0 || headers.invalid) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      copyText(message, messageCapacity, "H2 release headers were invalid");
      return false;
    }

    if (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
        statusCode == 307 || statusCode == 308) {
      const bool rejected = redirects == 3 || !headers.location[0] ||
                            !allowedUrl(headers.location);
      if (!rejected) copyText(current, sizeof(current), headers.location);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (rejected) {
        copyText(message, messageCapacity, "H2 release redirect was rejected");
        return false;
      }
      continue;
    }

    if (statusCode != 200) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      snprintf(message, messageCapacity, "H2 release asset returned HTTP %d", statusCode);
      return false;
    }

    out = 0;
    uint32_t idle = millis();
    while (true) {
      if (out == capacity) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        copyText(message, messageCapacity, "H2 release asset exceeded size limit");
        return false;
      }
      const int read = esp_http_client_read(
          client, reinterpret_cast<char *>(destination + out),
          std::min<size_t>(4096, capacity - out));
      if (read > 0) {
        out += read;
        idle = millis();
        continue;
      }
      if (read == 0 && esp_http_client_is_complete_data_received(client)) break;
      if (millis() - idle > 15000) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        copyText(message, messageCapacity, "H2 release download stalled");
        return false;
      }
      delay(1);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return true;
  }

  copyText(message, messageCapacity, "H2 redirect limit exceeded");
  return false;
}

}  // namespace

void observePlantLinkFrame(const void *frame) {
  if (frame) observer(*static_cast<const plantlink::Frame *>(frame));
}

TargetState targetStateForRelease(const espplants_ota_installer::Release &release) {
  plantlink::FrameObserver previous = plantlink::frameObserver();
  plantlink::setFrameObserver(observer);
  const TargetState state = probeTarget(release);
  plantlink::setFrameObserver(previous);
  return state;
}

Result updateForRelease(const espplants_ota_installer::Release &release,
                        espplants_ota_installer::ProgressCallback progress,
                        char *message, size_t messageCapacity) {
  if (!releaseMetadataValid(release)) {
    copyText(message, messageCapacity, "H2 release metadata was invalid");
    return Result::FAILED;
  }

  plantlink::FrameObserver previous = plantlink::frameObserver();
  plantlink::setFrameObserver(observer);
  const TargetState initialState = probeTarget(release);
  if (initialState == TargetState::MATCH) {
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 already matches release");
    return Result::OK;
  }
  if (initialState == TargetState::NEWER_THAN_RELEASE) {
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 is newer than release target; downgrade skipped");
    return Result::OK;
  }
  if (initialState == TargetState::UNKNOWN) {
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity,
             "H2 identity is unknown; automatic H2 update blocked");
    return Result::FAILED;
  }

  uint8_t *image = static_cast<uint8_t *>(
      heap_caps_malloc(kMaxH2Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!image) {
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 OTA PSRAM allocation failed");
    return Result::FAILED;
  }

  char url[512]{};
  snprintf(url, sizeof(url), "%s%s/%s", kReleasePrefix, release.tag, release.h2Asset);
  size_t imageLength = 0;
  if (!getAsset(url, image, kMaxH2Bytes, imageLength, message, messageCapacity)) {
    heap_caps_free(image);
    plantlink::setFrameObserver(previous);
    return Result::FAILED;
  }
  if (imageLength != release.h2FirmwareSize) {
    heap_caps_free(image);
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 firmware size did not match manifest");
    return Result::FAILED;
  }

  uint8_t actualSha[32]{};
  mbedtls_sha256(image, imageLength, actualSha, 0);
  if (memcmp(actualSha, release.h2FirmwareSha256, sizeof(actualSha)) != 0) {
    heap_caps_free(image);
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 firmware SHA-256 mismatch");
    return Result::FAILED;
  }

  // Explicitly authorize exactly the next H2OtaBegin frame immediately before
  // the privileged operation. Normal startup Hello frames carry zero here and
  // never arm OTA. This is a physical-link/session confirmation, not crypto.
  uint8_t helloPayload[8]{};
  uint32_t before = counter();
  plantlink::putU32LE(helloPayload, millis());
  plantlink::putU32LE(helloPayload + 4, plantlink_ota::kAuthorizeIntentMagic);
  sendFrame(plantlink::MessageType::Hello, helloPayload, sizeof(helloPayload));
  uint32_t started = millis();
  while (millis() - started < kHelloTimeoutMs) {
    if (counter() != before) break;
    delay(2);
  }

  uint8_t begin[plantlink_ota::kBeginBytes]{};
  begin[0] = plantlink::kProtocolVersion;
  plantlink::putU32LE(begin + 1, imageLength);
  memcpy(begin + 5, release.h2FirmwareSha256, 32);
  snprintf(reinterpret_cast<char *>(begin + 37), 96, "%s", release.h2BuildId);

  before = counter();
  sendFrame(plantlink::MessageType::H2OtaBegin, begin, sizeof(begin));
  uint8_t statusValue = 0;
  uint8_t errorValue = 0;
  uint32_t next = 0;
  if (!waitStatus(before, kAckTimeoutMs, statusValue, errorValue, next)) {
    heap_caps_free(image);
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 OTA did not answer");
    return Result::FAILED;
  }
  if (statusValue == uint8_t(plantlink_ota::Status::Error)) {
    heap_caps_free(image);
    plantlink::setFrameObserver(previous);
    snprintf(message, messageCapacity, "H2 OTA begin failed (%u)", errorValue);
    return Result::FAILED;
  }

  for (uint32_t offset = 0; offset < imageLength;) {
    const size_t chunkLength =
        std::min<size_t>(plantlink_ota::kChunkDataBytes, imageLength - offset);
    uint8_t chunk[4 + plantlink_ota::kChunkDataBytes]{};
    plantlink::putU32LE(chunk, offset);
    memcpy(chunk + 4, image + offset, chunkLength);

    bool acknowledged = false;
    for (int retry = 0; retry < 3 && !acknowledged; ++retry) {
      before = counter();
      sendFrame(plantlink::MessageType::H2OtaChunk, chunk, 4 + chunkLength);
      if (waitStatus(before, kAckTimeoutMs, statusValue, errorValue, next) &&
          statusValue != uint8_t(plantlink_ota::Status::Error) &&
          next >= offset + chunkLength) {
        acknowledged = true;
      }
    }
    if (!acknowledged) {
      sendFrame(plantlink::MessageType::H2OtaAbort, nullptr, 0);
      heap_caps_free(image);
      plantlink::setFrameObserver(previous);
      snprintf(message, messageCapacity, "H2 OTA transfer failed at %lu",
               static_cast<unsigned long>(offset));
      return Result::FAILED;
    }
    offset = next;
    if (progress) progress(offset, imageLength);
  }

  heap_caps_free(image);
  before = counter();
  sendFrame(plantlink::MessageType::H2OtaEnd, nullptr, 0);
  if (!waitStatus(before, kAckTimeoutMs * 2, statusValue, errorValue, next) ||
      statusValue == uint8_t(plantlink_ota::Status::Error)) {
    plantlink::setFrameObserver(previous);
    copyText(message, messageCapacity, "H2 final validation failed");
    return Result::FAILED;
  }

  plantlink::putU32LE(helloPayload + 4, 0);
  started = millis();
  while (millis() - started < kRebootTimeoutMs) {
    plantlink::putU32LE(helloPayload, millis());
    sendFrame(plantlink::MessageType::Hello, helloPayload, sizeof(helloPayload));
    delay(250);

    char hello[sizeof(lastHello)]{};
    portENTER_CRITICAL(&mux);
    memcpy(hello, lastHello, sizeof(hello));
    portEXIT_CRITICAL(&mux);
    if (strcmp(hello, release.h2BuildId) == 0) {
      plantlink::setFrameObserver(previous);
      copyText(message, messageCapacity, "H2 updated and reboot-confirmed");
      return Result::OK;
    }
  }

  plantlink::setFrameObserver(previous);
  copyText(message, messageCapacity, "H2 updated but reboot confirmation timed out");
  return Result::FAILED;
}

}  // namespace espplants_h2_ota
