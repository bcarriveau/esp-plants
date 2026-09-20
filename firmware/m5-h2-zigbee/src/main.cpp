#include <Arduino.h>
#include <Zigbee.h>

#include "aps/esp_zigbee_aps.h"
#include "esp_zigbee_core.h"
#include "nwk/esp_zigbee_nwk.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "plantlink.h"
#include "zg303z_tuya.h"

// Arduino-ESP32 registers this APS handler internally for binding-table housekeeping.
// Our observer calls it first so adding ZG-303Z raw capture does not remove core behavior.
bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t ind);

namespace {

constexpr uint8_t kGatewayEndpoint = 1;
constexpr uint32_t kPlantLinkBaud = 115200;
// M5Stack's current Gateway H2 NCP documentation maps Grove UART to H2 RX=23/TX=24.
constexpr int kPlantLinkRxPin = 23;
constexpr int kPlantLinkTxPin = 24;
constexpr size_t kMaxSensors = 16;
constexpr size_t kCapturedApsBytes = 128;
constexpr uint32_t kStatusIntervalMs = 1500;

HardwareSerial PlantUart(1);
ZigbeeGateway zbGateway(kGatewayEndpoint);
plantlink::Decoder plantDecoder;
uint16_t nextSequence = 1;
uint32_t lastStatusMs = 0;
uint32_t permitJoinUntilMs = 0;
bool zigbeeReady = false;

struct ApsEvent {
  uint16_t shortAddress = 0xffff;
  uint8_t ieee[8]{};
  uint8_t sourceEndpoint = 0;
  uint16_t clusterId = 0;
  uint16_t profileId = 0;
  uint8_t lqi = 0;
  int8_t rssi = plantlink::kRssiUnavailableDbm;
  uint16_t originalLength = 0;
  uint8_t capturedLength = 0;
  uint8_t data[kCapturedApsBytes]{};
};

QueueHandle_t apsQueue = nullptr;

struct SensorState {
  bool used = false;
  uint8_t ieee[8]{};
  uint16_t shortAddress = 0xffff;
  uint16_t fieldFlags = 0;
  int16_t temperatureCentiC = 0;
  uint16_t humidityCentiPct = 0;
  uint8_t soilMoisturePct = 0;
  uint8_t batteryPct = 0;
  uint8_t waterWarning = 0;
  uint8_t lqi = 0;
  int8_t rssi = plantlink::kRssiUnavailableDbm;
  uint32_t lastSeenMs = 0;
};

SensorState sensors[kMaxSensors];
uint8_t sensorCount = 0;

bool ieeeIsZero(const uint8_t ieee[8]) {
  for (size_t i = 0; i < 8; ++i) {
    if (ieee[i] != 0) return false;
  }
  return true;
}

bool ieeeEqual(const uint8_t a[8], const uint8_t b[8]) {
  return memcmp(a, b, 8) == 0;
}

void sendFrame(plantlink::MessageType type, const uint8_t *payload = nullptr,
               uint16_t payloadLength = 0, uint8_t flags = plantlink::FlagNone) {
  uint8_t encoded[plantlink::kMaxEncodedBytes]{};
  const size_t length = plantlink::encodeFrame(type, flags, nextSequence++, payload,
                                                payloadLength, encoded, sizeof(encoded));
  if (length) PlantUart.write(encoded, length);
}

uint8_t currentChannel() {
  if (!zigbeeReady) return 0;
  return static_cast<uint8_t>(esp_zb_get_current_channel());
}

uint8_t permitJoinRemaining() {
  if (!permitJoinUntilMs) return 0;
  const uint32_t now = millis();
  if (static_cast<int32_t>(permitJoinUntilMs - now) <= 0) {
    permitJoinUntilMs = 0;
    return 0;
  }
  const uint32_t remainingMs = permitJoinUntilMs - now;
  const uint32_t seconds = (remainingMs + 999u) / 1000u;
  return seconds > 255u ? 255u : static_cast<uint8_t>(seconds);
}

void sendNetworkStatus() {
  uint8_t payload[4]{};
  payload[0] = zigbeeReady ? 1 : 0;
  payload[1] = currentChannel();
  payload[2] = sensorCount;
  payload[3] = permitJoinRemaining();
  sendFrame(plantlink::MessageType::NetworkStatus, payload, sizeof(payload));
}

void sendHeartbeat() {
  uint8_t payload[8]{};
  plantlink::putU32LE(payload, millis());
  uint32_t caps = plantlink::CapabilityZigbeeCoordinator |
                  plantlink::CapabilityZg303zDecoder |
                  plantlink::CapabilityRawZigbeeLog;
  plantlink::putU32LE(payload + 4, caps);
  sendFrame(plantlink::MessageType::Heartbeat, payload, sizeof(payload));
}

void sendHelloAck() {
  static constexpr char kBuild[] = "m5-h2-zigbee/0.1-dev";
  sendFrame(plantlink::MessageType::HelloAck,
            reinterpret_cast<const uint8_t *>(kBuild), sizeof(kBuild) - 1,
            plantlink::FlagResponse);
  sendNetworkStatus();
}

void sendDeviceJoined(const SensorState &sensor) {
  uint8_t payload[10]{};
  memcpy(payload, sensor.ieee, 8);
  plantlink::putU16LE(payload + 8, sensor.shortAddress);
  sendFrame(plantlink::MessageType::DeviceJoined, payload, sizeof(payload));
}

void sendSensorReport(const SensorState &sensor) {
  plantlink::SensorReportData report;
  memcpy(report.ieee, sensor.ieee, 8);
  report.shortAddress = sensor.shortAddress;
  report.fieldFlags = sensor.fieldFlags;
  report.temperatureCentiC = sensor.temperatureCentiC;
  report.humidityCentiPct = sensor.humidityCentiPct;
  report.soilMoisturePct = sensor.soilMoisturePct;
  report.batteryPct = sensor.batteryPct;
  report.waterWarning = sensor.waterWarning;
  report.lqi = sensor.lqi;
  report.rssiDbm = sensor.rssi;

  uint8_t payload[plantlink::kSensorReportPayloadBytes]{};
  const size_t length = plantlink::serializeSensorReport(report, payload, sizeof(payload));
  if (length) sendFrame(plantlink::MessageType::SensorReport, payload, length);
}

void sendRawEvent(const ApsEvent &event) {
  // Diagnostic payload: ieee[8], short[2], cluster[2], endpoint[1], lqi[1],
  // rssi[1], original_length[2], captured raw bytes...
  uint8_t payload[plantlink::kMaxPayloadBytes]{};
  size_t offset = 0;
  memcpy(payload + offset, event.ieee, 8);
  offset += 8;
  plantlink::putU16LE(payload + offset, event.shortAddress);
  offset += 2;
  plantlink::putU16LE(payload + offset, event.clusterId);
  offset += 2;
  payload[offset++] = event.sourceEndpoint;
  payload[offset++] = event.lqi;
  payload[offset++] = static_cast<uint8_t>(event.rssi);
  plantlink::putU16LE(payload + offset, event.originalLength);
  offset += 2;
  const size_t available = plantlink::kMaxPayloadBytes - offset;
  const size_t copyLen = event.capturedLength < available ? event.capturedLength : available;
  memcpy(payload + offset, event.data, copyLen);
  offset += copyLen;
  sendFrame(plantlink::MessageType::RawZigbeeEvent, payload, static_cast<uint16_t>(offset));
}

SensorState *findOrCreateSensor(const ApsEvent &event, bool &created) {
  created = false;

  if (!ieeeIsZero(event.ieee)) {
    for (auto &sensor : sensors) {
      if (sensor.used && ieeeEqual(sensor.ieee, event.ieee)) {
        sensor.shortAddress = event.shortAddress;
        return &sensor;
      }
    }
  }

  // If IEEE lookup failed, allow an existing short address to keep collecting
  // debug data, but do not create a permanent identity from a short address.
  for (auto &sensor : sensors) {
    if (sensor.used && sensor.shortAddress == event.shortAddress) {
      if (!ieeeIsZero(event.ieee)) memcpy(sensor.ieee, event.ieee, 8);
      return &sensor;
    }
  }

  if (ieeeIsZero(event.ieee)) return nullptr;

  for (auto &sensor : sensors) {
    if (!sensor.used) {
      sensor = SensorState{};
      sensor.used = true;
      memcpy(sensor.ieee, event.ieee, 8);
      sensor.shortAddress = event.shortAddress;
      created = true;
      if (sensorCount < 255) ++sensorCount;
      return &sensor;
    }
  }
  return nullptr;
}

void applyNormalized(SensorState &sensor, const zg303z::NormalizedUpdate &update) {
  if (update.hasTemperature) {
    sensor.temperatureCentiC = update.temperatureCentiC;
    sensor.fieldFlags |= plantlink::SensorHasTemperature;
  }
  if (update.hasHumidity) {
    sensor.humidityCentiPct = update.humidityCentiPct;
    sensor.fieldFlags |= plantlink::SensorHasHumidity;
  }
  if (update.hasSoilMoisture) {
    sensor.soilMoisturePct = update.soilMoisturePct;
    sensor.fieldFlags |= plantlink::SensorHasSoilMoisture;
  }
  if (update.hasBattery) {
    sensor.batteryPct = update.batteryPct;
    sensor.fieldFlags |= plantlink::SensorHasBattery;
  }
  if (update.hasWaterWarning) {
    sensor.waterWarning = update.waterWarning ? 1 : 0;
    sensor.fieldFlags |= plantlink::SensorHasWaterWarning;
  }
}

void printHex(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) Serial.printf("%02X", data[i]);
}

