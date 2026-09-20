#include <Arduino.h>
#include <Preferences.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>

#include "plantlink.h"

namespace {

constexpr uint32_t kPlantLinkBaud = 115200;
constexpr int kPlantLinkRxPin = 44;
constexpr int kPlantLinkTxPin = 43;
constexpr uint32_t kHelloIntervalMs = 1500;
constexpr uint32_t kLinkTimeoutMs = 7000;
constexpr uint32_t kUiRefreshIntervalMs = 1000;
constexpr size_t kMaxSensors = 16;
constexpr size_t kPlantNameBytes = 24;
constexpr uint32_t kPlantRecordMagic = 0x504C4E54u;  // PLNT
constexpr uint8_t kPlantRecordVersion = 1;

enum class Page : uint8_t { Home = 0, Plant = 1, Settings = 2 };

struct PersistedPlant {
  uint32_t magic = kPlantRecordMagic;
  uint8_t version = kPlantRecordVersion;
  uint8_t ieee[8]{};
  char name[kPlantNameBytes]{};
};

struct PlantSensor {
  bool used = false;
  bool seenThisBoot = false;
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
  char name[kPlantNameBytes]{};
};

struct PlantListRow {
  lv_obj_t *box = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *moisture = nullptr;
  lv_obj_t *bar = nullptr;
};

plantlink::Decoder decoder;
Preferences preferences;
PlantSensor sensors[kMaxSensors];
PlantListRow rows[kMaxSensors];

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
int selectedSensor = -1;
Page currentPage = Page::Home;

lv_obj_t *homePage = nullptr;
lv_obj_t *plantPage = nullptr;
lv_obj_t *settingsPage = nullptr;
lv_obj_t *headerCount = nullptr;
lv_obj_t *navHome = nullptr;
lv_obj_t *navPlant = nullptr;
lv_obj_t *navSettings = nullptr;

lv_obj_t *homeName = nullptr;
lv_obj_t *homeMood = nullptr;
lv_obj_t *homeSoil = nullptr;
lv_obj_t *homeTemp = nullptr;
lv_obj_t *homeHumidity = nullptr;
lv_obj_t *homeBar = nullptr;
lv_obj_t *homeWarning = nullptr;

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

lv_obj_t *settingsH2 = nullptr;
lv_obj_t *settingsZigbee = nullptr;
lv_obj_t *settingsPlants = nullptr;
lv_obj_t *settingsUnit = nullptr;
lv_obj_t *settingsPair = nullptr;

lv_obj_t *renameModal = nullptr;
lv_obj_t *renameTitle = nullptr;
lv_obj_t *renameInput = nullptr;
lv_obj_t *renameKeyboard = nullptr;
bool renameUppercase = true;

bool ieeeEqual(const uint8_t a[8], const uint8_t b[8]) { return memcmp(a, b, 8) == 0; }

bool ieeeZero(const uint8_t ieee[8]) {
  for (size_t i = 0; i < 8; ++i) if (ieee[i]) return false;
  return true;
}

void label(lv_obj_t *obj, const char *text) {
  if (obj && text) lv_label_set_text(obj, text);
}

void slotKey(size_t slot, char out[12]) {
  snprintf(out, 12, "plant%02u", static_cast<unsigned>(slot));
}

void defaultName(size_t slot, char *out, size_t size) {
  snprintf(out, size, "PLANT %u", static_cast<unsigned>(slot + 1));
}

size_t registeredCount() {
  size_t count = 0;
  for (const auto &sensor : sensors) if (sensor.used) ++count;
  return count;
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
    uiDirty = true;
    return &s;
  }
  Serial.println("[registry] no free plant slots");
  return nullptr;
}

const char *mood(const PlantSensor &s) {
  if (!s.seenThisBoot) return "Waiting for this plant to check in";
  if ((s.fieldFlags & plantlink::SensorHasWaterWarning) && s.waterWarning) return "I'M THIRSTY!";
  if (!(s.fieldFlags & plantlink::SensorHasSoilMoisture)) return "Waiting for a moisture reading";
  if (s.soilMoisturePct <= 20) return "Dry - I could use a drink";
  if (s.soilMoisturePct <= 40) return "Getting a little thirsty";
  if (s.soilMoisturePct <= 70) return "Doing good";
  return "Nice and moist";
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
  uiDirty = true;
  Serial.printf("[plantlink] permit join requested: %u s\n", seconds);
}

lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, 20, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(obj, 10, 0);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

void metric(lv_obj_t *parent, const char *caption, int x, int y, lv_obj_t **value,
            const lv_font_t *font = &lv_font_montserrat_28) {
  lv_obj_t *c = lv_label_create(parent);
  lv_label_set_text(c, caption);
  lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(c, lv_color_hex(0x687269), 0);
  lv_obj_set_pos(c, x, y);
  *value = lv_label_create(parent);
  lv_label_set_text(*value, "--");
  lv_obj_set_style_text_font(*value, font, 0);
  lv_obj_set_style_text_color(*value, lv_color_hex(0x142419), 0);
  lv_obj_set_pos(*value, x, y + 19);
}

void showPage(Page page) {
  currentPage = page;
  if (homePage) (page == Page::Home) ? lv_obj_clear_flag(homePage, LV_OBJ_FLAG_HIDDEN)
                                     : lv_obj_add_flag(homePage, LV_OBJ_FLAG_HIDDEN);
  if (plantPage) (page == Page::Plant) ? lv_obj_clear_flag(plantPage, LV_OBJ_FLAG_HIDDEN)
                                       : lv_obj_add_flag(plantPage, LV_OBJ_FLAG_HIDDEN);
  if (settingsPage) (page == Page::Settings) ? lv_obj_clear_flag(settingsPage, LV_OBJ_FLAG_HIDDEN)
                                             : lv_obj_add_flag(settingsPage, LV_OBJ_FLAG_HIDDEN);

  const lv_color_t active = lv_color_hex(0xDDECDD);
  const lv_color_t idle = lv_color_hex(0xFFFFFF);
  if (navHome) lv_obj_set_style_bg_color(navHome, page == Page::Home ? active : idle, 0);
  if (navPlant) lv_obj_set_style_bg_color(navPlant, page == Page::Plant ? active : idle, 0);
  if (navSettings) lv_obj_set_style_bg_color(navSettings, page == Page::Settings ? active : idle, 0);
  uiDirty = true;
}

void navEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  showPage(static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

void rowEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const intptr_t slot = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  if (slot < 0 || slot >= static_cast<intptr_t>(kMaxSensors) || !sensors[slot].used) return;
  selectedSensor = static_cast<int>(slot);
  showPage(Page::Plant);
}

void featuredEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (selectedSensor >= 0 && selectedSensor < static_cast<int>(kMaxSensors) && sensors[selectedSensor].used)
    showPage(Page::Plant);
}

void unitEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  useFahrenheit = !useFahrenheit;
  preferences.putBool("fahrenheit", useFahrenheit);
  Serial.printf("[settings] temperature units=%s\n", useFahrenheit ? "F" : "C");
  uiDirty = true;
}

void pairEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) requestJoin(120);
}

void closeRename() {
  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}

void saveRename() {
  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) {
    closeRename();
    return;
  }

  const char *text = lv_textarea_get_text(renameInput);
  if (text && text[0]) {
    PlantSensor &s = sensors[selectedSensor];
    strncpy(s.name, text, sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';
    saveSlot(static_cast<size_t>(selectedSensor));
    Serial.printf("[registry] renamed slot=%u name=\"%s\"\n",
                  static_cast<unsigned>(selectedSensor + 1), s.name);
    uiDirty = true;
  }
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

void renameEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (selectedSensor < 0 || selectedSensor >= static_cast<int>(kMaxSensors) ||
      !sensors[selectedSensor].used) return;

  char titleText[48]{};
  snprintf(titleText, sizeof(titleText), "RENAME PLANT %u",
           static_cast<unsigned>(selectedSensor + 1));
  label(renameTitle, titleText);

  lv_textarea_set_text(renameInput, sensors[selectedSensor].name);
  lv_textarea_set_cursor_pos(renameInput, LV_TEXTAREA_CURSOR_LAST);

  renameUppercase = true;
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);

  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
}

