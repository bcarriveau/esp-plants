#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>

#include <M5GFX.h>
#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

#include "plant_protocol.h"

using lgfx::epd_mode_t;

namespace {

// ============================================================================
// USER / PRODUCT SETTINGS
// ============================================================================

// Plant names are NOT compiled into firmware anymore. They are stored in NVS
// and edited from the T5 setup page.
//
// Short press of the existing side/function button toggles setup mode.
// Long hold requests the same true PMU shutdown already proven on this board.
constexpr uint32_t POWER_HOLD_MS = 1500;

// Protect the e-paper from unnecessary refreshes during development.
constexpr uint32_t MIN_NORMAL_REFRESH_MS = 60000;

// Setup hotspot. The password is generated once per T5 and stored in NVS.
constexpr uint16_t SETUP_HTTP_PORT = 80;
constexpr uint16_t SETUP_DNS_PORT = 53;
constexpr size_t MAX_PLANTS = 16;
constexpr size_t PLANT_NAME_LENGTH = 32;
constexpr size_t WIFI_SSID_LENGTH = 33;
constexpr size_t WIFI_PASSWORD_LENGTH = 65;
constexpr size_t SETUP_PASSWORD_LENGTH = 12;

// ============================================================================
// T5 S3 PRO HARDWARE — verified against LILYGO H752-01 sources
// ============================================================================

constexpr int PANEL_WIDTH = 960;
constexpr int PANEL_HEIGHT = 540;
constexpr int EPD_BUS_SPEED_HZ = 20000000;
constexpr int VCOM_MV = 1560;

constexpr int I2C_SDA = 39;
constexpr int I2C_SCL = 40;

constexpr uint8_t PCA9535_ADDR = 0x20;
constexpr uint8_t BQ25896_ADDR = 0x6B;
constexpr uint8_t TPS65185_ADDR = 0x68;

constexpr uint8_t PCA_INPUT_PORT0  = 0x00;
constexpr uint8_t PCA_OUTPUT_PORT0 = 0x02;
constexpr uint8_t PCA_CONFIG_PORT0 = 0x06;

// PCA9535 logical pin numbering: P0.0..P0.7 = 0..7, P1.0..P1.7 = 8..15.
constexpr uint8_t PCA_PIN_EPD_OE         = 8;
constexpr uint8_t PCA_PIN_EPD_MODE       = 9;
constexpr uint8_t PCA_PIN_BUTTON         = 10; // P1.2, active-low
constexpr uint8_t PCA_PIN_TPS_PWRUP      = 11;
constexpr uint8_t PCA_PIN_VCOM_CTRL      = 12;
constexpr uint8_t PCA_PIN_TPS_WAKEUP     = 13;
constexpr uint8_t PCA_PIN_TPS_POWER_GOOD = 14;

constexpr uint8_t TPS_REG_ENABLE = 0x01;
constexpr uint8_t TPS_REG_VCOM = 0x03;
constexpr uint8_t TPS_REG_POWER_GOOD = 0x0F;
constexpr uint8_t TPS_ENABLE_ALL_RAILS = 0x3F;
constexpr uint8_t TPS_POWER_GOOD_MASK = 0xFA;
constexpr uint8_t TPS_POWER_GOOD_EXPECTED = 0xFA;

constexpr gpio_num_t PIN_EPD_CKH = GPIO_NUM_4;
constexpr gpio_num_t PIN_EPD_D0 = GPIO_NUM_5;
constexpr gpio_num_t PIN_EPD_D1 = GPIO_NUM_6;
constexpr gpio_num_t PIN_EPD_D2 = GPIO_NUM_7;
constexpr gpio_num_t PIN_EPD_D7 = GPIO_NUM_8;
constexpr gpio_num_t PIN_TOUCH_RST = GPIO_NUM_9;
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_11;
constexpr gpio_num_t PIN_EPD_D3 = GPIO_NUM_15;
constexpr gpio_num_t PIN_EPD_D4 = GPIO_NUM_16;
constexpr gpio_num_t PIN_EPD_D5 = GPIO_NUM_17;
constexpr gpio_num_t PIN_EPD_D6 = GPIO_NUM_18;
constexpr gpio_num_t PIN_DUMMY_BUS = GPIO_NUM_1;
constexpr gpio_num_t PIN_EPD_STH = GPIO_NUM_41;
constexpr gpio_num_t PIN_EPD_LE = GPIO_NUM_42;
constexpr gpio_num_t PIN_EPD_STV = GPIO_NUM_45;
constexpr gpio_num_t PIN_EPD_CKV = GPIO_NUM_48;

constexpr uint8_t PANEL_OFFSET_ROTATION = 3;
constexpr int POWER_GOOD_TIMEOUT_MS = 400;

uint8_t pca_output[2] = {0xFF, 0x00};

XPowersPPM PPM;
bool pmu_ready = false;

// ============================================================================
// LOW-LEVEL I2C / E-PAPER POWER CONTROL
// ============================================================================

bool i2cWriteBytes(uint8_t address, const uint8_t* data, size_t length) {
  Wire.beginTransmission(address);
  const size_t written = Wire.write(data, length);
  return written == length && Wire.endTransmission() == 0;
}

bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
  const uint8_t buffer[2] = {reg, value};
  return i2cWriteBytes(address, buffer, sizeof(buffer));
}

