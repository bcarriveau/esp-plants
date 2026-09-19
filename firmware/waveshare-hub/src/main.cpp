#include <Arduino.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>

#include "plantlink.h"

namespace {

constexpr uint32_t kPlantLinkBaud = 115200;
constexpr int kPlantLinkRxPin = 44;  // ESP32-S3 UART0 RX -> Waveshare UART2 RXD
constexpr int kPlantLinkTxPin = 43;  // ESP32-S3 UART0 TX -> Waveshare UART2 TXD
constexpr uint32_t kHelloIntervalMs = 1500;
constexpr uint32_t kLinkTimeoutMs = 7000;

plantlink::Decoder decoder;
uint16_t nextSequence = 1;
uint32_t lastHelloMs = 0;
uint32_t lastH2RxMs = 0;
bool h2Online = false;

lv_obj_t *linkValue = nullptr;
lv_obj_t *zigbeeValue = nullptr;
lv_obj_t *sensorValue = nullptr;
lv_obj_t *eventValue = nullptr;
lv_obj_t *pairButton = nullptr;

uint8_t sensorCount = 0;
uint8_t zigbeeChannel = 0;
bool networkReady = false;

void uiSetText(lv_obj_t *label, const char *text) {
  if (!label || !text) return;
  if (lvgl_port_lock(-1)) {
    lv_label_set_text(label, text);
    lvgl_port_unlock();
  }
}

void setLinkState(bool online) {
  if (h2Online == online) return;
  h2Online = online;
  uiSetText(linkValue, online ? "ONLINE" : "WAITING FOR H2");
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
  plantlink::putU32LE(payload + 4, 0);  // Waveshare capabilities reserved for later.
  sendFrame(plantlink::MessageType::Hello, payload, sizeof(payload));
}

void requestPermitJoin(uint8_t seconds) {
  sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);
  char text[80];
  snprintf(text, sizeof(text), "Pairing requested for %u seconds", seconds);
  uiSetText(eventValue, text);
  Serial.printf("[plantlink] permit join requested: %u s\n", seconds);
}

void pairButtonEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) requestPermitJoin(120);
}

void buildUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xF4F6F1), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(0x152018), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "ESP PLANTS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 34, 24);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "Waveshare + direct Zigbee bring-up");
  lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_18, 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 36, 70);

  auto makeRow = [&](const char *name, int y, lv_obj_t **valueOut) {
    lv_obj_t *nameLabel = lv_label_create(screen);
    lv_label_set_text(nameLabel, name);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(nameLabel, 42, y);

    *valueOut = lv_label_create(screen);
    lv_label_set_text(*valueOut, "--");
    lv_obj_set_style_text_font(*valueOut, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(*valueOut, 300, y);
  };

  makeRow("H2 LINK", 135, &linkValue);
  makeRow("ZIGBEE", 185, &zigbeeValue);
  makeRow("SENSORS", 235, &sensorValue);
  makeRow("LAST EVENT", 285, &eventValue);

  lv_label_set_text(linkValue, "WAITING FOR H2");
  lv_label_set_text(zigbeeValue, "STARTING");
  lv_label_set_text(sensorValue, "0");
  lv_label_set_text(eventValue, "No Zigbee traffic yet");

  pairButton = lv_btn_create(screen);
  lv_obj_set_size(pairButton, 250, 70);
  lv_obj_align(pairButton, LV_ALIGN_BOTTOM_LEFT, 40, -42);
  lv_obj_add_event_cb(pairButton, pairButtonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *pairLabel = lv_label_create(pairButton);
  lv_label_set_text(pairLabel, "ADD SENSOR");
  lv_obj_set_style_text_font(pairLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(pairLabel);

  lv_obj_t *note = lv_label_create(screen);
  lv_label_set_text(note, "Native USB = flash/debug   |   UART2 = H2 PlantLink");
  lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
  lv_obj_align(note, LV_ALIGN_BOTTOM_RIGHT, -25, -18);
}

void handleNetworkStatus(const plantlink::Frame &frame) {
  // payload: ready(1), channel(1), sensor_count(1), permit_join_remaining_s(1)
  if (frame.payloadLength < 4) return;
  networkReady = frame.payload[0] != 0;
  zigbeeChannel = frame.payload[1];
  sensorCount = frame.payload[2];
  const uint8_t permitRemaining = frame.payload[3];

  char text[96];
  if (networkReady) {
    if (permitRemaining) {
      snprintf(text, sizeof(text), "READY  CH %u  |  JOIN OPEN %us", zigbeeChannel, permitRemaining);
    } else {
      snprintf(text, sizeof(text), "READY  CH %u", zigbeeChannel);
    }
  } else {
    snprintf(text, sizeof(text), "STARTING");
  }
  uiSetText(zigbeeValue, text);

  snprintf(text, sizeof(text), "%u", sensorCount);
  uiSetText(sensorValue, text);
}

void handleDeviceJoined(const plantlink::Frame &frame) {
  if (frame.payloadLength < 10) return;
  char ieee[24]{};
  plantlink::formatIeee(frame.payload, ieee, sizeof(ieee));
  const uint16_t shortAddr = plantlink::getU16LE(frame.payload + 8);
  char text[96];
  snprintf(text, sizeof(text), "JOINED %s  (0x%04X)", ieee, shortAddr);
  uiSetText(eventValue, text);
  Serial.printf("[zigbee] %s\n", text);
}

void handleSensorReport(const plantlink::Frame &frame) {
  plantlink::SensorReportData report;
  if (!plantlink::parseSensorReport(frame.payload, frame.payloadLength, report)) return;

  char ieee[24]{};
  plantlink::formatIeee(report.ieee, ieee, sizeof(ieee));
  char text[180];
  int used = snprintf(text, sizeof(text), "%s", ieee);
  if ((report.fieldFlags & plantlink::SensorHasSoilMoisture) && used > 0) {
    used += snprintf(text + used, sizeof(text) - used, "  SOIL %u%%", report.soilMoisturePct);
  }
  if ((report.fieldFlags & plantlink::SensorHasTemperature) && used > 0 && used < static_cast<int>(sizeof(text))) {
    used += snprintf(text + used, sizeof(text) - used, "  %.1fF",
                     (report.temperatureCentiC / 100.0f) * 9.0f / 5.0f + 32.0f);
  }
  if ((report.fieldFlags & plantlink::SensorHasHumidity) && used > 0 && used < static_cast<int>(sizeof(text))) {
    used += snprintf(text + used, sizeof(text) - used, "  RH %.0f%%", report.humidityCentiPct / 100.0f);
  }
  if ((report.fieldFlags & plantlink::SensorHasBattery) && used > 0 && used < static_cast<int>(sizeof(text))) {
    snprintf(text + used, sizeof(text) - used, "  BAT %u%%", report.batteryPct);
  }
  uiSetText(eventValue, text);
  if (report.rssiDbm == plantlink::kRssiUnavailableDbm) {
    Serial.printf("[sensor] %s  lqi=%u rssi=n/a\n", text, report.lqi);
  } else {
    Serial.printf("[sensor] %s  lqi=%u rssi=%d\n", text, report.lqi, report.rssiDbm);
  }
}

void handleFrame(const plantlink::Frame &frame) {
  lastH2RxMs = millis();
  setLinkState(true);

  switch (frame.type) {
    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1 ? frame.payloadLength : sizeof(build) - 1;
      memcpy(build, frame.payload, n);
      Serial.printf("[plantlink] H2 hello: %s\n", build);
      break;
    }
    case plantlink::MessageType::Heartbeat:
      break;
    case plantlink::MessageType::NetworkStatus:
      handleNetworkStatus(frame);
      break;
    case plantlink::MessageType::DeviceJoined:
      handleDeviceJoined(frame);
      break;
    case plantlink::MessageType::SensorReport:
      handleSensorReport(frame);
      break;
    case plantlink::MessageType::RawZigbeeEvent:
      Serial.printf("[zigbee] raw event (%u bytes)\n", frame.payloadLength);
      break;
    default:
      break;
  }
}

void servicePlantLink() {
  plantlink::Frame frame;
  while (Serial0.available()) {
    if (decoder.feed(static_cast<uint8_t>(Serial0.read()), frame)) handleFrame(frame);
  }

  const uint32_t now = millis();
  if (!h2Online && now - lastHelloMs >= kHelloIntervalMs) {
    lastHelloMs = now;
    sendHello();
  }
  if (h2Online && now - lastH2RxMs > kLinkTimeoutMs) {
    setLinkState(false);
    uiSetText(zigbeeValue, "H2 OFFLINE");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);  // Native USB CDC. UART0 stays dedicated to PlantLink.
  delay(250);
  Serial.println();
  Serial.println("ESP PLANTS Waveshare bring-up");

  Serial0.begin(kPlantLinkBaud, SERIAL_8N1, kPlantLinkRxPin, kPlantLinkTxPin);
  Serial.printf("PlantLink UART0: RX=%d TX=%d baud=%lu\n", kPlantLinkRxPin, kPlantLinkTxPin,
                static_cast<unsigned long>(kPlantLinkBaud));

  Serial.println("Initializing Waveshare display...");
  lcd_init();
  if (lvgl_port_lock(-1)) {
    buildUi();
    lvgl_port_unlock();
  }
  Serial.println("Display ready; waiting for H2.");
}

void loop() {
  servicePlantLink();
  delay(2);
}