void processApsEvent(const ApsEvent &event) {
  char ieeeText[24]{};
  plantlink::formatIeee(event.ieee, ieeeText, sizeof(ieeeText));
  Serial.printf("[aps] src=%s short=0x%04X ep=%u cluster=0x%04X lqi=%u rssi=%d len=%u data=",
                ieeeText, event.shortAddress, event.sourceEndpoint, event.clusterId,
                event.lqi, event.rssi, event.originalLength);
  printHex(event.data, event.capturedLength);
  Serial.println();

  bool created = false;
  SensorState *sensor = findOrCreateSensor(event, created);
  if (sensor) {
    sensor->lqi = event.lqi;
    sensor->rssi = event.rssi;
    sensor->lastSeenMs = millis();
    if (created) {
      Serial.printf("[zigbee] first traffic from new IEEE device %s\n", ieeeText);
      sendDeviceJoined(*sensor);
      sendNetworkStatus();
    }
  }

  zg303z::NormalizedUpdate normalized;
  bool decoded = false;

  if (event.clusterId == zg303z::kTuyaClusterId) {
    zg303z::TuyaFrameInfo info;
    decoded = zg303z::decodeTuyaFrame(
        event.data, event.capturedLength, info, normalized,
        [&](const zg303z::Datapoint &dp) {
          Serial.printf("[tuya] dp=%u type=0x%02X len=%u", dp.id, dp.type, dp.length);
          if (dp.numericValid) Serial.printf(" value=%ld", static_cast<long>(dp.numeric));
          if (dp.metric == zg303z::Metric::Unknown) Serial.print(" UNKNOWN");
          Serial.println();
        });
  } else if (event.clusterId == zg303z::kTemperatureClusterId ||
             event.clusterId == zg303z::kHumidityClusterId ||
             event.clusterId == zg303z::kPowerConfigClusterId) {
    decoded = zg303z::decodeStandardReport(event.clusterId, event.data,
                                            event.capturedLength, normalized);
  }

  if (sensor && decoded) {
    applyNormalized(*sensor, normalized);
    sendSensorReport(*sensor);
  }

  // Keep raw traffic observable during bring-up, especially unknown Tuya DPs.
  if (!decoded || event.clusterId == zg303z::kTuyaClusterId) sendRawEvent(event);
}