bool i2cReadRegister(uint8_t address, uint8_t reg,
                     uint8_t* buffer, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  const size_t received =
      Wire.requestFrom(static_cast<int>(address), static_cast<int>(length));

  if (received != length) return false;

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool pca9535Init() {
  // Match LILYGO's current M5GFX example:
  // Port 1 bit 2 (button) remains input.
  pca_output[0] = 0xFF;
  pca_output[1] = 0x00;

  if (!i2cWriteRegister(PCA9535_ADDR, PCA_OUTPUT_PORT0 + 0, pca_output[0]))
    return false;
  if (!i2cWriteRegister(PCA9535_ADDR, PCA_OUTPUT_PORT0 + 1, pca_output[1]))
    return false;
  if (!i2cWriteRegister(PCA9535_ADDR, PCA_CONFIG_PORT0 + 0, 0x00))
    return false;
  if (!i2cWriteRegister(PCA9535_ADDR, PCA_CONFIG_PORT0 + 1, 0xC4))
    return false;

  return true;
}

bool pca9535SetLevel(uint8_t pin, bool level) {
  const uint8_t port = pin / 8;
  const uint8_t bit = pin & 0x07;

  if (level)
    pca_output[port] |= static_cast<uint8_t>(1U << bit);
  else
    pca_output[port] &= static_cast<uint8_t>(~(1U << bit));

  return i2cWriteRegister(
      PCA9535_ADDR, PCA_OUTPUT_PORT0 + port, pca_output[port]);
}

bool pca9535GetLevel(uint8_t pin, bool& level) {
  const uint8_t port = pin / 8;
  const uint8_t bit = pin & 0x07;
  uint8_t value = 0;

  if (!i2cReadRegister(
          PCA9535_ADDR, PCA_INPUT_PORT0 + port, &value, 1)) {
    return false;
  }

  level = (value & (1U << bit)) != 0;
  return true;
}

bool powerButtonPressed() {
  bool level = true;
  if (!pca9535GetLevel(PCA_PIN_BUTTON, level)) {
    return false;
  }
  return !level; // active-low, same as LILYGO factory code
}

bool tpsWriteRegister(uint8_t reg, const uint8_t* data, size_t length) {
  uint8_t buffer[4] = {reg, 0, 0, 0};
  if (length > sizeof(buffer) - 1) return false;

  for (size_t i = 0; i < length; ++i) {
    buffer[i + 1] = data[i];
  }

  return i2cWriteBytes(TPS65185_ADDR, buffer, length + 1);
}

bool tpsWriteRegisterU8(uint8_t reg, uint8_t value) {
  return tpsWriteRegister(reg, &value, 1);
}

bool tpsReadRegisterU8(uint8_t reg, uint8_t& value) {
  return i2cReadRegister(TPS65185_ADDR, reg, &value, 1);
}

class LilyGoT5ProEpdBus : public lgfx::Bus_EPD {
 public:
  bool init() override {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, LOW);

    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, HIGH);

    if (!pca9535Init()) {
      Serial.println("PCA9535 init failed");
      return false;
    }

    const bool ok = lgfx::Bus_EPD::init();
    if (!ok) Serial.println("Bus_EPD init failed");
    return ok;
  }

  bool powerControl(bool power_on) override {
    if (_pwr_on == power_on) return true;

    wait();
    const bool ok =
        power_on ? powerOnSequence() : powerOffSequence();

    if (!ok) {
      Serial.printf("EPD power %s failed\n",
                    power_on ? "on" : "off");
      return false;
    }

    _pwr_on = power_on;
    return true;
  }

 private:
  bool waitPcaPowerGood() {
    for (int i = 0; i < POWER_GOOD_TIMEOUT_MS; ++i) {
      bool level = false;
      if (!pca9535GetLevel(PCA_PIN_TPS_POWER_GOOD, level))
        return false;
      if (level) return true;
      delay(1);
    }
    return false;
  }

  bool waitTpsPowerGood() {
    for (int i = 0; i < POWER_GOOD_TIMEOUT_MS; ++i) {
      uint8_t value = 0;
      if (!tpsReadRegisterU8(TPS_REG_POWER_GOOD, value))
        return false;
      if ((value & TPS_POWER_GOOD_MASK) ==
          TPS_POWER_GOOD_EXPECTED)
        return true;
      delay(1);
    }
    return false;
  }

  bool powerOnSequence() {
    if (!pca9535SetLevel(PCA_PIN_EPD_OE, true)) return false;
    if (!pca9535SetLevel(PCA_PIN_EPD_MODE, true)) return false;
    if (!pca9535SetLevel(PCA_PIN_TPS_WAKEUP, true)) return false;
    if (!pca9535SetLevel(PCA_PIN_TPS_PWRUP, true)) return false;
    if (!pca9535SetLevel(PCA_PIN_VCOM_CTRL, true)) return false;

    delay(1);

    if (!waitPcaPowerGood()) return false;

    if (!tpsWriteRegisterU8(
            TPS_REG_ENABLE, TPS_ENABLE_ALL_RAILS))
      return false;

    const uint16_t vcom =
        static_cast<uint16_t>(VCOM_MV / 10);

    const uint8_t vcom_data[2] = {
        static_cast<uint8_t>(vcom & 0xFF),
        static_cast<uint8_t>((vcom >> 8) & 0xFF),
    };

    if (!tpsWriteRegister(
            TPS_REG_VCOM, vcom_data, sizeof(vcom_data)))
      return false;

    return waitTpsPowerGood();
  }

  bool powerOffSequence() {
    if (!pca9535SetLevel(PCA_PIN_EPD_OE, false)) return false;
    if (!pca9535SetLevel(PCA_PIN_EPD_MODE, false)) return false;
    if (!pca9535SetLevel(PCA_PIN_TPS_PWRUP, false)) return false;
    if (!pca9535SetLevel(PCA_PIN_VCOM_CTRL, false)) return false;

    delay(1);
    return pca9535SetLevel(PCA_PIN_TPS_WAKEUP, false);
  }
};

class LilyGoT5ProDisplay : public lgfx::LGFX_Device {
 public:
  LilyGoT5ProDisplay() {
    auto bus_cfg = bus_.config();
    bus_cfg.bus_speed = EPD_BUS_SPEED_HZ;
    bus_cfg.pin_data[0] = PIN_EPD_D0;
    bus_cfg.pin_data[1] = PIN_EPD_D1;
    bus_cfg.pin_data[2] = PIN_EPD_D2;
    bus_cfg.pin_data[3] = PIN_EPD_D3;
    bus_cfg.pin_data[4] = PIN_EPD_D4;
    bus_cfg.pin_data[5] = PIN_EPD_D5;
    bus_cfg.pin_data[6] = PIN_EPD_D6;
    bus_cfg.pin_data[7] = PIN_EPD_D7;
    bus_cfg.pin_pwr = PIN_DUMMY_BUS;
    bus_cfg.pin_spv = PIN_EPD_STV;
    bus_cfg.pin_ckv = PIN_EPD_CKV;
    bus_cfg.pin_sph = PIN_EPD_STH;
    bus_cfg.pin_oe = PIN_DUMMY_BUS;
    bus_cfg.pin_le = PIN_EPD_LE;
    bus_cfg.pin_cl = PIN_EPD_CKH;
    bus_cfg.bus_width = 8;
    bus_.config(bus_cfg);

    panel_.setBus(&bus_);

    auto panel_cfg = panel_.config();
    panel_cfg.memory_width = PANEL_WIDTH;
    panel_cfg.memory_height = PANEL_HEIGHT;
    panel_cfg.panel_width = PANEL_WIDTH;
    panel_cfg.panel_height = PANEL_HEIGHT;
    panel_cfg.offset_x = 0;
    panel_cfg.offset_y = 0;
    panel_cfg.offset_rotation = PANEL_OFFSET_ROTATION;
    panel_cfg.bus_shared = false;
    panel_.config(panel_cfg);

    auto detail = panel_.config_detail();
    detail.line_padding = 0;
    detail.task_priority = 3;
    panel_.config_detail(detail);

    setPanel(&panel_);
  }

 private:
  LilyGoT5ProEpdBus bus_;
  lgfx::Panel_EPD panel_;
};

LilyGoT5ProDisplay display;
bool display_ready = false;
// ============================================================================
// PERSISTENT CONFIGURATION + LIVE SENSOR DATABASE
// ============================================================================

constexpr uint32_t CONFIG_MAGIC = 0x50434647UL;  // "PCFG"
constexpr uint16_t CONFIG_VERSION = 1;
constexpr char CONFIG_NAMESPACE[] = "plantmon";
constexpr char CONFIG_KEY[] = "cfg";

constexpr uint32_t HOME_WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t DEVELOPMENT_WAKE_SECONDS = 60;

struct PersistedPlant {
  uint8_t used;
  uint8_t provisioned;
  uint8_t reserved[2];
  uint32_t sensor_id;
  uint8_t mac[6];
  char name[PLANT_NAME_LENGTH];
};

struct PersistedConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t structure_size;
  char wifi_ssid[WIFI_SSID_LENGTH];
  char wifi_password[WIFI_PASSWORD_LENGTH];
  char setup_password[SETUP_PASSWORD_LENGTH];
  PersistedPlant plants[MAX_PLANTS];
  uint32_t checksum;
};

struct LivePlant {
  bool used;
  uint32_t sensor_id;
  uint8_t mac[6];
  plant::ReadingPacket packet;
  uint32_t last_seen_ms;
  IPAddress source_ip;
  bool via_udp;
};

struct ReceivedEvent {
  uint8_t mac[6];
  plant::ReadingPacket packet;
  IPAddress source_ip;
  bool via_udp;
};

PersistedConfig config_data{};
LivePlant live_plants[MAX_PLANTS]{};