void buildHeader(lv_obj_t *screen) {
  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, 800, 66);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x183E2B), 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "ESP PLANTS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(title, 22, 9);

  lv_obj_t *tag = lv_label_create(header);
  lv_label_set_text(tag, "keep 'em alive");
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tag, lv_color_hex(0xCDE0D2), 0);
  lv_obj_set_pos(tag, 24, 43);

  headerCount = lv_label_create(header);
  lv_label_set_text(headerCount, "0 PLANTS");
  lv_obj_set_style_text_font(headerCount, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(headerCount, lv_color_hex(0xE6F3E9), 0);
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
  lv_obj_set_style_text_color(section, lv_color_hex(0x66806D), 0);
  lv_obj_set_pos(section, 22, 14);

  homeName = lv_label_create(featured);
  lv_label_set_text(homeName, "WAITING FOR SENSOR");
  lv_obj_set_style_text_font(homeName, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(homeName, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(homeName, 22, 38);
  lv_obj_set_width(homeName, 440);
  lv_label_set_long_mode(homeName, LV_LABEL_LONG_DOT);

  homeMood = lv_label_create(featured);
  lv_label_set_text(homeMood, "Pair a sensor and I'll keep an eye on it");
  lv_obj_set_style_text_font(homeMood, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(homeMood, lv_color_hex(0x467252), 0);
  lv_obj_set_pos(homeMood, 24, 82);
  lv_obj_set_width(homeMood, 445);
  lv_label_set_long_mode(homeMood, LV_LABEL_LONG_DOT);

  metric(featured, "SOIL", 24, 126, &homeSoil, &lv_font_montserrat_32);
  metric(featured, "TEMP", 180, 126, &homeTemp);
  metric(featured, "AIR RH", 334, 126, &homeHumidity);

  homeBar = lv_bar_create(featured);
  lv_obj_set_pos(homeBar, 24, 201);
  lv_obj_set_size(homeBar, 448, 22);
  lv_bar_set_range(homeBar, 0, 100);
  lv_obj_set_style_bg_color(homeBar, lv_color_hex(0xDCE8DD), LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeBar, lv_color_hex(0x62A86E), LV_PART_INDICATOR);

  lv_obj_t *hint = lv_label_create(featured);
  lv_label_set_text(hint, "Tap card for full plant details");
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x839087), 0);
  lv_obj_set_pos(hint, 24, 248);

  homeWarning = lv_label_create(featured);
  lv_label_set_text(homeWarning, "WATER ME!");
  lv_obj_set_style_text_font(homeWarning, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(homeWarning, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(homeWarning, lv_color_hex(0xB94C3E), 0);
  lv_obj_set_style_pad_hor(homeWarning, 14, 0);
  lv_obj_set_style_pad_ver(homeWarning, 8, 0);
  lv_obj_set_style_radius(homeWarning, 10, 0);
  lv_obj_set_pos(homeWarning, 24, 278);
  lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *listCard = card(homePage, 528, 10, 258, 334);
  lv_obj_t *listTitle = lv_label_create(listCard);
  lv_label_set_text(listTitle, "YOUR PLANTS");
  lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(listTitle, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(listTitle, 10, 8);

  lv_obj_t *list = lv_obj_create(listCard);
  lv_obj_set_pos(list, 0, 42);
  lv_obj_set_size(list, 234, 266);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 7, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  for (size_t i = 0; i < kMaxSensors; ++i) {
    rows[i].box = lv_obj_create(list);
    lv_obj_set_size(rows[i].box, 228, 56);
    lv_obj_set_style_radius(rows[i].box, 12, 0);
    lv_obj_set_style_border_width(rows[i].box, 0, 0);
    lv_obj_set_style_bg_color(rows[i].box, lv_color_hex(0xF2F6F1), 0);
    lv_obj_set_style_pad_all(rows[i].box, 8, 0);
    lv_obj_clear_flag(rows[i].box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(rows[i].box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(rows[i].box, rowEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    rows[i].name = lv_label_create(rows[i].box);
    lv_label_set_text(rows[i].name, "PLANT");
    lv_obj_set_style_text_font(rows[i].name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rows[i].name, lv_color_hex(0x173E2A), 0);
    lv_obj_set_width(rows[i].name, 145);
    lv_label_set_long_mode(rows[i].name, LV_LABEL_LONG_DOT);

    rows[i].moisture = lv_label_create(rows[i].box);
    lv_label_set_text(rows[i].moisture, "--%");
    lv_obj_set_style_text_font(rows[i].moisture, &lv_font_montserrat_18, 0);
    lv_obj_align(rows[i].moisture, LV_ALIGN_TOP_RIGHT, -2, -2);

    rows[i].bar = lv_bar_create(rows[i].box);
    lv_obj_set_pos(rows[i].bar, 2, 30);
    lv_obj_set_size(rows[i].bar, 208, 9);
    lv_bar_set_range(rows[i].bar, 0, 100);
    lv_obj_set_style_bg_color(rows[i].bar, lv_color_hex(0xDCE8DD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rows[i].bar, lv_color_hex(0x62A86E), LV_PART_INDICATOR);
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
  lv_obj_set_style_text_color(detailSlot, lv_color_hex(0x66806D), 0);
  lv_obj_set_pos(detailSlot, 22, 14);

  detailName = lv_label_create(p);
  lv_obj_set_style_text_font(detailName, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(detailName, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(detailName, 22, 38);
  lv_obj_set_width(detailName, 510);
  lv_label_set_long_mode(detailName, LV_LABEL_LONG_DOT);

  detailMood = lv_label_create(p);
  lv_obj_set_style_text_font(detailMood, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(detailMood, lv_color_hex(0x467252), 0);
  lv_obj_set_pos(detailMood, 24, 80);

  detailIeee = lv_label_create(p);
  lv_obj_set_style_text_font(detailIeee, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailIeee, lv_color_hex(0x839087), 0);
  lv_obj_set_pos(detailIeee, 24, 108);

  renameButton = lv_btn_create(p);
  lv_obj_set_size(renameButton, 142, 48);
  lv_obj_set_pos(renameButton, 600, 24);
  lv_obj_set_style_radius(renameButton, 12, 0);
  lv_obj_set_style_bg_color(renameButton, lv_color_hex(0x2E6144), 0);
  lv_obj_add_event_cb(renameButton, renameEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *renameLabel = lv_label_create(renameButton);
  lv_label_set_text(renameLabel, "RENAME");
  lv_obj_set_style_text_font(renameLabel, &lv_font_montserrat_16, 0);
  lv_obj_center(renameLabel);

  metric(p, "SOIL", 24, 150, &detailSoil, &lv_font_montserrat_32);
  metric(p, "TEMP", 182, 150, &detailTemp);
  metric(p, "AIR RH", 342, 150, &detailHumidity);
  metric(p, "BATTERY", 502, 150, &detailBattery);
  metric(p, "SIGNAL", 640, 150, &detailSignal, &lv_font_montserrat_24);

  detailBar = lv_bar_create(p);
  lv_obj_set_pos(detailBar, 24, 226);
  lv_obj_set_size(detailBar, 718, 22);
  lv_bar_set_range(detailBar, 0, 100);
  lv_obj_set_style_bg_color(detailBar, lv_color_hex(0xDCE8DD), LV_PART_MAIN);
  lv_obj_set_style_bg_color(detailBar, lv_color_hex(0x62A86E), LV_PART_INDICATOR);

  detailUpdated = lv_label_create(p);
  lv_obj_set_style_text_font(detailUpdated, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailUpdated, lv_color_hex(0x687269), 0);
  lv_obj_set_pos(detailUpdated, 24, 276);

  detailWarning = lv_label_create(p);
  lv_label_set_text(detailWarning, "WATER ME!");
  lv_obj_set_style_text_font(detailWarning, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(detailWarning, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(detailWarning, lv_color_hex(0xB94C3E), 0);
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

  lv_obj_t *system = card(settingsPage, 14, 10, 380, 334);
  lv_obj_t *title = lv_label_create(system);
  lv_label_set_text(title, "SYSTEM");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(title, 22, 18);

  lv_obj_t *sub = lv_label_create(system);
  lv_label_set_text(sub, "The nerdy stuff lives here.");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x839087), 0);
  lv_obj_set_pos(sub, 22, 51);

  lv_obj_t *cap = lv_label_create(system);
  lv_label_set_text(cap, "H2 LINK"); lv_obj_set_pos(cap, 22, 92);
  settingsH2 = lv_label_create(system); lv_obj_set_pos(settingsH2, 22, 113);
  lv_obj_set_style_text_font(settingsH2, &lv_font_montserrat_20, 0);

  cap = lv_label_create(system);
  lv_label_set_text(cap, "ZIGBEE"); lv_obj_set_pos(cap, 22, 158);
  settingsZigbee = lv_label_create(system); lv_obj_set_pos(settingsZigbee, 22, 179);
  lv_obj_set_style_text_font(settingsZigbee, &lv_font_montserrat_20, 0);

  cap = lv_label_create(system);
  lv_label_set_text(cap, "REGISTERED PLANTS"); lv_obj_set_pos(cap, 22, 224);
  settingsPlants = lv_label_create(system); lv_obj_set_pos(settingsPlants, 22, 245);
  lv_obj_set_style_text_font(settingsPlants, &lv_font_montserrat_20, 0);

  lv_obj_t *setup = card(settingsPage, 408, 10, 378, 334);
  title = lv_label_create(setup);
  lv_label_set_text(title, "PLANT SETUP");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x173E2A), 0);
  lv_obj_set_pos(title, 22, 18);

  cap = lv_label_create(setup);
  lv_label_set_text(cap, "TEMPERATURE"); lv_obj_set_pos(cap, 22, 78);
  lv_obj_t *unitButton = lv_btn_create(setup);
  lv_obj_set_size(unitButton, 122, 52); lv_obj_set_pos(unitButton, 22, 103);
  lv_obj_set_style_radius(unitButton, 12, 0);
  lv_obj_set_style_bg_color(unitButton, lv_color_hex(0x2E6144), 0);
  lv_obj_add_event_cb(unitButton, unitEvent, LV_EVENT_CLICKED, nullptr);
  settingsUnit = lv_label_create(unitButton);
  lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_24, 0);
  lv_obj_center(settingsUnit);

  cap = lv_label_create(setup);
  lv_label_set_text(cap, "ZIGBEE SENSOR"); lv_obj_set_pos(cap, 22, 182);
  lv_obj_t *pairButton = lv_btn_create(setup);
  lv_obj_set_size(pairButton, 180, 52); lv_obj_set_pos(pairButton, 22, 207);
  lv_obj_set_style_radius(pairButton, 12, 0);
  lv_obj_set_style_bg_color(pairButton, lv_color_hex(0x66A66F), 0);
  lv_obj_add_event_cb(pairButton, pairEvent, LV_EVENT_CLICKED, nullptr);
  settingsPair = lv_label_create(pairButton);
  lv_label_set_text(settingsPair, "ADD SENSOR");
  lv_obj_set_style_text_font(settingsPair, &lv_font_montserrat_16, 0);
  lv_obj_center(settingsPair);

  lv_obj_t *note = lv_label_create(setup);
  lv_label_set_text(note, "Names stay tied to each sensor's\nIEEE address across reboots.");
  lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(note, lv_color_hex(0x687269), 0);
  lv_obj_set_pos(note, 22, 280);
}

void buildNav(lv_obj_t *screen) {
  lv_obj_t *bar = lv_obj_create(screen);
  lv_obj_set_pos(bar, 0, 422);
  lv_obj_set_size(bar, 800, 58);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xF9FBF8), 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  auto add = [&](int x, const char *text, Page page, lv_obj_t **button) {
    *button = lv_btn_create(bar);
    lv_obj_set_size(*button, 246, 44);
    lv_obj_set_pos(*button, x, 7);
    lv_obj_set_style_radius(*button, 12, 0);
    lv_obj_set_style_shadow_width(*button, 0, 0);
    lv_obj_set_style_border_width(*button, 1, 0);
    lv_obj_set_style_border_color(*button, lv_color_hex(0xD6E1D7), 0);
    lv_obj_add_event_cb(*button, navEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(page)));
    lv_obj_t *l = lv_label_create(*button);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x173E2A), 0);
    lv_obj_center(l);
  };
  add(14, "HOME", Page::Home, &navHome);
  add(277, "PLANT", Page::Plant, &navPlant);
  add(540, "SETTINGS", Page::Settings, &navSettings);
}

void buildRename(lv_obj_t *screen) {
  renameModal = lv_obj_create(screen);
  lv_obj_set_pos(renameModal, 0, 0);
  lv_obj_set_size(renameModal, 800, 480);
  lv_obj_set_style_radius(renameModal, 0, 0);
  lv_obj_set_style_border_width(renameModal, 0, 0);
  lv_obj_set_style_bg_color(renameModal, lv_color_hex(0xEEF3ED), 0);
  lv_obj_set_style_pad_all(renameModal, 0, 0);
  lv_obj_clear_flag(renameModal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *topBar = lv_obj_create(renameModal);
  lv_obj_set_pos(topBar, 0, 0);
  lv_obj_set_size(topBar, 800, 68);
  lv_obj_set_style_radius(topBar, 0, 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_style_bg_color(topBar, lv_color_hex(0x183E2B), 0);
  lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

  renameTitle = lv_label_create(topBar);
  lv_obj_set_style_text_font(renameTitle, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(renameTitle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(renameTitle, 18, 8);

  lv_obj_t *hint = lv_label_create(topBar);
  lv_label_set_text(hint, "Name stays tied to this sensor.");
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0xCDE0D2), 0);
  lv_obj_set_pos(hint, 20, 36);

  renameInput = lv_textarea_create(renameModal);
  lv_obj_set_pos(renameInput, 18, 82);
  lv_obj_set_size(renameInput, 764, 62);
  lv_textarea_set_one_line(renameInput, true);
  lv_textarea_set_max_length(renameInput, kPlantNameBytes - 1);
  lv_obj_set_style_text_font(renameInput, &lv_font_montserrat_28, 0);
  lv_obj_set_style_radius(renameInput, 12, 0);
  lv_obj_set_style_border_width(renameInput, 2, 0);
  lv_obj_set_style_border_color(renameInput, lv_color_hex(0x66A66F), 0);
  lv_obj_set_style_bg_color(renameInput, lv_color_hex(0xFFFFFF), 0);

  renameKeyboard = lv_btnmatrix_create(renameModal);
  lv_obj_set_pos(renameKeyboard, 18, 158);
  lv_obj_set_size(renameKeyboard, 764, 304);
  lv_btnmatrix_set_map(renameKeyboard, kRenameUpperMap);
  lv_obj_set_style_radius(renameKeyboard, 14, 0);
  lv_obj_set_style_border_width(renameKeyboard, 0, 0);
  lv_obj_set_style_bg_color(renameKeyboard, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(renameKeyboard, 8, 0);
  lv_obj_set_style_pad_row(renameKeyboard, 7, 0);
  lv_obj_set_style_pad_column(renameKeyboard, 7, 0);

  lv_obj_set_style_radius(renameKeyboard, 9, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(renameKeyboard, lv_color_hex(0xEDF3ED), LV_PART_ITEMS);
  lv_obj_set_style_text_color(renameKeyboard, lv_color_hex(0x173E2A), LV_PART_ITEMS);
  lv_obj_set_style_text_font(renameKeyboard, &lv_font_montserrat_18, LV_PART_ITEMS);

  lv_obj_add_event_cb(renameKeyboard, renameKeyboardEvent, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}

void buildUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xEEF3ED), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(0x142419), LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  buildHeader(screen);
  buildHome(screen);
  buildPlant(screen);
  buildSettings(screen);
  buildNav(screen);
  buildRename(screen);
  showPage(Page::Home);
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

void refreshUi() {
  if (!uiDirty && millis() - lastUiRefreshMs < kUiRefreshIntervalMs) return;
  lastUiRefreshMs = millis();
  uiDirty = false;
  if (!lvgl_port_lock(-1)) return;

  char text[128]{};
  const size_t count = registeredCount();
  snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(count), count == 1 ? "PLANT" : "PLANTS");
  label(headerCount, text);

  for (size_t i = 0; i < kMaxSensors; ++i) {
    if (!sensors[i].used) {
      lv_obj_add_flag(rows[i].box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(rows[i].box, LV_OBJ_FLAG_HIDDEN);
    label(rows[i].name, sensors[i].name);
    if (sensors[i].seenThisBoot && (sensors[i].fieldFlags & plantlink::SensorHasSoilMoisture)) {
      snprintf(text, sizeof(text), "%u%%", sensors[i].soilMoisturePct);
      lv_bar_set_value(rows[i].bar, sensors[i].soilMoisturePct, LV_ANIM_OFF);
    } else {
      snprintf(text, sizeof(text), "--%%");
      lv_bar_set_value(rows[i].bar, 0, LV_ANIM_OFF);
    }
    label(rows[i].moisture, text);
    lv_obj_set_style_bg_color(rows[i].box,
                              lv_color_hex(selectedSensor == static_cast<int>(i) ? 0xDDECDD : 0xF2F6F1), 0);
  }

  const bool valid = selectedSensor >= 0 && selectedSensor < static_cast<int>(kMaxSensors) && sensors[selectedSensor].used;
  if (!valid) {
    label(homeName, "WAITING FOR SENSOR");
    label(homeMood, "Pair a sensor and I'll keep an eye on it");
    label(homeSoil, "--%"); label(homeTemp, useFahrenheit ? "--.- F" : "--.- C"); label(homeHumidity, "--%");
    lv_bar_set_value(homeBar, 0, LV_ANIM_OFF); lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);
    label(detailSlot, "PLANT --"); label(detailName, "NO PLANT SELECTED"); label(detailMood, "--");
    label(detailIeee, "--"); label(detailSoil, "--%"); label(detailTemp, useFahrenheit ? "--.- F" : "--.- C");
    label(detailHumidity, "--%"); label(detailBattery, "--%"); label(detailSignal, "LQI --");
    label(detailUpdated, "No sensor data yet"); lv_bar_set_value(detailBar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN); lv_obj_add_state(renameButton, LV_STATE_DISABLED);
  } else {
    PlantSensor &s = sensors[selectedSensor];
    label(homeName, s.name); label(homeMood, mood(s));
    formatSoil(s, text, sizeof(text)); label(homeSoil, text);
    lv_bar_set_value(homeBar, s.seenThisBoot && (s.fieldFlags & plantlink::SensorHasSoilMoisture) ? s.soilMoisturePct : 0, LV_ANIM_OFF);
    formatTemp(s, text, sizeof(text)); label(homeTemp, text);
    formatHumidity(s, text, sizeof(text)); label(homeHumidity, text);
    if (s.seenThisBoot && (s.fieldFlags & plantlink::SensorHasWaterWarning) && s.waterWarning)
      lv_obj_clear_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(homeWarning, LV_OBJ_FLAG_HIDDEN);

    snprintf(text, sizeof(text), "PLANT %u", static_cast<unsigned>(selectedSensor + 1)); label(detailSlot, text);
    label(detailName, s.name); label(detailMood, mood(s));
    char ieee[24]{}; plantlink::formatIeee(s.ieee, ieee, sizeof(ieee));
    if (s.seenThisBoot) snprintf(text, sizeof(text), "%s   short 0x%04X", ieee, s.shortAddress);
    else snprintf(text, sizeof(text), "%s   waiting for check-in", ieee);
    label(detailIeee, text);
    formatSoil(s, text, sizeof(text)); label(detailSoil, text);
    lv_bar_set_value(detailBar, s.seenThisBoot && (s.fieldFlags & plantlink::SensorHasSoilMoisture) ? s.soilMoisturePct : 0, LV_ANIM_OFF);
    formatTemp(s, text, sizeof(text)); label(detailTemp, text);
    formatHumidity(s, text, sizeof(text)); label(detailHumidity, text);
    if (s.seenThisBoot && (s.fieldFlags & plantlink::SensorHasBattery)) snprintf(text, sizeof(text), "%u%%", s.batteryPct);
    else snprintf(text, sizeof(text), "--%%"); label(detailBattery, text);
    if (s.seenThisBoot) snprintf(text, sizeof(text), "LQI %u", s.lqi); else snprintf(text, sizeof(text), "LQI --");
    label(detailSignal, text);
    if (!s.seenThisBoot || !s.lastSeenMs) snprintf(text, sizeof(text), "Waiting for this plant to check in");
    else {
      const uint32_t age = (millis() - s.lastSeenMs) / 1000u;
      if (age < 2) snprintf(text, sizeof(text), "Updated now");
      else if (age < 60) snprintf(text, sizeof(text), "Updated %lus ago", static_cast<unsigned long>(age));
      else snprintf(text, sizeof(text), "Updated %lum ago", static_cast<unsigned long>(age / 60u));
    }
    label(detailUpdated, text);
    if (s.seenThisBoot && (s.fieldFlags & plantlink::SensorHasWaterWarning) && s.waterWarning)
      lv_obj_clear_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(detailWarning, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(renameButton, LV_STATE_DISABLED);
  }

  label(settingsH2, h2Online ? "ONLINE" : "OFFLINE");
  if (networkReady) snprintf(text, sizeof(text), "READY  CH %u  |  H2 SEES %u", zigbeeChannel, h2SensorCount);
  else if (h2Online) snprintf(text, sizeof(text), "STARTING");
  else snprintf(text, sizeof(text), "H2 OFFLINE");
  label(settingsZigbee, text);
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(count)); label(settingsPlants, text);
  label(settingsUnit, useFahrenheit ? "F" : "C");
  if (permitJoinRemaining) snprintf(text, sizeof(text), "PAIR %us", permitJoinRemaining);
  else snprintf(text, sizeof(text), "ADD SENSOR");
  label(settingsPair, text);

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
  size_t slot = 0;
  PlantSensor *s = findOrCreateSensor(frame.payload, plantlink::getU16LE(frame.payload + 8), &slot);
  char ieee[24]{}; plantlink::formatIeee(frame.payload, ieee, sizeof(ieee));
  Serial.printf("[zigbee] device seen: %s short=0x%04X slot=%u\n", ieee,
                plantlink::getU16LE(frame.payload + 8), s ? static_cast<unsigned>(slot + 1) : 0u);
  if (s) { s->seenThisBoot = true; s->lastSeenMs = millis(); uiDirty = true; }
}

void handleSensorReport(const plantlink::Frame &frame) {
  plantlink::SensorReportData report;
  if (!plantlink::parseSensorReport(frame.payload, frame.payloadLength, report)) return;
  size_t slot = 0;
  PlantSensor *s = findOrCreateSensor(report.ieee, report.shortAddress, &slot);
  if (!s) return;

  s->seenThisBoot = true;
  s->shortAddress = report.shortAddress;
  s->fieldFlags = report.fieldFlags;
  s->temperatureCentiC = report.temperatureCentiC;
  s->humidityCentiPct = report.humidityCentiPct;
  s->soilMoisturePct = report.soilMoisturePct;
  s->batteryPct = report.batteryPct;
  s->waterWarning = report.waterWarning;
  s->lqi = report.lqi;
  s->rssi = report.rssiDbm;
  s->lastSeenMs = millis();
  uiDirty = true;

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

void handleFrame(const plantlink::Frame &frame) {
  lastH2RxMs = millis();
  if (!h2Online) { h2Online = true; uiDirty = true; }
  switch (frame.type) {
    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1 ? frame.payloadLength : sizeof(build) - 1;
      memcpy(build, frame.payload, n);
      Serial.printf("[plantlink] H2 hello: %s\n", build);
      break;
    }
    case plantlink::MessageType::NetworkStatus: handleNetworkStatus(frame); break;
    case plantlink::MessageType::DeviceJoined: handleDeviceJoined(frame); break;
    case plantlink::MessageType::SensorReport: handleSensorReport(frame); break;
    default: break;
  }
}

void servicePlantLink() {
  plantlink::Frame frame;
  while (Serial0.available()) if (decoder.feed(static_cast<uint8_t>(Serial0.read()), frame)) handleFrame(frame);
  const uint32_t now = millis();
  if (!h2Online && now - lastHelloMs >= kHelloIntervalMs) { lastHelloMs = now; sendHello(); }
  if (h2Online && now - lastH2RxMs > kLinkTimeoutMs) {
    h2Online = false; networkReady = false; permitJoinRemaining = 0; uiDirty = true;
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

  preferences.begin("espplants", false);
  useFahrenheit = preferences.getBool("fahrenheit", true);
  Serial.printf("[settings] temperature units=%s\n", useFahrenheit ? "F" : "C");
  loadRegistry();

  Serial0.begin(kPlantLinkBaud, SERIAL_8N1, kPlantLinkRxPin, kPlantLinkTxPin);
  Serial.printf("[plantlink] UART0 RX=%d TX=%d baud=%lu\n", kPlantLinkRxPin, kPlantLinkTxPin,
                static_cast<unsigned long>(kPlantLinkBaud));

  Serial.println("[display] initializing Waveshare 800x480...");
  lcd_init();
  if (lvgl_port_lock(-1)) { buildUi(); lvgl_port_unlock(); }
  Serial.println("[display] ready");
  Serial.println("[plantlink] waiting for H2 frames");
}

void loop() {
  servicePlantLink();
  refreshUi();
  delay(2);
}