bool apsDataHandler(esp_zb_apsde_data_ind_t ind) {
  // Preserve the Arduino Zigbee core's own APS bookkeeping before observing
  // the same packet. The callback returns false so the stack still processes it.
  (void)zb_apsde_data_indication_handler(ind);
  if (!apsQueue) return false;

  ApsEvent event;
  event.shortAddress = ind.src_short_addr;
  event.sourceEndpoint = ind.src_endpoint;
  event.clusterId = ind.cluster_id;
  event.profileId = ind.profile_id;
  event.lqi = ind.lqi;
  // Arduino-ESP32 3.3.7's APS indication exposes LQI but no RSSI field.
  // Do not derive fake dBm from LQI; -128 means RSSI unavailable on PlantLink.
  event.rssi = plantlink::kRssiUnavailableDbm;
  event.originalLength = ind.asdu_length;
  event.capturedLength = ind.asdu_length > kCapturedApsBytes
                             ? static_cast<uint8_t>(kCapturedApsBytes)
                             : static_cast<uint8_t>(ind.asdu_length);
  if (event.capturedLength && ind.asdu) memcpy(event.data, ind.asdu, event.capturedLength);

  // Resolve permanent IEEE identity while executing in Zigbee stack context.
  esp_zb_ieee_addr_t ieee{};
  if (esp_zb_ieee_address_by_short(ind.src_short_addr, ieee) == ESP_OK) {
    memcpy(event.ieee, ieee, 8);
  }

  xQueueSend(apsQueue, &event, 0);

  // false = observe the frame but allow normal Zigbee stack processing to continue.
  return false;
}

