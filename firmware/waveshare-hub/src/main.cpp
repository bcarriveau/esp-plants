#include <Arduino.h>
#include <Preferences.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>

#include "plantlink.h"

namespace {

constexpr uint32_t kPlantLinkBaud = 115200;
constexpr int kPlantLinkRxPin = 44;  // ESP32-S3 UART0 RX -> Waveshare UART2 RXD
constexpr int kPlantLinkTxPin = 43;  // ESP32-S3 UART0 TX -> Waveshare UART2 TXD
constexpr uint32_t kHelloIntervalMs = 1500;
constexpr uint32_t kLinkTimeoutMs = 7000;
constexpr uint32_t kUiRefreshIntervalMs = 1000;
constexpr size_t kMaxSensors = 16;

struct PlantSensor {
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
  char name[20]{};
};

struct PlantListRow {
  lv_obj_t *container = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *moisture = nullptr;
  lv_obj_t *bar = nullptr;
};

plantlink::Decoder decoder;
Preferences preferences;
PlantSensor sensors[kMaxSensors];
PlantListRow listRows[kMaxSensors];

uint16_t nextSequence = 1;
uint32_t lastHelloMs = 0;
uint32_t lastH2RxMs = 0;
uint32_t lastUiRefreshMs = 0;

bool h2Online = false;
bool networkReady = false;
bool useFahrenheit = true;
bool uiDirty = true;

uint8_t zigbeeChannel = 0;
uint8_t h2SensorCount = 0;
uint8_t permitJoinRemaining = 0;
uint8_t localSensorCount = 0;
int selectedSensor = -1;

lv_obj_t *h2StatusLabel = nullptr;
lv_obj_t *zigbeeStatusLabel = nullptr;
lv_obj_t *sensorCountLabel = nullptr;

lv_obj_t *plantNameLabel = nullptr;
lv_obj_t *plantMoodLabel = nullptr;
lv_obj_t *ieeeLabel = nullptr;
lv_obj_t *soilValueLabel = nullptr;
lv_obj_t *soilBar = nullptr;
lv_obj_t *tempValueLabel = nullptr;
lv_obj_t *humidityValueLabel = nullptr;
lv_obj_t *batteryValueLabel = nullptr;
lv_obj_t *signalValueLabel = nullptr;
lv_obj_t *updatedLabel = nullptr;
lv_obj_t *warningLabel = nullptr;

lv_obj_t *plantList = nullptr;
lv_obj_t *pairButton = nullptr;
lv_obj_t *pairButtonLabel = nullptr;
lv_obj_t *unitButton = nullptr;
lv_obj_t *unitButtonLabel = nullptr;

bool ieeeEqual(const uint8_t a[8], const uint8_t b[8]) {
  return memcmp(a, b, 8) == 0;
}

bool ieeeIsZero(const uint8_t ieee[8]) {
  if (!ieee) return true;
  for (size_t i = 0; i < 8; ++i) {
    if (ieee[i] != 0) return false;
  }
  return true;
}

PlantSensor *findSensor(const uint8_t ieee[8]) {
  if (ieeeIsZero(ieee)) return nullptr;
  for (auto &sensor : sensors) {
    if (sensor.used && ieeeEqual(sensor.ieee, ieee)) return &sensor;
  }
  return nullptr;
}

PlantSensor *findOrCreateSensor(const uint8_t ieee[8], uint16_t shortAddress) {
  PlantSensor *sensor = findSensor(ieee);
  if (sensor) {
    sensor->shortAddress = shortAddress;
    return sensor;
  }

  if (ieeeIsZero(ieee)) return nullptr;

  for (size_t i = 0; i < kMaxSensors; ++i) {
    if (sensors[i].used) continue;

    sensors[i] = PlantSensor{};
    sensors[i].used = true;
    memcpy(sensors[i].ieee, ieee, 8);
    sensors[i].shortAddress = shortAddress;
    ++localSensorCount;
    snprintf(sensors[i].name, sizeof(sensors[i].name), "PLANT %u",
             static_cast<unsigned>(localSensorCount));

    if (selectedSensor < 0) selectedSensor = static_cast<int>(i);
    uiDirty = true;
    return &sensors[i];
  }

  return nullptr;
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

void requestPermitJoin(uint8_t seconds) {
  sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);
  permitJoinRemaining = seconds;
  uiDirty = true;
  Serial.printf("[plantlink] permit join requested: %u s\n", seconds);
}

void setH2Online(bool online) {
  if (h2Online == online) return;
  h2Online = online;
  uiDirty = true;
}

void setLabelText(lv_obj_t *label, const char *text) {
  if (label && text) lv_label_set_text(label, text);
}

const char *plantMood(const PlantSensor &sensor) {
  if ((sensor.fieldFlags & plantlink::SensorHasWaterWarning) && sensor.waterWarning) {
    return "I'M THIRSTY!";
  }
  if (!(sensor.fieldFlags & plantlink::SensorHasSoilMoisture)) {
    return "Waiting for a moisture reading";
  }
  if (sensor.soilMoisturePct <= 20) return "Dry - I could use a drink";
  if (sensor.soilMoisturePct <= 40) return "Getting a little thirsty";
  if (sensor.soilMoisturePct <= 70) return "Doing good";
  return "Nice and moist";
}

void sensorRowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const intptr_t index = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  if (index < 0 || index >= static_cast<intptr_t>(kMaxSensors)) return;
  if (!sensors[index].used) return;
  selectedSensor = static_cast<int>(index);
  uiDirty = true;
}

void pairButtonEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) requestPermitJoin(120);
}

void unitButtonEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  useFahrenheit = !useFahrenheit;
  preferences.putBool("fahrenheit", useFahrenheit);
  uiDirty = true;
}

lv_obj_t *makeMetric(lv_obj_t *parent, const char *caption, int x, int y,
                     lv_obj_t **valueOut) {
  lv_obj_t *captionLabel = lv_label_create(parent);
  lv_label_set_text(captionLabel, caption);
  lv_obj_set_style_text_font(captionLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(captionLabel, lv_color_hex(0x687269), 0);
  lv_obj_set_pos(captionLabel, x, y);

  *valueOut = lv_label_create(parent);
  lv_label_set_text(*valueOut, "--");
  lv_obj_set_style_text_font(*valueOut, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(*valueOut, lv_color_hex(0x142419), 0);
  lv_obj_set_pos(*valueOut, x, y + 20);
  return captionLabel;
}

void buildUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xEEF3ED), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(0x142419), LV_PART_MAIN);

  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, 800, 68);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x183E2B), 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "ESP PLANTS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(title, 22, 10);

  lv_obj_t *tagline = lv_label_create(header);
  lv_label_set_text(tagline, "keep 'em alive");
  lv_obj_set_style_text_font(tagline, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tagline, lv_color_hex(0xCDE0D2), 0);
  lv_obj_set_pos(tagline, 24, 45);

  h2StatusLabel = lv_label_create(header);
  lv_label_set_text(h2StatusLabel, "H2 OFFLINE");
  lv_obj_set_style_text_font(h2StatusLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(h2StatusLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(h2StatusLabel, 264, 13);

  zigbeeStatusLabel = lv_label_create(header);
  lv_label_set_text(zigbeeStatusLabel, "ZIGBEE --");
  lv_obj_set_style_text_font(zigbeeStatusLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(zigbeeStatusLabel, lv_color_hex(0xCDE0D2), 0);
  lv_obj_set_pos(zigbeeStatusLabel, 264, 38);

  sensorCountLabel = lv_label_create(header);
  lv_label_set_text(sensorCountLabel, "SENSORS 0");
  lv_obj_set_style_text_font(sensorCountLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sensorCountLabel, lv_color_hex(0xCDE0D2), 0);
  lv_obj_set_pos(sensorCountLabel, 410, 38);

  unitButton = lv_btn_create(header);
  lv_obj_set_size(unitButton, 48, 40);
  lv_obj_set_pos(unitButton, 604, 14);
  lv_obj_set_style_radius(unitButton, 12, 0);
  lv_obj_set_style_bg_color(unitButton, lv_color_hex(0x2E6144), 0);
  lv_obj_add_event_cb(unitButton, unitButtonEvent, LV_EVENT_CLICKED, nullptr);

  unitButtonLabel = lv_label_create(unitButton);
  lv_label_set_text(unitButtonLabel, "F");
  lv_obj_set_style_text_font(unitButtonLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(unitButtonLabel);

  pairButton = lv_btn_create(header);
  lv_obj_set_size(pairButton, 126, 40);
  lv_obj_set_pos(pairButton, 660, 14);
  lv_obj_set_style_radius(pairButton, 12, 0);
  lv_obj_set_style_bg_color(pairButton, lv_color_hex(0x66A66F), 0);
  lv_obj_add_event_cb(pairButton, pairButtonEvent, LV_EVENT_CLICKED, nullptr);

  pairButtonLabel = lv_label_create(pairButton);
  lv_label_set_text(pairButtonLabel, "ADD SENSOR");
  lv_obj_set_style_text_font(pairButtonLabel, &lv_font_montserrat_14, 0);
  lv_obj_center(pairButtonLabel);

  // Featured plant card: intentionally inspired by the playful HA dashboard
  // rather than the earlier engineering bring-up table.
  lv_obj_t *featured = lv_obj_create(screen);
  lv_obj_set_pos(featured, 18, 82);
  lv_obj_set_size(featured, 504, 380);
  lv_obj_set_style_radius(featured, 20, 0);
  lv_obj_set_style_border_width(featured, 0, 0);
  lv_obj_set_style_bg_color(featured, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(featured, 12, 0);
  lv_obj_set_style_shadow_opa(featured, LV_OPA_20, 0);
  lv_obj_clear_flag(featured, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *section = lv_label_create(featured);
  lv_label_set_text(section, "WHO NEEDS WATER?");
  lv_obj_set_style_text_font(section, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(section, lv_color_hex(0x66806D), 0);
  lv_obj_set_pos(section, 22, 16);

  plantNameLabel = lv_label_create(featured);
  lv_label_set_text(plantNameLabel, "WAITING FOR SENSOR");
  lv_obj_set_style_text_font(plantNameLabel, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(plantNameLabel, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(plantNameLabel, 22, 40);

  plantMoodLabel = lv_label_create(featured);
  lv_label_set_text(plantMoodLabel, "Pair a sensor and I'll keep an eye on it");
  lv_obj_set_style_text_font(plantMoodLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(plantMoodLabel, lv_color_hex(0x467252), 0);
  lv_obj_set_pos(plantMoodLabel, 24, 83);

  ieeeLabel = lv_label_create(featured);
  lv_label_set_text(ieeeLabel, "--");
  lv_obj_set_style_text_font(ieeeLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ieeeLabel, lv_color_hex(0x839087), 0);
  lv_obj_set_pos(ieeeLabel, 24, 111);
  lv_obj_set_width(ieeeLabel, 452);
  lv_label_set_long_mode(ieeeLabel, LV_LABEL_LONG_DOT);

  makeMetric(featured, "SOIL", 24, 150, &soilValueLabel);
  makeMetric(featured, "TEMP", 178, 150, &tempValueLabel);
  makeMetric(featured, "AIR RH", 332, 150, &humidityValueLabel);

  soilBar = lv_bar_create(featured);
  lv_obj_set_pos(soilBar, 24, 224);
  lv_obj_set_size(soilBar, 452, 22);
  lv_bar_set_range(soilBar, 0, 100);
  lv_bar_set_value(soilBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(soilBar, 11, LV_PART_MAIN);
  lv_obj_set_style_radius(soilBar, 11, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(soilBar, lv_color_hex(0xDCE8DD), LV_PART_MAIN);
  lv_obj_set_style_bg_color(soilBar, lv_color_hex(0x62A86E), LV_PART_INDICATOR);

  makeMetric(featured, "BATTERY", 24, 270, &batteryValueLabel);
  makeMetric(featured, "SIGNAL", 178, 270, &signalValueLabel);

  updatedLabel = lv_label_create(featured);
  lv_label_set_text(updatedLabel, "No sensor data yet");
  lv_obj_set_style_text_font(updatedLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updatedLabel, lv_color_hex(0x687269), 0);
  lv_obj_set_pos(updatedLabel, 332, 296);

  warningLabel = lv_label_create(featured);
  lv_label_set_text(warningLabel, "WATER ME!");
  lv_obj_set_style_text_font(warningLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(warningLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(warningLabel, lv_color_hex(0xB94C3E), 0);
  lv_obj_set_style_pad_hor(warningLabel, 14, 0);
  lv_obj_set_style_pad_ver(warningLabel, 8, 0);
  lv_obj_set_style_radius(warningLabel, 10, 0);
  lv_obj_set_pos(warningLabel, 332, 326);
  lv_obj_add_flag(warningLabel, LV_OBJ_FLAG_HIDDEN);

  // Right-side plant list. All known plants exist here; the list scrolls when
  // the installation grows beyond what fits on one 800x480 screen.
  lv_obj_t *listCard = lv_obj_create(screen);
  lv_obj_set_pos(listCard, 538, 82);
  lv_obj_set_size(listCard, 244, 380);
  lv_obj_set_style_radius(listCard, 20, 0);
  lv_obj_set_style_border_width(listCard, 0, 0);
  lv_obj_set_style_bg_color(listCard, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(listCard, 12, 0);
  lv_obj_set_style_shadow_opa(listCard, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(listCard, 12, 0);

  lv_obj_t *listTitle = lv_label_create(listCard);
  lv_label_set_text(listTitle, "YOUR PLANTS");
  lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(listTitle, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(listTitle, 4, 2);

  lv_obj_t *listHint = lv_label_create(listCard);
  lv_label_set_text(listHint, "Tap one for details");
  lv_obj_set_style_text_font(listHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(listHint, lv_color_hex(0x839087), 0);
  lv_obj_set_pos(listHint, 4, 27);

  plantList = lv_obj_create(listCard);
  lv_obj_set_pos(plantList, 0, 52);
  lv_obj_set_size(plantList, 220, 300);
  lv_obj_set_style_border_width(plantList, 0, 0);
  lv_obj_set_style_bg_opa(plantList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(plantList, 0, 0);
  lv_obj_set_style_pad_row(plantList, 8, 0);
  lv_obj_set_flex_flow(plantList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(plantList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(plantList, LV_SCROLLBAR_MODE_AUTO);

  for (size_t i = 0; i < kMaxSensors; ++i) {
    PlantListRow &row = listRows[i];
    row.container = lv_obj_create(plantList);
    lv_obj_set_size(row.container, 214, 58);
    lv_obj_set_style_radius(row.container, 12, 0);
    lv_obj_set_style_border_width(row.container, 0, 0);
    lv_obj_set_style_bg_color(row.container, lv_color_hex(0xF2F6F1), 0);
    lv_obj_set_style_pad_all(row.container, 8, 0);
    lv_obj_clear_flag(row.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row.container, sensorRowEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    row.name = lv_label_create(row.container);
    lv_label_set_text(row.name, "PLANT");
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(row.name, lv_color_hex(0x173E2A), 0);
    lv_obj_set_pos(row.name, 2, 0);

    row.moisture = lv_label_create(row.container);
    lv_label_set_text(row.moisture, "--%");
    lv_obj_set_style_text_font(row.moisture, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(row.moisture, lv_color_hex(0x173E2A), 0);
    lv_obj_align(row.moisture, LV_ALIGN_TOP_RIGHT, -2, -2);

    row.bar = lv_bar_create(row.container);
    lv_obj_set_pos(row.bar, 2, 31);
    lv_obj_set_size(row.bar, 194, 9);
    lv_bar_set_range(row.bar, 0, 100);
    lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(row.bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(row.bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(0xDCE8DD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.bar, lv_color_hex(0x62A86E), LV_PART_INDICATOR);
  }

  uiDirty = true;
}

void refreshDashboard() {
  if (!uiDirty && millis() - lastUiRefreshMs < kUiRefreshIntervalMs) return;
  lastUiRefreshMs = millis();
  uiDirty = false;

  if (!lvgl_port_lock(-1)) return;

  char text[128]{};

  snprintf(text, sizeof(text), "H2 %s", h2Online ? "ONLINE" : "OFFLINE");
  setLabelText(h2StatusLabel, text);

  if (networkReady) {
    snprintf(text, sizeof(text), "ZIGBEE CH %u", zigbeeChannel);
  } else if (h2Online) {
    snprintf(text, sizeof(text), "ZIGBEE STARTING");
  } else {
    snprintf(text, sizeof(text), "ZIGBEE --");
  }
  setLabelText(zigbeeStatusLabel, text);

  snprintf(text, sizeof(text), "SENSORS %u", h2SensorCount);
  setLabelText(sensorCountLabel, text);
  setLabelText(unitButtonLabel, useFahrenheit ? "F" : "C");

  if (permitJoinRemaining > 0) {
    snprintf(text, sizeof(text), "PAIR %us", permitJoinRemaining);
    setLabelText(pairButtonLabel, text);
  } else {
    setLabelText(pairButtonLabel, "ADD SENSOR");
  }

  if (h2Online && networkReady) {
    lv_obj_clear_state(pairButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(pairButton, LV_STATE_DISABLED);
  }

  for (size_t i = 0; i < kMaxSensors; ++i) {
    PlantListRow &row = listRows[i];
    if (!sensors[i].used) {
      lv_obj_add_flag(row.container, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(row.container, LV_OBJ_FLAG_HIDDEN);
    setLabelText(row.name, sensors[i].name);

    if (sensors[i].fieldFlags & plantlink::SensorHasSoilMoisture) {
      snprintf(text, sizeof(text), "%u%%", sensors[i].soilMoisturePct);
      lv_bar_set_value(row.bar, sensors[i].soilMoisturePct, LV_ANIM_OFF);
    } else {
      snprintf(text, sizeof(text), "--%%");
      lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
    }
    setLabelText(row.moisture, text);

    const bool selected = selectedSensor == static_cast<int>(i);
    lv_obj_set_style_bg_color(row.container,
                              lv_color_hex(selected ? 0xDDECDD : 0xF2F6F1), 0);
  }

  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) {
    setLabelText(plantNameLabel, "WAITING FOR SENSOR");
    setLabelText(plantMoodLabel, "Pair a sensor and I'll keep an eye on it");
    setLabelText(ieeeLabel, "No plant data yet");
    setLabelText(soilValueLabel, "--%");
    setLabelText(tempValueLabel, useFahrenheit ? "--.- F" : "--.- C");
    setLabelText(humidityValueLabel, "--%");
    setLabelText(batteryValueLabel, "--%");
    setLabelText(signalValueLabel, "LQI --");
    setLabelText(updatedLabel, "No sensor data yet");
    lv_bar_set_value(soilBar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(warningLabel, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    return;
  }

  PlantSensor &sensor = sensors[selectedSensor];
  setLabelText(plantNameLabel, sensor.name);
  setLabelText(plantMoodLabel, plantMood(sensor));

  char ieee[24]{};
  plantlink::formatIeee(sensor.ieee, ieee, sizeof(ieee));
  snprintf(text, sizeof(text), "%s   short 0x%04X", ieee, sensor.shortAddress);
  setLabelText(ieeeLabel, text);

  if (sensor.fieldFlags & plantlink::SensorHasSoilMoisture) {
    snprintf(text, sizeof(text), "%u%%", sensor.soilMoisturePct);
    lv_bar_set_value(soilBar, sensor.soilMoisturePct, LV_ANIM_OFF);
  } else {
    snprintf(text, sizeof(text), "--%%");
    lv_bar_set_value(soilBar, 0, LV_ANIM_OFF);
  }
  setLabelText(soilValueLabel, text);

  if (sensor.fieldFlags & plantlink::SensorHasTemperature) {
    const float c = sensor.temperatureCentiC / 100.0f;
    if (useFahrenheit) {
      snprintf(text, sizeof(text), "%.1f F", c * 9.0f / 5.0f + 32.0f);
    } else {
      snprintf(text, sizeof(text), "%.1f C", c);
    }
  } else {
    snprintf(text, sizeof(text), useFahrenheit ? "--.- F" : "--.- C");
  }
  setLabelText(tempValueLabel, text);

  if (sensor.fieldFlags & plantlink::SensorHasHumidity) {
    snprintf(text, sizeof(text), "%.0f%%", sensor.humidityCentiPct / 100.0f);
  } else {
    snprintf(text, sizeof(text), "--%%");
  }
  setLabelText(humidityValueLabel, text);

  if (sensor.fieldFlags & plantlink::SensorHasBattery) {
    snprintf(text, sizeof(text), "%u%%", sensor.batteryPct);
  } else {
    snprintf(text, sizeof(text), "--%%");
  }
  setLabelText(batteryValueLabel, text);

  snprintf(text, sizeof(text), "LQI %u", sensor.lqi);
  setLabelText(signalValueLabel, text);

  const uint32_t ageSeconds = (millis() - sensor.lastSeenMs) / 1000u;
  if (ageSeconds < 2) {
    snprintf(text, sizeof(text), "Updated now");
  } else if (ageSeconds < 60) {
    snprintf(text, sizeof(text), "Updated %lus ago",
             static_cast<unsigned long>(ageSeconds));
  } else {
    snprintf(text, sizeof(text), "Updated %lum ago",
             static_cast<unsigned long>(ageSeconds / 60u));
  }
  setLabelText(updatedLabel, text);

  if ((sensor.fieldFlags & plantlink::SensorHasWaterWarning) && sensor.waterWarning) {
    lv_obj_clear_flag(warningLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(warningLabel, LV_OBJ_FLAG_HIDDEN);
  }

  lvgl_port_unlock();
}

void handleNetworkStatus(const plantlink::Frame &frame) {
  if (frame.payloadLength < 4) return;
  networkReady = frame.payload[0] != 0;
  zigbeeChannel = frame.payload[1];
  h2SensorCount = frame.payload[2];
  permitJoinRemaining = frame.payload[3];
  uiDirty = true;
}

void handleDeviceJoined(const plantlink::Frame &frame) {
  if (frame.payloadLength < 10) return;

  PlantSensor *sensor = findOrCreateSensor(frame.payload,
                                           plantlink::getU16LE(frame.payload + 8));
  char ieee[24]{};
  plantlink::formatIeee(frame.payload, ieee, sizeof(ieee));
  Serial.printf("[zigbee] device seen: %s short=0x%04X\n", ieee,
                plantlink::getU16LE(frame.payload + 8));

  if (sensor) {
    sensor->lastSeenMs = millis();
    uiDirty = true;
  }
}

void handleSensorReport(const plantlink::Frame &frame) {
  plantlink::SensorReportData report;
  if (!plantlink::parseSensorReport(frame.payload, frame.payloadLength, report)) return;

  PlantSensor *sensor = findOrCreateSensor(report.ieee, report.shortAddress);
  if (!sensor) return;

  sensor->shortAddress = report.shortAddress;
  sensor->fieldFlags = report.fieldFlags;
  sensor->temperatureCentiC = report.temperatureCentiC;
  sensor->humidityCentiPct = report.humidityCentiPct;
  sensor->soilMoisturePct = report.soilMoisturePct;
  sensor->batteryPct = report.batteryPct;
  sensor->waterWarning = report.waterWarning;
  sensor->lqi = report.lqi;
  sensor->rssi = report.rssiDbm;
  sensor->lastSeenMs = millis();
  uiDirty = true;

  char ieee[24]{};
  plantlink::formatIeee(report.ieee, ieee, sizeof(ieee));

  Serial.printf("[sensor] %s", ieee);
  if (report.fieldFlags & plantlink::SensorHasSoilMoisture) {
    Serial.printf(" soil=%u%%", report.soilMoisturePct);
  }
  if (report.fieldFlags & plantlink::SensorHasTemperature) {
    const float c = report.temperatureCentiC / 100.0f;
    Serial.printf(" temp=%.1fC/%.1fF", c, c * 9.0f / 5.0f + 32.0f);
  }
  if (report.fieldFlags & plantlink::SensorHasHumidity) {
    Serial.printf(" rh=%.1f%%", report.humidityCentiPct / 100.0f);
  }
  if (report.fieldFlags & plantlink::SensorHasBattery) {
    Serial.printf(" batt=%u%%", report.batteryPct);
  }
  if (report.fieldFlags & plantlink::SensorHasWaterWarning) {
    Serial.printf(" water_warning=%s", report.waterWarning ? "ON" : "OFF");
  }
  Serial.printf(" lqi=%u", report.lqi);
  if (report.rssiDbm == plantlink::kRssiUnavailableDbm) {
    Serial.print(" rssi=n/a");
  } else {
    Serial.printf(" rssi=%d", report.rssiDbm);
  }
  Serial.println();
}

void handleFrame(const plantlink::Frame &frame) {
  lastH2RxMs = millis();
  setH2Online(true);

  switch (frame.type) {
    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1
                           ? frame.payloadLength
                           : sizeof(build) - 1;
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
    setH2Online(false);
    networkReady = false;
    permitJoinRemaining = 0;
    uiDirty = true;
  }
}

}  // namespace

void setup() {
  // ESP32-S3 native USB CDC. The explicit PlatformIO USB defines make Serial
  // stay on the native USB port while Serial0 remains dedicated to PlantLink.
  Serial.begin(115200);
  const uint32_t usbWaitStart = millis();
  while (!Serial && millis() - usbWaitStart < 1500u) {
    delay(10);
  }

  Serial.println();
  Serial.println("ESP PLANTS Waveshare dashboard");
  Serial.println("[usb] native USB CDC console online");

  preferences.begin("espplants", false);
  useFahrenheit = preferences.getBool("fahrenheit", true);
  Serial.printf("[settings] temperature units=%s\n", useFahrenheit ? "F" : "C");

  Serial0.begin(kPlantLinkBaud, SERIAL_8N1, kPlantLinkRxPin, kPlantLinkTxPin);
  Serial.printf("[plantlink] UART0 RX=%d TX=%d baud=%lu\n", kPlantLinkRxPin,
                kPlantLinkTxPin, static_cast<unsigned long>(kPlantLinkBaud));

  Serial.println("[display] initializing Waveshare 800x480...");
  lcd_init();

  if (lvgl_port_lock(-1)) {
    buildUi();
    lvgl_port_unlock();
  }

  Serial.println("[display] ready");
  Serial.println("[plantlink] waiting for H2 frames");
}

void loop() {
  servicePlantLink();
  refreshDashboard();
  delay(2);
}