Preferences preferences;
WebServer web_server(SETUP_HTTP_PORT);
DNSServer dns_server;
WiFiUDP udp;

bool setup_mode = false;
bool web_routes_registered = false;
bool home_wifi_connected = false;
bool udp_ready = false;

bool deferred_exit_setup = false;
uint32_t deferred_exit_at_ms = 0;

String setup_ssid;
IPAddress setup_ip(192, 168, 4, 1);
IPAddress setup_gateway(192, 168, 4, 1);
IPAddress setup_subnet(255, 255, 255, 0);

QueueHandle_t receive_queue = nullptr;

plant::ReadingPacket displayed_packet{};
uint32_t displayed_sensor_id = 0;
bool have_displayed_packet = false;
uint32_t last_display_refresh_ms = 0;

// Last application-level provisioning acknowledgement.
volatile uint32_t provision_ack_sensor_id = 0;
volatile bool provision_ack_received = false;

// ============================================================================
// CONFIG STORAGE
// ============================================================================

uint32_t configChecksum(const PersistedConfig& source) {
  PersistedConfig copy = source;
  copy.checksum = 0;

  return plant::fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

void generateSetupPassword(
    char* output,
    size_t output_size) {
  snprintf(
      output,
      output_size,
      "PLT%08lX",
      static_cast<unsigned long>(esp_random()));
}

void initializeFreshConfig() {
  memset(&config_data, 0, sizeof(config_data));

  config_data.magic = CONFIG_MAGIC;
  config_data.version = CONFIG_VERSION;
  config_data.structure_size =
      sizeof(PersistedConfig);

  generateSetupPassword(
      config_data.setup_password,
      sizeof(config_data.setup_password));

  config_data.checksum =
      configChecksum(config_data);
}

bool saveConfig() {
  config_data.magic = CONFIG_MAGIC;
  config_data.version = CONFIG_VERSION;
  config_data.structure_size =
      sizeof(PersistedConfig);

  config_data.checksum =
      configChecksum(config_data);

  if (!preferences.begin(
          CONFIG_NAMESPACE, false)) {
    Serial.println(
        "NVS save failed: Preferences.begin().");
    return false;
  }

  const size_t written =
      preferences.putBytes(
          CONFIG_KEY,
          &config_data,
          sizeof(config_data));

  preferences.end();

  if (written != sizeof(config_data)) {
    Serial.printf(
        "NVS save failed: wrote %u/%u bytes.\n",
        static_cast<unsigned>(written),
        static_cast<unsigned>(
            sizeof(config_data)));
    return false;
  }

  Serial.println(
      "T5 configuration saved.");
  return true;
}

bool loadConfig() {
  initializeFreshConfig();

  if (!preferences.begin(
          CONFIG_NAMESPACE, false)) {
    Serial.println(
        "NVS unavailable; using temporary defaults.");
    return false;
  }

  const size_t stored_size =
      preferences.getBytesLength(CONFIG_KEY);

  bool valid = false;

  if (stored_size == sizeof(config_data)) {
    const size_t read =
        preferences.getBytes(
            CONFIG_KEY,
            &config_data,
            sizeof(config_data));

    valid =
        read == sizeof(config_data) &&
        config_data.magic == CONFIG_MAGIC &&
        config_data.version == CONFIG_VERSION &&
        config_data.structure_size ==
            sizeof(PersistedConfig) &&
        config_data.checksum ==
            configChecksum(config_data);
  }

  preferences.end();

  if (!valid) {
    Serial.println(
        "No compatible Phase-3B config found; "
        "creating fresh configuration.");

    initializeFreshConfig();
    saveConfig();
    return false;
  }

  Serial.println(
      "T5 configuration loaded.");
  return true;
}

bool wifiCredentialsSaved() {
  return config_data.wifi_ssid[0] != '\0';
}

// ============================================================================
// PLANT DATABASE
// ============================================================================

int findPersistedPlant(uint32_t sensor_id) {
  for (size_t i = 0; i < MAX_PLANTS; ++i) {
    if (config_data.plants[i].used &&
        config_data.plants[i].sensor_id ==
            sensor_id) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

int findLivePlant(uint32_t sensor_id) {
  for (size_t i = 0; i < MAX_PLANTS; ++i) {
    if (live_plants[i].used &&
        live_plants[i].sensor_id ==
            sensor_id) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

int ensurePersistedPlant(
    uint32_t sensor_id,
    const uint8_t* mac) {
  int index =
      findPersistedPlant(sensor_id);

  if (index >= 0) {
    if (mac != nullptr) {
      memcpy(
          config_data.plants[index].mac,
          mac,
          6);
    }
    return index;
  }

  for (size_t i = 0; i < MAX_PLANTS; ++i) {
    if (config_data.plants[i].used)
      continue;

    PersistedPlant& record =
        config_data.plants[i];

    memset(&record, 0, sizeof(record));

    record.used = 1;
    record.sensor_id = sensor_id;

    if (mac != nullptr) {
      memcpy(record.mac, mac, 6);
    }

    snprintf(
        record.name,
        sizeof(record.name),
        "Plant %u",
        static_cast<unsigned>(i + 1));

    Serial.printf(
        "New sensor 0x%08lX registered as \"%s\".\n",
        static_cast<unsigned long>(sensor_id),
        record.name);

    saveConfig();
    return static_cast<int>(i);
  }

  Serial.println(
      "Plant database full.");
  return -1;
}

int ensureLivePlant(
    uint32_t sensor_id,
    const uint8_t* mac) {
  int index =
      findLivePlant(sensor_id);

  if (index >= 0) {
    if (mac != nullptr) {
      memcpy(
          live_plants[index].mac,
          mac,
          6);
    }
    return index;
  }

  for (size_t i = 0; i < MAX_PLANTS; ++i) {
    if (live_plants[i].used)
      continue;

    live_plants[i].used = true;
    live_plants[i].sensor_id =
        sensor_id;

    if (mac != nullptr) {
      memcpy(
          live_plants[i].mac,
          mac,
          6);
    }

    return static_cast<int>(i);
  }

  return -1;
}

String sensorIdToHex(uint32_t sensor_id) {
  char buffer[9];

  snprintf(
      buffer,
      sizeof(buffer),
      "%08lX",
      static_cast<unsigned long>(sensor_id));

  return String(buffer);
}

String macToString(const uint8_t* mac) {
  char buffer[18];

  snprintf(
      buffer,
      sizeof(buffer),
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5]);

  return String(buffer);
}

String plantNameFor(uint32_t sensor_id) {
  const int index =
      findPersistedPlant(sensor_id);

  if (index >= 0 &&
      config_data.plants[index].name[0] != '\0') {
    return String(
        config_data.plants[index].name);
  }

  return String("Sensor ") +
         sensorIdToHex(sensor_id);
}

String htmlEscape(const String& input) {
  String output;
  output.reserve(input.length() + 16);

  for (size_t i = 0;
       i < input.length();
       ++i) {
    const char c = input[i];

    switch (c) {
      case '&': output += F("&amp;"); break;
      case '<': output += F("&lt;"); break;
      case '>': output += F("&gt;"); break;
      case '"': output += F("&quot;"); break;
      case '\'': output += F("&#39;"); break;
      default: output += c; break;
    }
  }

  return output;
}

String sanitizePlantName(String value) {
  value.trim();

  String clean;
  clean.reserve(PLANT_NAME_LENGTH);

  for (size_t i = 0;
       i < value.length() &&
       clean.length() <
           (PLANT_NAME_LENGTH - 1);
       ++i) {
    const char c = value[i];

    if (static_cast<uint8_t>(c) >= 32 &&
        c != '<' &&
        c != '>') {
      clean += c;
    }
  }

  return clean;
}

// ============================================================================
// E-PAPER UI
// ============================================================================

uint32_t gray(uint8_t value) {
  return display.color888(
      value, value, value);
}

void beginFrame() {
  display.setAutoDisplay(false);
  display.setColorDepth(4);
  display.setRotation(1);
  display.setEpdMode(
      epd_mode_t::epd_quality);

  display.startWrite();
  display.fillScreen(TFT_WHITE);
}

void finishFrame() {
  display.endWrite();
  display.display();
  display.waitDisplay();
  display.powerSaveOn();

  last_display_refresh_ms = millis();
}

void drawHeader(const char* subtitle) {
  const int w = display.width();

  display.fillRect(
      0, 0, w, 82, gray(235));

  display.drawFastHLine(
      0, 81, w, TFT_BLACK);

  display.setTextColor(
      TFT_BLACK, gray(235));

  display.setTextDatum(
      textdatum_t::middle_left);

  display.setFont(&fonts::Font4);
  display.drawString(
      "PLANT MONITOR", 28, 31);

  display.setFont(&fonts::Font2);
  display.drawString(
      subtitle, 30, 61);
}

void drawWaitingScreen() {
  if (!display_ready) return;

  beginFrame();

  drawHeader(
      home_wifi_connected
          ? "HOME WI-FI / UDP"
          : "WAITING");

  display.setTextColor(
      TFT_BLACK, TFT_WHITE);

  display.setTextDatum(
      textdatum_t::middle_center);

  display.setFont(&fonts::Font4);

  display.drawString(
      "Waiting for plant sensor...",
      display.width() / 2,
      display.height() / 2 - 50);

  display.setFont(&fonts::Font2);

  if (home_wifi_connected) {
    display.drawString(
        String("T5 IP: ") +
            WiFi.localIP().toString(),
        display.width() / 2,
        display.height() / 2 + 10);

    display.drawString(
        "Normal readings use the home router / mesh",
        display.width() / 2,
        display.height() / 2 + 45);
  } else {
    display.drawString(
        "Short press: setup / provisioning",
        display.width() / 2,
        display.height() / 2 + 15);
  }

  display.drawString(
      "Long hold: power off",
      display.width() / 2,
      display.height() - 55);

  finishFrame();
}

void drawPlantScreen(
    const plant::ReadingPacket& packet,
    const String& plant_name) {
  if (!display_ready) return;

  beginFrame();
  drawHeader(
      home_wifi_connected
          ? "HOME WI-FI / UDP"
          : "LOCAL SETUP");

  const int w = display.width();
  const int h = display.height();

  display.setTextColor(
      TFT_BLACK, TFT_WHITE);

  display.setTextDatum(
      textdatum_t::top_left);

  display.setFont(&fonts::Font4);
  display.drawString(
      plant_name, 38, 112);

  display.setTextDatum(
      textdatum_t::middle_center);

  display.setFont(&fonts::Font8);
  display.drawString(
      String(packet.moisture_percent) + "%",
      w / 2, 235);

  display.setFont(&fonts::Font4);
  display.drawString(
      plant::stateName(
          packet.moisture_state),
      w / 2, 335);

  display.fillRoundRect(
      36, h - 118, w - 72, 78,
      14, gray(240));

  display.drawRoundRect(
      36, h - 118, w - 72, 78,
      14, TFT_BLACK);

  display.setTextDatum(
      textdatum_t::middle_left);

  display.setFont(&fonts::Font2);
  display.setTextColor(
      TFT_BLACK, gray(240));

  display.drawString(
      String("Sensor battery: ") +
          String(packet.battery_percent) +
          "%",
      62, h - 80);

  display.setTextDatum(
      textdatum_t::middle_center);

  display.drawString(
      String("ID: ") +
          sensorIdToHex(packet.sensor_id),
      w / 2, h - 80);

  display.setTextDatum(
      textdatum_t::middle_right);

  display.drawString(
      String("Seq: ") +
          String(packet.sequence),
      w - 62, h - 80);

  finishFrame();
}

void drawSetupScreen() {
  if (!display_ready) return;

  beginFrame();
  drawHeader("SETUP / PROVISIONING");

  display.setTextColor(
      TFT_BLACK, TFT_WHITE);

  display.setTextDatum(
      textdatum_t::middle_center);

  display.setFont(&fonts::Font4);

  display.drawString(
      "Connect to:",
      display.width() / 2,
      135);

  display.drawString(
      setup_ssid,
      display.width() / 2,
      195);

  display.setFont(&fonts::Font2);

  display.drawString(
      String("Password: ") +
          String(config_data.setup_password),
      display.width() / 2,
      250);

  display.setFont(&fonts::Font4);

  display.drawString(
      "Open 192.168.4.1",
      display.width() / 2,
      315);

  display.setFont(&fonts::Font2);

  display.drawString(
      wifiCredentialsSaved()
          ? String("Saved home Wi-Fi: ") +
                String(config_data.wifi_ssid)
          : String(
                "Save home Wi-Fi on the setup page"),
      display.width() / 2,
      370);

  display.drawString(
      "Use the page to rename and provision sensors",
      display.width() / 2,
      415);

  display.drawString(
      "Short press again to return to home Wi-Fi",
      display.width() / 2,
      465);

  finishFrame();
}

void drawPowerOffScreen() {
  if (!display_ready) return;

  beginFrame();

  display.setTextColor(
      TFT_BLACK, TFT_WHITE);

  display.setTextDatum(
      textdatum_t::middle_center);

  display.setFont(&fonts::Font4);

  display.drawString(
      "POWERING OFF",
      display.width() / 2,
      display.height() / 2 - 30);

  display.setFont(&fonts::Font2);

  display.drawString(
      "Use RST to start again",
      display.width() / 2,
      display.height() / 2 + 25);

  finishFrame();
}

bool shouldRefreshFor(
    const plant::ReadingPacket& packet) {
  if (!have_displayed_packet)
    return true;

  if (packet.sensor_id !=
      displayed_sensor_id)
    return true;

  if (packet.moisture_state !=
      displayed_packet.moisture_state)
    return true;

  const int moisture_delta =
      abs(
          static_cast<int>(
              packet.moisture_percent) -
          static_cast<int>(
              displayed_packet.moisture_percent));

  if (moisture_delta >= 3)
    return true;

  const int battery_delta =
      abs(
          static_cast<int>(
              packet.battery_percent) -
          static_cast<int>(
              displayed_packet.battery_percent));

  if (battery_delta >= 2)
    return true;

  return (millis() -
          last_display_refresh_ms) >=
         MIN_NORMAL_REFRESH_MS;
}

void restoreNormalDisplay() {
  if (!display_ready) return;

  if (setup_mode) {
    drawSetupScreen();
    return;
  }

  if (have_displayed_packet) {
    drawPlantScreen(
        displayed_packet,
        plantNameFor(
            displayed_sensor_id));
    return;
  }

  drawWaitingScreen();
}

// ============================================================================
// RADIO MODE CONTROL
// ============================================================================

bool setProvisionChannel() {
  const esp_err_t result =
      esp_wifi_set_channel(
          plant::PROVISION_ESPNOW_CHANNEL,
          WIFI_SECOND_CHAN_NONE);

  if (result != ESP_OK) {
    Serial.printf(
        "Provision channel failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  return true;
}

bool initEspNowOnce() {
  static bool initialized = false;

  if (initialized) return true;

  const esp_err_t result =
      esp_now_init();

  if (result != ESP_OK) {
    Serial.printf(
        "ESP-NOW init failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  initialized = true;
  return true;
}

bool ensureEspNowPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac))
    return true;

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  peer.ifidx = WIFI_IF_STA;
#endif

  const esp_err_t result =
      esp_now_add_peer(&peer);

  if (result != ESP_OK) {
    Serial.printf(
        "Could not add ESP-NOW peer: %s\n",
        esp_err_to_name(result));
    return false;
  }

  return true;
}

// ============================================================================
// UDP NORMAL TRANSPORT
// ============================================================================

void stopUdp() {
  if (udp_ready) {
    udp.stop();
    udp_ready = false;
  }
}

bool startUdp() {
  stopUdp();

  if (!home_wifi_connected)
    return false;

  if (!udp.begin(plant::T5_UDP_PORT)) {
    Serial.println(
        "T5 UDP listener failed.");
    return false;
  }

  udp_ready = true;

  Serial.printf(
      "T5 UDP listening on port %u.\n",
      plant::T5_UDP_PORT);

  return true;
}

void sendUdpAck(
    const plant::ReadingPacket& reading,
    const IPAddress& remote_ip,
    uint16_t remote_port) {
  plant::AckPacket ack{};

  ack.magic = plant::PACKET_MAGIC;
  ack.version = plant::PROTOCOL_VERSION;
  ack.packet_size = sizeof(ack);
  ack.sensor_id = reading.sensor_id;
  ack.sequence = reading.sequence;
  ack.accepted = 1;
  ack.next_wake_seconds =
      DEVELOPMENT_WAKE_SECONDS;

  plant::finalizePacket(ack);

  if (!udp.beginPacket(
          remote_ip,
          remote_port)) {
    Serial.println(
        "UDP ACK beginPacket failed.");
    return;
  }

  udp.write(
      reinterpret_cast<const uint8_t*>(&ack),
      sizeof(ack));

  const int result =
      udp.endPacket();

  Serial.printf(
      "UDP ACK to %s:%u: %s\n",
      remote_ip.toString().c_str(),
      remote_port,
      result == 1 ? "SENT" : "FAILED");
}

void queueReceivedReading(
    const uint8_t* mac,
    const plant::ReadingPacket& packet,
    const IPAddress& source_ip,
    bool via_udp) {
  if (receive_queue == nullptr)
    return;

  ReceivedEvent event{};

  if (mac != nullptr)
    memcpy(event.mac, mac, 6);

  event.packet = packet;
  event.source_ip = source_ip;
  event.via_udp = via_udp;

  if (xQueueSend(
          receive_queue,
          &event,
          0) != pdPASS) {
    Serial.println(
        "Receive queue full.");
  }
}

void serviceUdp() {
  if (!udp_ready) return;

  const int packet_size =
      udp.parsePacket();

  if (packet_size <= 0)
    return;

  const IPAddress source_ip =
      udp.remoteIP();

  const uint16_t source_port =
      udp.remotePort();

  if (packet_size !=
      sizeof(plant::ReadingPacket)) {
    Serial.printf(
        "Ignoring UDP packet of %d bytes.\n",
        packet_size);

    while (udp.available())
      udp.read();

    return;
  }

  plant::ReadingPacket packet{};

  const int read =
      udp.read(
          reinterpret_cast<uint8_t*>(&packet),
          sizeof(packet));

  if (read != sizeof(packet) ||
      !plant::validatePacket(packet)) {
    Serial.println(
        "Rejected invalid UDP plant packet.");
    return;
  }

  Serial.println();
  Serial.println(
      "--- HOME WI-FI UDP SENSOR PACKET ---");

  Serial.printf(
      "Sensor ID: 0x%08lX\n",
      static_cast<unsigned long>(
          packet.sensor_id));

  Serial.printf(
      "Moisture: %u%% (%s)\n",
      packet.moisture_percent,
      plant::stateName(
          packet.moisture_state));

  Serial.printf(
      "Battery: %u%% (%u mV)\n",
      packet.battery_percent,
      packet.battery_mv);

  Serial.print("From IP: ");
  Serial.println(source_ip);

  queueReceivedReading(
      nullptr,
      packet,
      source_ip,
      true);

  sendUdpAck(
      packet,
      source_ip,
      source_port);
}

// ============================================================================
// ESP-NOW PROVISIONING / LOCAL DISCOVERY
// ============================================================================

void sendEspNowAck(
    const uint8_t* mac,
    const plant::ReadingPacket& reading) {
  if (!ensureEspNowPeer(mac))
    return;

  plant::AckPacket ack{};

  ack.magic = plant::PACKET_MAGIC;
  ack.version = plant::PROTOCOL_VERSION;
  ack.packet_size = sizeof(ack);
  ack.sensor_id = reading.sensor_id;
  ack.sequence = reading.sequence;
  ack.accepted = 1;
  ack.next_wake_seconds =
      DEVELOPMENT_WAKE_SECONDS;

  plant::finalizePacket(ack);

  esp_now_send(
      mac,
      reinterpret_cast<const uint8_t*>(&ack),
      sizeof(ack));
}

void handleEspNowData(
    const uint8_t* mac,
    const uint8_t* data,
    int length) {
  if (length ==
      sizeof(plant::ReadingPacket)) {
    plant::ReadingPacket packet{};
    memcpy(&packet, data, sizeof(packet));

    if (!plant::validatePacket(packet))
      return;

    Serial.println();
    Serial.println(
        "--- LOCAL ESP-NOW SENSOR BEACON ---");

    Serial.printf(
        "Sensor ID: 0x%08lX\n",
        static_cast<unsigned long>(
            packet.sensor_id));

    queueReceivedReading(
        mac,
        packet,
        IPAddress(),
        false);

    sendEspNowAck(mac, packet);
    return;
  }

  if (length ==
      sizeof(plant::ProvisionAckPacket)) {
    plant::ProvisionAckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    if (!plant::validatePacket(ack))
      return;

    provision_ack_sensor_id =
        ack.sensor_id;

    provision_ack_received =
        ack.accepted == 1;

    Serial.printf(
        "Provision ACK from sensor "
        "0x%08lX.\n",
        static_cast<unsigned long>(
            ack.sensor_id));
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void onEspNowReceive(
    const esp_now_recv_info_t* info,
    const uint8_t* data,
    int length) {
  handleEspNowData(
      info->src_addr,
      data,
      length);
}

void onEspNowSent(
    const wifi_tx_info_t* info,
    esp_now_send_status_t status) {
  (void)info;

  Serial.printf(
      "ESP-NOW send result: %s\n",
      status == ESP_NOW_SEND_SUCCESS
          ? "DELIVERED"
          : "FAILED");
}

#else

void onEspNowReceive(
    const uint8_t* mac,
    const uint8_t* data,
    int length) {
  handleEspNowData(
      mac,
      data,
      length);
}

void onEspNowSent(
    const uint8_t* mac,
    esp_now_send_status_t status) {
  (void)mac;

  Serial.printf(
      "ESP-NOW send result: %s\n",
      status == ESP_NOW_SEND_SUCCESS
          ? "DELIVERED"
          : "FAILED");
}

#endif

bool startProvisioningRadio() {
  if (!WiFi.mode(WIFI_AP_STA)) {
    Serial.println(
        "Could not enter AP+STA mode.");
    return false;
  }

  delay(100);

  if (!setProvisionChannel())
    return false;

  if (!initEspNowOnce())
    return false;

  static bool callbacks_registered = false;

  if (!callbacks_registered) {
    esp_now_register_recv_cb(
        onEspNowReceive);

    esp_now_register_send_cb(
        onEspNowSent);

    callbacks_registered = true;
  }

  return true;
}

bool sendProvisionPacket(
    uint32_t sensor_id) {
  if (!setup_mode) {
    Serial.println(
        "Provisioning requires setup mode.");
    return false;
  }

  if (!wifiCredentialsSaved()) {
    Serial.println(
        "Provisioning requires saved "
        "home Wi-Fi first.");
    return false;
  }

  const int index =
      findPersistedPlant(sensor_id);

  if (index < 0) {
    Serial.println(
        "Provision target not found.");
    return false;
  }

  PersistedPlant& record =
      config_data.plants[index];

  if (!ensureEspNowPeer(record.mac))
    return false;

  plant::ProvisionPacket packet{};

  packet.magic = plant::PACKET_MAGIC;
  packet.version = plant::PROTOCOL_VERSION;
  packet.packet_size = sizeof(packet);
  packet.sensor_id = sensor_id;

  strlcpy(
      packet.wifi_ssid,
      config_data.wifi_ssid,
      sizeof(packet.wifi_ssid));

  strlcpy(
      packet.wifi_password,
      config_data.wifi_password,
      sizeof(packet.wifi_password));

  packet.t5_udp_port =
      plant::T5_UDP_PORT;

  plant::finalizePacket(packet);

  provision_ack_received = false;
  provision_ack_sensor_id = 0;

  Serial.printf(
      "Provisioning sensor 0x%08lX...\n",
      static_cast<unsigned long>(
          sensor_id));

  // A few sends make the manual provisioning action forgiving.
  for (uint8_t attempt = 1;
       attempt <= 6;
       ++attempt) {
    const esp_err_t result =
        esp_now_send(
            record.mac,
            reinterpret_cast<const uint8_t*>(&packet),
            sizeof(packet));

    Serial.printf(
        "Provision send %u/6: %s\n",
        attempt,
        esp_err_to_name(result));

    const uint32_t wait_start =
        millis();

    while ((millis() - wait_start) <
           450) {
      if (provision_ack_received &&
          provision_ack_sensor_id ==
              sensor_id) {
        record.provisioned = 1;
        saveConfig();

        Serial.println(
            "Sensor provisioning confirmed.");
        return true;
      }

      delay(10);
    }
  }

  Serial.println(
      "No application provisioning ACK "
      "received.");

  return false;
}

// ============================================================================
// HOME WI-FI / SETUP AP
// ============================================================================

String setupNetworkName() {
  String mac = WiFi.macAddress();

  if (mac.length() >= 5) {
    String suffix =
        mac.substring(
            mac.length() - 5);

    suffix.replace(":", "");

    return String("PlantMonitor-") +
           suffix;
  }

  return F("PlantMonitor-Setup");
}

void stopHomeWifi() {
  stopUdp();

  home_wifi_connected = false;

  WiFi.disconnect(true, false);
  delay(100);
}

bool connectHomeWifi() {
  if (!wifiCredentialsSaved()) {
    return false;
  }

  dns_server.stop();
  WiFi.softAPdisconnect(true);

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println(
        "Could not enable home Wi-Fi STA.");
    return false;
  }

  Serial.printf(
      "Connecting T5 to \"%s\"...\n",
      config_data.wifi_ssid);

  WiFi.begin(
      config_data.wifi_ssid,
      config_data.wifi_password);

  const uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start) >=
        HOME_WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(
          "T5 home Wi-Fi connection timed out.");

      WiFi.disconnect(true, false);
      home_wifi_connected = false;
      return false;
    }

    delay(100);
  }

  home_wifi_connected = true;

  Serial.println(
      "T5 home Wi-Fi connected.");

  Serial.print("T5 IP: ");
  Serial.println(WiFi.localIP());

  Serial.printf(
      "Home channel: %d\n",
      WiFi.channel());

  web_server.begin();
  startUdp();

  return true;
}

bool startSetupMode() {
  if (setup_mode)
    return true;

  stopHomeWifi();

  if (!startProvisioningRadio())
    return false;

  setup_ssid =
      setupNetworkName();

  if (!WiFi.softAPConfig(
          setup_ip,
          setup_gateway,
          setup_subnet)) {
    Serial.println(
        "softAPConfig failed.");
    return false;
  }

  if (!WiFi.softAP(
          setup_ssid.c_str(),
          config_data.setup_password,
          plant::PROVISION_ESPNOW_CHANNEL,
          0,
          4)) {
    Serial.println(
        "Setup SoftAP failed.");
    return false;
  }

  dns_server.start(
      SETUP_DNS_PORT,
      "*",
      WiFi.softAPIP());

  web_server.begin();

  setup_mode = true;

  Serial.println();
  Serial.println(
      "=== T5 SETUP / PROVISIONING ===");

  Serial.printf(
      "SSID: %s\n",
      setup_ssid.c_str());

  Serial.printf(
      "Password: %s\n",
      config_data.setup_password);

  Serial.printf(
      "Open: http://%s/\n",
      WiFi.softAPIP().toString().c_str());

  drawSetupScreen();
  return true;
}

void stopSetupModeAndConnectHome() {
  if (setup_mode) {
    dns_server.stop();
    WiFi.softAPdisconnect(true);
    setup_mode = false;
  }

  if (!connectHomeWifi()) {
    Serial.println(
        "Could not return to home Wi-Fi; "
        "re-entering setup mode.");

    startSetupMode();
    return;
  }

  restoreNormalDisplay();
}

// ============================================================================
// WEB UI
// ============================================================================

String buildWebPage() {
  String html;
  html.reserve(16000);

  html += F(
      "<!doctype html><html><head>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Plant Monitor</title>"
      "<style>"
      "body{font-family:Arial,sans-serif;background:#f2f2f2;color:#171717;margin:0}"
      ".wrap{max-width:850px;margin:auto;padding:18px}"
      ".card{background:#fff;border-radius:14px;padding:18px;margin:14px 0;"
      "box-shadow:0 2px 8px #0002}"
      ".sub{background:#fafafa;border:1px solid #ddd}"
      "h1{margin:4px 0}h2{margin-top:0}"
      "label{display:block;font-weight:bold;margin:12px 0 5px}"
      "input{box-sizing:border-box;width:100%;padding:11px;border:1px solid #aaa;"
      "border-radius:8px;font-size:16px}"
      "button{padding:11px 15px;border:0;border-radius:8px;background:#222;color:#fff;"
      "font-size:15px;cursor:pointer}"
      ".muted{color:#666}.pill{background:#eee;border-radius:8px;padding:10px}"
      ".good{background:#e7f4e8}.warn{background:#fff1d2}"
      ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:8px}"
      "code{font-size:14px}"
      "</style></head><body><div class='wrap'>");

  html += F("<h1>Plant Monitor</h1>");

  html += F("<div class='muted'>");

  if (setup_mode) {
    html += F("Setup network: ");
    html += htmlEscape(setup_ssid);
    html += F(" &bull; ");
    html += WiFi.softAPIP().toString();
  } else if (home_wifi_connected) {
    html += F("Home Wi-Fi: ");
    html += htmlEscape(
        String(config_data.wifi_ssid));
    html += F(" &bull; T5 IP ");
    html += WiFi.localIP().toString();
  } else {
    html += F("Not connected");
  }

  html += F("</div>");

  html += F(
      "<div class='card'><h2>Home Wi-Fi</h2>");

  if (wifiCredentialsSaved()) {
    html += F(
        "<div class='pill good'>Saved: <strong>");

    html += htmlEscape(
        String(config_data.wifi_ssid));

    html += F("</strong></div>");
  } else {
    html += F(
        "<div class='pill warn'>No home Wi-Fi saved.</div>");
  }

  html += F(
      "<form method='post' action='/wifi'>"
      "<label>Wi-Fi name (SSID)</label>"
      "<input name='ssid' maxlength='32' required value='");

  html += htmlEscape(
      String(config_data.wifi_ssid));

  html += F(
      "'><label>Wi-Fi password</label>"
      "<input type='password' name='password' maxlength='64' "
      "placeholder='Enter password'>"
      "<p class='muted'>Password is stored on the T5 but never displayed back.</p>"
      "<button type='submit'>Save Home Wi-Fi</button>"
      "</form>");

  if (setup_mode &&
      wifiCredentialsSaved()) {
    html += F(
        "<form method='post' action='/home' style='margin-top:12px'>"
        "<button type='submit'>Finish Setup & Connect Home Wi-Fi</button>"
        "</form>");
  }

  html += F("</div>");

  html += F(
      "<div class='card'><h2>Plant Sensors</h2>");

  bool any = false;

  for (size_t i = 0;
       i < MAX_PLANTS;
       ++i) {
    if (!config_data.plants[i].used)
      continue;

    any = true;

    const PersistedPlant& record =
        config_data.plants[i];

    const int live_index =
        findLivePlant(record.sensor_id);

    html += F(
        "<div class='card sub'>");

    html += F("<h2>");
    html += htmlEscape(
        String(record.name));
    html += F("</h2>");

    html += F("<div class='muted'><code>0x");
    html += sensorIdToHex(
        record.sensor_id);
    html += F("</code><br><code>");
    html += macToString(record.mac);
    html += F("</code></div>");

    html += F("<div style='margin:10px 0'>");

    if (record.provisioned) {
      html += F(
          "<span class='pill good'>Wi-Fi provisioned</span>");
    } else {
      html += F(
          "<span class='pill warn'>Wi-Fi not confirmed</span>");
    }

    html += F("</div>");

    if (live_index >= 0) {
      const LivePlant& live =
          live_plants[live_index];

      html += F("<div class='grid'>");

      html += F(
          "<div class='pill'><strong>Moisture</strong><br>");
      html += String(
          live.packet.moisture_percent);
      html += F("% &mdash; ");
      html += plant::stateName(
          live.packet.moisture_state);
      html += F("</div>");

      html += F(
          "<div class='pill'><strong>Battery</strong><br>");
      html += String(
          live.packet.battery_percent);
      html += F("%</div>");

      html += F(
          "<div class='pill'><strong>Transport</strong><br>");
      html += live.via_udp
          ? F("Home Wi-Fi")
          : F("Local ESP-NOW");
      html += F("</div>");

      html += F(
          "<div class='pill'><strong>Last seen</strong><br>");
      html += String(
          (millis() -
           live.last_seen_ms) /
          1000UL);
      html += F(" sec ago</div>");

      html += F("</div>");
    }

    html += F(
        "<form method='post' action='/rename'>"
        "<input type='hidden' name='id' value='");

    html += sensorIdToHex(
        record.sensor_id);

    html += F(
        "'><label>Plant name</label>"
        "<input name='name' maxlength='31' required value='");

    html += htmlEscape(
        String(record.name));

    html += F(
        "'><div style='margin-top:10px'>"
        "<button type='submit'>Save Plant Name</button>"
        "</div></form>");

    if (setup_mode &&
        wifiCredentialsSaved()) {
      html += F(
          "<form method='post' action='/provision' style='margin-top:12px'>"
          "<input type='hidden' name='id' value='");

      html += sensorIdToHex(
          record.sensor_id);

      html += F(
          "'><button type='submit'>Send Wi-Fi to Sensor</button>"
          "</form>"
          "<p class='muted'>The XIAO must currently be awake in provisioning mode nearby.</p>");
    }

    html += F("</div>");
  }

  if (!any) {
    html += F(
        "<p>No sensors registered yet.</p>"
        "<p class='muted'>An unprovisioned XIAO sends local ESP-NOW beacons "
        "while it waits for Wi-Fi provisioning.</p>");
  }

  html += F("</div>");

  if (!setup_mode) {
    html += F(
        "<div class='card'><h2>Setup / Provisioning Mode</h2>"
        "<p>Short-press the T5 side/function button to switch from home Wi-Fi "
        "to the local setup network.</p></div>");
  }

  html += F(
      "<div class='muted' style='text-align:center;padding:16px'>"
      "Plant names live only on the T5."
      "</div></div></body></html>");

  return html;
}

void redirectToRoot() {
  web_server.sendHeader(
      "Location", "/", true);

  web_server.send(
      303,
      "text/plain",
      "");
}

void handleRoot() {
  web_server.send(
      200,
      "text/html; charset=utf-8",
      buildWebPage());
}

void handleSaveWifi() {
  String ssid =
      web_server.arg("ssid");

  String password =
      web_server.arg("password");

  ssid.trim();

  if (ssid.length() == 0 ||
      ssid.length() >= WIFI_SSID_LENGTH ||
      password.length() >=
          WIFI_PASSWORD_LENGTH) {
    web_server.send(
        400,
        "text/plain",
        "Invalid Wi-Fi name/password length.");

    return;
  }

  memset(
      config_data.wifi_ssid,
      0,
      sizeof(config_data.wifi_ssid));

  memset(
      config_data.wifi_password,
      0,
      sizeof(config_data.wifi_password));

  ssid.toCharArray(
      config_data.wifi_ssid,
      sizeof(config_data.wifi_ssid));

  password.toCharArray(
      config_data.wifi_password,
      sizeof(config_data.wifi_password));

  if (!saveConfig()) {
    web_server.send(
        500,
        "text/plain",
        "Could not save Wi-Fi.");

    return;
  }

  Serial.printf(
      "Saved home Wi-Fi: %s\n",
      config_data.wifi_ssid);

  if (setup_mode)
    drawSetupScreen();

  redirectToRoot();
}

void handleRename() {
  const String id_text =
      web_server.arg("id");

  const String clean_name =
      sanitizePlantName(
          web_server.arg("name"));

  char* end = nullptr;

  const uint32_t sensor_id =
      static_cast<uint32_t>(
          strtoul(
              id_text.c_str(),
              &end,
              16));

  if (end == id_text.c_str() ||
      *end != '\0' ||
      clean_name.length() == 0) {
    web_server.send(
        400,
        "text/plain",
        "Invalid rename request.");

    return;
  }

  const int index =
      findPersistedPlant(sensor_id);

  if (index < 0) {
    web_server.send(
        404,
        "text/plain",
        "Sensor not found.");

    return;
  }

  memset(
      config_data.plants[index].name,
      0,
      sizeof(
          config_data.plants[index].name));

  clean_name.toCharArray(
      config_data.plants[index].name,
      sizeof(
          config_data.plants[index].name));

  saveConfig();

  if (have_displayed_packet &&
      displayed_sensor_id ==
          sensor_id) {
    drawPlantScreen(
        displayed_packet,
        plantNameFor(sensor_id));
  }

  redirectToRoot();
}

void handleProvision() {
  if (!setup_mode) {
    web_server.send(
        409,
        "text/plain",
        "Enter setup mode first.");

    return;
  }

  const String id_text =
      web_server.arg("id");

  char* end = nullptr;

  const uint32_t sensor_id =
      static_cast<uint32_t>(
          strtoul(
              id_text.c_str(),
              &end,
              16));

  if (end == id_text.c_str() ||
      *end != '\0') {
    web_server.send(
        400,
        "text/plain",
        "Invalid sensor ID.");

    return;
  }

  const bool ok =
      sendProvisionPacket(sensor_id);

  String response;

  if (ok) {
    response =
        "<!doctype html><html><body style='font-family:Arial;padding:30px'>"
        "<h2>Sensor provisioned.</h2>"
        "<p>The XIAO saved the home Wi-Fi and is rebooting.</p>"
        "<p>Finish setup on the T5 when you are ready. "
        "The next sensor wake will use the home router/mesh.</p>"
        "<p><a href='/'>Back</a></p>"
        "</body></html>";
  } else {
    response =
        "<!doctype html><html><body style='font-family:Arial;padding:30px'>"
        "<h2>No provisioning confirmation.</h2>"
        "<p>Make sure that XIAO is awake nearby in provisioning mode, then try again.</p>"
        "<p><a href='/'>Back</a></p>"
        "</body></html>";
  }

  web_server.send(
      ok ? 200 : 504,
      "text/html; charset=utf-8",
      response);
}

void handleConnectHome() {
  if (!wifiCredentialsSaved()) {
    web_server.send(
        400,
        "text/plain",
        "Save home Wi-Fi first.");

    return;
  }

  web_server.send(
      200,
      "text/html; charset=utf-8",
      "<!doctype html><html><body style='font-family:Arial;padding:30px'>"
      "<h2>Connecting to home Wi-Fi...</h2>"
      "<p>The PlantMonitor setup network will disappear.</p>"
      "</body></html>");

  deferred_exit_setup = true;
  deferred_exit_at_ms =
      millis() + 700;
}

void registerWebRoutes() {
  if (web_routes_registered)
    return;

  web_server.on(
      "/", HTTP_GET, handleRoot);

  web_server.on(
      "/wifi", HTTP_POST, handleSaveWifi);

  web_server.on(
      "/rename", HTTP_POST, handleRename);

  web_server.on(
      "/provision",
      HTTP_POST,
      handleProvision);

  web_server.on(
      "/home",
      HTTP_POST,
      handleConnectHome);

  web_server.on(
      "/generate_204",
      HTTP_GET,
      redirectToRoot);

  web_server.on(
      "/hotspot-detect.html",
      HTTP_GET,
      redirectToRoot);

  web_server.on(
      "/connecttest.txt",
      HTTP_GET,
      redirectToRoot);

  web_server.on(
      "/ncsi.txt",
      HTTP_GET,
      redirectToRoot);

  web_server.onNotFound(
      redirectToRoot);

  web_routes_registered = true;
}

// ============================================================================
// POWER
// ============================================================================

void initPmu() {
  pmu_ready = PPM.init(
      Wire,
      I2C_SDA,
      I2C_SCL,
      BQ25896_SLAVE_ADDRESS);

  if (!pmu_ready) {
    Serial.println(
        "Warning: BQ25896 PMU init failed.");

    return;
  }

  PPM.setSysPowerDownVoltage(3300);
  PPM.enableMeasure();

  Serial.println(
      "BQ25896 PMU ready.");
}

void shutdownNow() {
  Serial.println(
      "Side button held: PMU shutdown.");

  drawPowerOffScreen();
  delay(300);

  if (!pmu_ready) {
    Serial.println(
        "PMU unavailable; shutdown aborted.");

    restoreNormalDisplay();
    return;
  }

  dns_server.stop();
  web_server.stop();
  stopUdp();

  WiFi.mode(WIFI_OFF);

  Serial.flush();
  PPM.shutdown();

  delay(1200);

  // If it returns, external power is still present.
  Serial.println(
      "PMU shutdown returned; board still powered.");

  if (wifiCredentialsSaved()) {
    connectHomeWifi();
  }

  restoreNormalDisplay();
}

void serviceControlButton() {
  static bool was_pressed = false;
  static uint32_t pressed_since = 0;
  static bool long_action = false;

  const bool pressed =
      powerButtonPressed();

  if (pressed && !was_pressed) {
    pressed_since = millis();
    long_action = false;
  }

  if (pressed &&
      !long_action &&
      (millis() - pressed_since) >=
          POWER_HOLD_MS) {
    long_action = true;
    shutdownNow();
  }

  if (!pressed && was_pressed &&
      !long_action) {
    if (setup_mode) {
      stopSetupModeAndConnectHome();
    } else {
      startSetupMode();
    }
  }

  was_pressed = pressed;
}

// ============================================================================
// RECEIVED DATA -> DATABASE / DISPLAY
// ============================================================================

void processReceivedEvent(
    const ReceivedEvent& event) {
  const uint8_t* mac =
      event.via_udp
          ? nullptr
          : event.mac;

  const int persistent_index =
      ensurePersistedPlant(
          event.packet.sensor_id,
          mac);

  const int live_index =
      ensureLivePlant(
          event.packet.sensor_id,
          mac);

  if (persistent_index < 0 ||
      live_index < 0) {
    return;
  }

  LivePlant& live =
      live_plants[live_index];

  live.packet = event.packet;
  live.last_seen_ms = millis();
  live.source_ip = event.source_ip;
  live.via_udp = event.via_udp;

  if (event.via_udp) {
    config_data.plants[
        persistent_index].provisioned = 1;
  }

  if (setup_mode)
    return;

  if (shouldRefreshFor(event.packet)) {
    drawPlantScreen(
        event.packet,
        plantNameFor(
            event.packet.sensor_id));

    displayed_packet =
        event.packet;

    displayed_sensor_id =
        event.packet.sensor_id;

    have_displayed_packet = true;
  } else {
    displayed_packet =
        event.packet;

    displayed_sensor_id =
        event.packet.sensor_id;

    have_displayed_packet = true;
  }
}

// ============================================================================
// ARDUINO ENTRY POINTS
// ============================================================================

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(700);

  Serial.println();
  Serial.println(
      "LILYGO T5 Pro Plant Receiver — Phase 3B Wi-Fi / UDP");
  Serial.printf(
      "Protocol version: %u\n",
      plant::PROTOCOL_VERSION);

  loadConfig();

  receive_queue =
      xQueueCreate(
          10,
          sizeof(ReceivedEvent));

  if (!display.init_without_reset(false)) {
    Serial.println(
        "WARNING: e-paper init failed.");
  } else {
    display_ready = true;
  }

  initPmu();
  registerWebRoutes();

  if (wifiCredentialsSaved() &&
      connectHomeWifi()) {
    Serial.println(
        "Normal mode: home Wi-Fi / UDP.");

    drawWaitingScreen();
  } else {
    Serial.println(
        "Starting local setup/provisioning mode.");

    startSetupMode();
  }

  Serial.println(
      "Short press: setup mode. "
      "Long hold: power off.");
}

void loop() {
  serviceControlButton();

  if (setup_mode) {
    dns_server.processNextRequest();
  } else {
    serviceUdp();
  }

  web_server.handleClient();

  if (deferred_exit_setup &&
      static_cast<int32_t>(
          millis() -
          deferred_exit_at_ms) >= 0) {
    deferred_exit_setup = false;
    stopSetupModeAndConnectHome();
  }

  if (receive_queue != nullptr) {
    ReceivedEvent event{};

    while (xQueueReceive(
               receive_queue,
               &event,
               0) == pdTRUE) {
      processReceivedEvent(event);
    }
  }

  delay(10);
}