void handlePlantFrame(const plantlink::Frame &frame) {
  switch (frame.type) {
    case plantlink::MessageType::Hello:
      sendHelloAck();
      break;

    case plantlink::MessageType::PermitJoin: {
      if (frame.payloadLength < 1) break;
      uint8_t seconds = frame.payload[0];
      if (seconds > 180) seconds = 180;
      if (seconds == 0) {
        Zigbee.closeNetwork();
        permitJoinUntilMs = 0;
        Serial.println("[zigbee] permit join closed");
      } else {
        Zigbee.openNetwork(seconds);
        permitJoinUntilMs = millis() + static_cast<uint32_t>(seconds) * 1000u;
        Serial.printf("[zigbee] permit join open for %u seconds\n", seconds);
      }
      sendNetworkStatus();
      break;
    }

    case plantlink::MessageType::FactoryResetNetwork:
      // Destructive reset is intentionally NOT wired to the first UI build.
      // A future implementation will require an explicit confirmation token.
      Serial.println("[zigbee] ignored unconfirmed factory reset command");
      break;

    default:
      break;
  }
}

void servicePlantLink() {
  plantlink::Frame frame;
  while (PlantUart.available()) {
    if (plantDecoder.feed(static_cast<uint8_t>(PlantUart.read()), frame)) {
      handlePlantFrame(frame);
    }
  }

  const uint32_t now = millis();
  if (now - lastStatusMs >= kStatusIntervalMs) {
    lastStatusMs = now;
    zigbeeReady = Zigbee.connected();
    sendHeartbeat();
    sendNetworkStatus();
  }
}

void printUsbConsoleHelp() {
  Serial.println("[console] commands:");
  Serial.println("  p = open Zigbee pairing for 120 seconds");
  Serial.println("  c = close Zigbee pairing");
  Serial.println("  s = print Zigbee/network/sensor status");
  Serial.println("  h or ? = show this help");
}

void printUsbConsoleStatus() {
  zigbeeReady = Zigbee.connected();
  Serial.printf("[console] zigbee=%s channel=%u sensors=%u permit_join=%us\n",
                zigbeeReady ? "online" : "not-ready", currentChannel(), sensorCount,
                permitJoinRemaining());

  if (sensorCount == 0) {
    Serial.println("[console] no Zigbee sensors seen yet");
    return;
  }

  for (const auto &sensor : sensors) {
    if (!sensor.used) continue;

    char ieeeText[24]{};
    plantlink::formatIeee(sensor.ieee, ieeeText, sizeof(ieeeText));

    Serial.printf("[console] sensor ieee=%s short=0x%04X lqi=%u",
                  ieeeText, sensor.shortAddress, sensor.lqi);

    if (sensor.rssi == plantlink::kRssiUnavailableDbm) {
      Serial.print(" rssi=n/a");
    } else {
      Serial.printf(" rssi=%d", sensor.rssi);
    }

    if (sensor.fieldFlags & plantlink::SensorHasSoilMoisture) {
      Serial.printf(" soil=%u%%", sensor.soilMoisturePct);
    }
    if (sensor.fieldFlags & plantlink::SensorHasTemperature) {
      Serial.printf(" temp=%.1fC", sensor.temperatureCentiC / 100.0f);
    }
    if (sensor.fieldFlags & plantlink::SensorHasHumidity) {
      Serial.printf(" rh=%.1f%%", sensor.humidityCentiPct / 100.0f);
    }
    if (sensor.fieldFlags & plantlink::SensorHasBattery) {
      Serial.printf(" batt=%u%%", sensor.batteryPct);
    }
    Serial.println();
  }
}

void handleUsbConsoleCommand(char command) {
  if (command >= 'A' && command <= 'Z') {
    command = static_cast<char>(command - 'A' + 'a');
  }

  switch (command) {
    case 'p':
      Zigbee.openNetwork(120);
      permitJoinUntilMs = millis() + 120000u;
      Serial.println("[console] Zigbee pairing OPEN for 120 seconds");
      sendNetworkStatus();
      break;

    case 'c':
      Zigbee.closeNetwork();
      permitJoinUntilMs = 0;
      Serial.println("[console] Zigbee pairing CLOSED");
      sendNetworkStatus();
      break;

    case 's':
      printUsbConsoleStatus();
      break;

    case 'h':
    case '?':
      printUsbConsoleHelp();
      break;

    default:
      Serial.printf("[console] unknown command '%c' - press h for help\n", command);
      break;
  }
}

void serviceUsbConsole() {
  while (Serial.available()) {
    const int raw = Serial.read();
    if (raw < 0) break;

    const char command = static_cast<char>(raw);
    if (command == '\r' || command == '\n' || command == ' ' || command == '\t') {
      continue;
    }

    handleUsbConsoleCommand(command);
  }
}
bool startZigbee() {
  Serial.println("[zigbee] configuring ESP32-H2 as native coordinator");
  zbGateway.setManufacturerAndModel("ESP PLANTS", "PlantGateway-H2");
  Zigbee.addEndpoint(&zbGateway);
  Zigbee.setDebugMode(true);
  Zigbee.setRebootOpenNetwork(0);

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    Serial.println("[zigbee] FAILED to start coordinator");
    return false;
  }

  zigbeeReady = Zigbee.connected();
  Serial.printf("[zigbee] coordinator started; connected=%s channel=%u\n",
                zigbeeReady ? "yes" : "no", currentChannel());

  // Observe raw APS frames so vendor-specific Tuya 0xEF00 traffic remains visible.
  // This is registered after Arduino Zigbee startup and returns false so the stack
  // continues its normal processing after we copy the frame to our queue.
  esp_zb_aps_data_indication_handler_register(apsDataHandler);
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP PLANTS - M5Stack Gateway H2 bring-up");

  PlantUart.begin(kPlantLinkBaud, SERIAL_8N1, kPlantLinkRxPin, kPlantLinkTxPin);
  Serial.printf("PlantLink UART1: RX=%d TX=%d baud=%lu\n", kPlantLinkRxPin,
                kPlantLinkTxPin, static_cast<unsigned long>(kPlantLinkBaud));

  apsQueue = xQueueCreate(12, sizeof(ApsEvent));
  if (!apsQueue) {
    Serial.println("FATAL: could not allocate APS event queue");
    while (true) delay(1000);
  }

  startZigbee();
  sendNetworkStatus();
  printUsbConsoleHelp();
}

void loop() {
  serviceUsbConsole();
  servicePlantLink();

  ApsEvent event;
  while (apsQueue && xQueueReceive(apsQueue, &event, 0) == pdTRUE) {
    processApsEvent(event);
  }

  delay(2);
}
