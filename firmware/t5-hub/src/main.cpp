#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <atomic>

#include <M5GFX.h>
#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

#include "plant_protocol.h"
#include "t5_off_screen_xbm.h"

using lgfx::epd_mode_t;

namespace {

// ============================================================================
// USER / PRODUCT SETTINGS
// ============================================================================

// Plant names are NOT compiled into firmware anymore. They are stored in NVS
// and edited from the T5 setup page.
//
// The physical enclosure button labeled IO48 is the user/function button.
// On this board revision it is read through PCA9535 P1.2; ESP32 GPIO48 remains
// the e-paper CKV signal and is not used as a pushbutton.
// Short press toggles setup mode. Long hold draws the final OFF image and then
// requests true BQ25896 PMU shutdown. The physical PWR/QON button wakes the unit.
constexpr uint32_t FUNCTION_HOLD_MS = 2000;

// Protect the e-paper from unnecessary refreshes during development.
constexpr uint32_t MIN_NORMAL_REFRESH_MS = 60000;
constexpr uint32_t POWER_SOURCE_REFRESH_MIN_MS = 5000;
constexpr uint32_t POWER_SAMPLE_INTERVAL_MS = 10000;
constexpr uint32_t LAST_READING_CACHE_MIN_WRITE_MS = 60000;
constexpr uint8_t T5_BATTERY_REFRESH_DELTA = 3;

constexpr uint32_t HOME_WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t UDP_RETRY_INTERVAL_MS = 3000;
constexpr uint32_t WIFI_UDP_SETTLE_MS = 500;

// "Online" is intentionally based on very recent traffic, not just whether a
// sensor has ever been seen. ESP PLANTS sensors sleep by design, so the web
// page also shows the last-seen age instead of treating normal sleep as a fault.
constexpr uint32_t WIFI_SENSOR_RECENT_MS = 12000;
constexpr uint32_t ESPNOW_SENSOR_RECENT_MS = 8000;

// Wi-Fi and ESP-NOW share the same 2.4 GHz radio/channel. The T5 keeps
// ESP-NOW active alongside home Wi-Fi instead of switching radio modes.
constexpr uint32_t SETUP_AP_RESTART_DELAY_MS = 120;

// Setup hotspot. The password is generated once per T5 and stored in NVS.
constexpr uint16_t SETUP_HTTP_PORT = 80;
constexpr uint16_t SETUP_DNS_PORT = 53;
constexpr size_t MAX_PLANTS = 16;
constexpr size_t PLANT_NAME_LENGTH = 32;
constexpr size_t WIFI_SSID_LENGTH = 33;
constexpr size_t WIFI_PASSWORD_LENGTH = 65;
constexpr size_t SETUP_PASSWORD_LENGTH = 12;

// Display frontlight levels. OFF is the safe/default battery setting.
constexpr uint8_t FRONTLIGHT_PWM_CHANNEL = 7;
constexpr uint32_t FRONTLIGHT_PWM_HZ = 5000;
constexpr uint8_t FRONTLIGHT_PWM_BITS = 8;
constexpr char FRONTLIGHT_NAMESPACE[] = "plantui";
constexpr char FRONTLIGHT_KEY[] = "frontlight";

enum class FrontlightLevel : uint8_t {
  Off = 0,
  Low = 1,
  Medium = 2,
  High = 3
};

// ============================================================================
// T5 S3 PRO HARDWARE — verified against LILYGO H752-01 sources
// ============================================================================

constexpr int PANEL_WIDTH = 960;
constexpr int PANEL_HEIGHT = 540;
constexpr int EPD_BUS_SPEED_HZ = 20000000;
constexpr int VCOM_MV = 1560;

constexpr int I2C_SDA = 39;
constexpr int I2C_SCL = 40;

// H752-01 peripheral pins preserved from the hardware-verified Test 9 /
// T5S3-Reader baseline. GPS/LoRa stay disabled while ESP PLANTS is running.
constexpr uint8_t PIN_GPS_RXD = 44;
constexpr uint8_t PIN_GPS_TXD = 43;
constexpr uint8_t PIN_SPI_MISO = 21;
constexpr uint8_t PIN_SPI_MOSI = 13;
constexpr uint8_t PIN_SPI_SCLK = 14;
constexpr uint8_t PIN_SD_CS = 12;
constexpr uint8_t PIN_LORA_CS = 46;
constexpr uint8_t PIN_LORA_IRQ = 10;
constexpr uint8_t PIN_LORA_RST = 1;
constexpr uint8_t PIN_LORA_BUSY = 47;
constexpr uint8_t PIN_PCA9535_INT = 38;
constexpr uint8_t PIN_BOOT = 0;

constexpr uint8_t PCA9535_ADDR = 0x20;
constexpr uint8_t BQ25896_ADDR = 0x6B;

// BQ25896 register/field definitions used by the exact Reader/Test-9 power
// lifecycle. Keep this on the raw direct-I2C path used by Test 9.
constexpr uint8_t BQ_REG00 = 0x00;
constexpr uint8_t BQ_REG02 = 0x02;
constexpr uint8_t BQ_REG03 = 0x03;
constexpr uint8_t BQ_REG04 = 0x04;
constexpr uint8_t BQ_REG05 = 0x05;
constexpr uint8_t BQ_REG06 = 0x06;
constexpr uint8_t BQ_REG07 = 0x07;
constexpr uint8_t BQ_REG09 = 0x09;
constexpr uint8_t BQ_REG0B = 0x0B;
constexpr uint8_t BQ_REG11 = 0x11;
constexpr uint8_t BQ_REG14 = 0x14;

constexpr uint8_t BQ_REG00_EN_HIZ = 0x80;
constexpr uint8_t BQ_REG00_EN_ILIM = 0x40;
constexpr uint8_t BQ_REG00_IINLIM_MASK = 0x3F;
constexpr uint8_t BQ_REG02_CONV_RATE = 0x40;
constexpr uint8_t BQ_REG02_ICO_EN = 0x10;
constexpr uint8_t BQ_REG03_OTG_CONFIG = 0x20;
constexpr uint8_t BQ_REG03_CHG_CONFIG = 0x10;
constexpr uint8_t BQ_REG03_SYS_MIN_MASK = 0x0E;
constexpr uint8_t BQ_REG04_ICHG_MASK = 0x7F;
constexpr uint8_t BQ_REG05_IPRECHG_MASK = 0xF0;
constexpr uint8_t BQ_REG05_ITERM_MASK = 0x0F;
constexpr uint8_t BQ_REG06_VREG_MASK = 0xFC;
constexpr uint8_t BQ_REG07_WATCHDOG_MASK = 0x30;
constexpr uint8_t BQ_REG09_BATFET_DIS = 0x20;
constexpr uint8_t BQ_REG0B_PG_STAT = 0x04;
constexpr uint8_t BQ_REG11_VBUS_GD = 0x80;
constexpr uint8_t BQ_REG14_REG_RST = 0x80;

constexpr uint8_t BQ27220_ADDR = 0x55;
constexpr uint8_t TPS65185_ADDR = 0x68;

// BQ27220 standard command registers. These are read-only status accesses.
constexpr uint8_t BQ27220_REG_VOLTAGE = 0x08;
constexpr uint8_t BQ27220_REG_STATE_OF_CHARGE = 0x2C;

constexpr uint8_t PCA_INPUT_PORT0  = 0x00;
constexpr uint8_t PCA_OUTPUT_PORT0 = 0x02;
constexpr uint8_t PCA_CONFIG_PORT0 = 0x06;

// PCA9535 logical pin numbering: P0.0..P0.7 = 0..7, P1.0..P1.7 = 8..15.
constexpr uint8_t PCA_PIN_LORA_GPS_EN    = 0;
constexpr uint8_t PCA_PIN_EPD_OE         = 8;
constexpr uint8_t PCA_PIN_EPD_MODE       = 9;
constexpr uint8_t PCA_PIN_FUNCTION_BUTTON = 10; // physical IO48-labeled button; PCA P1.2, active-low
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
constexpr gpio_num_t PIN_DUMMY_BUS = GPIO_NUM_46;
constexpr gpio_num_t PIN_EPD_STH = GPIO_NUM_41;
constexpr gpio_num_t PIN_EPD_LE = GPIO_NUM_42;
constexpr gpio_num_t PIN_EPD_STV = GPIO_NUM_45;
constexpr gpio_num_t PIN_EPD_CKV = GPIO_NUM_48;

constexpr uint8_t PANEL_OFFSET_ROTATION = 3;
constexpr int POWER_GOOD_TIMEOUT_MS = 400;

uint8_t pca_output[2] = {0xFE, 0x00};

bool pmu_ready = false;

// Defined later, but used by the display/frontlight shutdown path.
void frontlightHardwareOff();

// ============================================================================
// LOW-LEVEL I2C / E-PAPER POWER CONTROL
// ============================================================================

bool i2cWriteBytes(uint8_t address, const uint8_t* data, size_t length) {
  Wire.beginTransmission(address);
  const size_t written = Wire.write(data, length);
  return written == length && Wire.endTransmission(true) == 0;
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

bool i2cReadU16LE(uint8_t address, uint8_t reg, uint16_t& value) {
  uint8_t bytes[2] = {0, 0};

  if (!i2cReadRegister(address, reg, bytes, sizeof(bytes)))
    return false;

  value =
      static_cast<uint16_t>(bytes[0]) |
      (static_cast<uint16_t>(bytes[1]) << 8);

  return true;
}

bool pca9535Init() {
  // Test 10 proved the display with the Reader-safe peripheral state. Keep
  // P0.0 (LORA_GPS_EN) low from the first output write; P1.2 remains input.
  pca_output[0] = 0xFE;
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

bool functionButtonPressed() {
  bool level = true;
  if (!pca9535GetLevel(PCA_PIN_FUNCTION_BUTTON, level)) {
    return false;
  }
  return !level; // active-low, same electrical behavior as LILYGO factory code
}


void forceReaderSafePeripheralState() {
  pinMode(PIN_LORA_CS, OUTPUT);
  digitalWrite(PIN_LORA_CS, HIGH);

  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, LOW);

  pinMode(PIN_LORA_IRQ, INPUT);
  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_GPS_RXD, INPUT);
  pinMode(PIN_GPS_TXD, INPUT);

  pca9535SetLevel(PCA_PIN_LORA_GPS_EN, false);
}

void prepareBoardLikeReader() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_PCA9535_INT, INPUT_PULLUP);

  // Reader prepareSdBus().
  pinMode(PIN_LORA_CS, OUTPUT);
  digitalWrite(PIN_LORA_CS, HIGH);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  SPI.begin(
      PIN_SPI_SCLK,
      PIN_SPI_MISO,
      PIN_SPI_MOSI,
      PIN_SD_CS);

  forceReaderSafePeripheralState();
}


bool bqWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BQ25896_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

bool bqReadReg(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(BQ25896_ADDR);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(
          BQ25896_ADDR,
          static_cast<uint8_t>(1)) != 1) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  value = static_cast<uint8_t>(Wire.read());
  return true;
}

bool bqUpdateBits(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;

  if (!bqReadReg(reg, current)) {
    return false;
  }

  const uint8_t updated =
      static_cast<uint8_t>(
          (current & static_cast<uint8_t>(~mask)) |
          (value & mask));

  if (updated == current) {
    return true;
  }

  return bqWriteReg(reg, updated);
}

bool resetBq25896ExactlyLikeReader() {
  if (!bqUpdateBits(
          BQ_REG14,
          BQ_REG14_REG_RST,
          BQ_REG14_REG_RST)) {
    return false;
  }

  // Test 9 copied Reader's driver: poll REG_RST 10 times, 2 ms apart.
  for (uint32_t attempt = 0;
       attempt < 10;
       ++attempt) {
    uint8_t reg14 = 0;

    if (!bqReadReg(BQ_REG14, reg14)) {
      return false;
    }

    if ((reg14 & BQ_REG14_REG_RST) == 0) {
      return true;
    }

    if (attempt + 1 < 10) {
      delay(2);
    }
  }

  return false;
}

bool configureBq25896ExactlyLikeReader() {
  if (!resetBq25896ExactlyLikeReader()) {
    return false;
  }

  if (!bqUpdateBits(
          BQ_REG00,
          BQ_REG00_EN_ILIM,
          BQ_REG00_EN_ILIM))
    return false;

  if (!bqUpdateBits(
          BQ_REG00,
          BQ_REG00_EN_HIZ,
          0))
    return false;

  if (!bqUpdateBits(
          BQ_REG02,
          BQ_REG02_ICO_EN,
          BQ_REG02_ICO_EN))
    return false;

  if (!bqUpdateBits(
          BQ_REG02,
          BQ_REG02_CONV_RATE,
          BQ_REG02_CONV_RATE))
    return false;

  if (!bqUpdateBits(
          BQ_REG07,
          BQ_REG07_WATCHDOG_MASK,
          0))
    return false;

  if (!bqUpdateBits(
          BQ_REG03,
          BQ_REG03_OTG_CONFIG,
          0))
    return false;

  // Reader explicitly enables the battery path before applying its profile.
  if (!bqUpdateBits(
          BQ_REG09,
          BQ_REG09_BATFET_DIS,
          0))
    return false;

  if (!bqUpdateBits(
          BQ_REG00,
          BQ_REG00_EN_HIZ,
          0))
    return false;

  // Reader BatteryProfile: 1000 mA input, 512 mA charge, 64 mA precharge,
  // 64 mA termination, 4208 mV charge voltage, 3300 mV SYS_MIN.
  if (!bqUpdateBits(
          BQ_REG00,
          BQ_REG00_IINLIM_MASK,
          18))
    return false;

  if (!bqUpdateBits(
          BQ_REG04,
          BQ_REG04_ICHG_MASK,
          8))
    return false;

  if (!bqUpdateBits(
          BQ_REG05,
          BQ_REG05_IPRECHG_MASK,
          0))
    return false;

  if (!bqUpdateBits(
          BQ_REG05,
          BQ_REG05_ITERM_MASK,
          0))
    return false;

  if (!bqUpdateBits(
          BQ_REG06,
          BQ_REG06_VREG_MASK,
          static_cast<uint8_t>(23U << 2)))
    return false;

  if (!bqUpdateBits(
          BQ_REG03,
          BQ_REG03_SYS_MIN_MASK,
          static_cast<uint8_t>(3U << 1)))
    return false;

  if (!bqUpdateBits(
          BQ_REG03,
          BQ_REG03_CHG_CONFIG,
          BQ_REG03_CHG_CONFIG))
    return false;

  return true;
}

bool batteryOnlyAccordingToReader() {
  uint8_t reg0b = 0;
  uint8_t reg11 = 0;

  if (!bqReadReg(BQ_REG0B, reg0b)) {
    return false;
  }

  if (!bqReadReg(BQ_REG11, reg11)) {
    return false;
  }

  const bool power_good =
      (reg0b & BQ_REG0B_PG_STAT) != 0;

  const bool vbus_good =
      (reg11 & BQ_REG11_VBUS_GD) != 0;

  return !power_good && !vbus_good;
}


[[noreturn]] void truePowerOffExactlyLikeDisplayTest10() {
  // GOLDEN REFERENCE:
  // ESP-PLANTS-T5-DISPLAY-TEST10.zip
  //
  // Keep the final cutoff sequence monolithic, exactly like the hardware-
  // verified Display Test 10: Reader-safe peripherals -> final battery-only
  // check -> OTG off -> charging off -> BATFET_DIS. Do not insert network,
  // display, delay, deinit, or wrapper-return work between these operations.
  forceReaderSafePeripheralState();

  if (!batteryOnlyAccordingToReader()) {
    // Same visible failure behavior as Display Test 10. Do not pretend the
    // unit shut down when VBUS is present or the status read failed.
    while (true) {
      digitalWrite(PIN_BACKLIGHT, HIGH);
      delay(80);
      digitalWrite(PIN_BACKLIGHT, LOW);
      delay(920);
    }
  }

  if (!bqUpdateBits(
          BQ_REG03,
          BQ_REG03_OTG_CONFIG,
          0)) {
    while (true) {
      digitalWrite(PIN_BACKLIGHT, HIGH);
      delay(80);
      digitalWrite(PIN_BACKLIGHT, LOW);
      delay(170);
    }
  }

  if (!bqUpdateBits(
          BQ_REG03,
          BQ_REG03_CHG_CONFIG,
          0)) {
    while (true) {
      digitalWrite(PIN_BACKLIGHT, HIGH);
      delay(80);
      digitalWrite(PIN_BACKLIGHT, LOW);
      delay(170);
    }
  }

  // Final write. On the known-good Test 9 / Display Test 10 baseline, SYS
  // collapses here and the board remains truly OFF: RST dead, PWR/QON wakes.
  if (!bqUpdateBits(
          BQ_REG09,
          BQ_REG09_BATFET_DIS,
          BQ_REG09_BATFET_DIS)) {
    while (true) {
      digitalWrite(PIN_BACKLIGHT, HIGH);
      delay(80);
      digitalWrite(PIN_BACKLIGHT, LOW);
      delay(170);
    }
  }

  // Normally unreachable. If BATFET_DIS does not remove SYS, stay inert.
  while (true) {
    delay(1000);
  }
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

    forceReaderSafePeripheralState();

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

constexpr uint32_t LAST_READING_MAGIC = 0x504C4153UL;  // "PLAS"
constexpr uint16_t LAST_READING_VERSION = 1;
constexpr char LAST_READING_NAMESPACE[] = "plantlast";
constexpr char LAST_READING_KEY[] = "reading";

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
  uint32_t last_udp_seen_ms;
  uint32_t last_espnow_seen_ms;
  IPAddress source_ip;
};

struct ReceivedEvent {
  uint8_t mac[6];
  plant::ReadingPacket packet;
  IPAddress source_ip;
  bool via_udp;
};

struct T5PowerState {
  bool gauge_valid;
  bool external_power;
  uint8_t percent;
  uint16_t battery_mv;
};

struct CachedReadingRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t structure_size;
  plant::ReadingPacket packet;
  uint32_t checksum;
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
bool esp_now_ready = false;

bool deferred_home_connect = false;
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
bool displayed_packet_is_cached = false;

T5PowerState t5_power{};
bool t5_power_sampled = false;
uint32_t last_power_sample_ms = 0;

CachedReadingRecord cached_reading{};
bool cached_reading_valid = false;
plant::ReadingPacket pending_cached_packet{};
bool cached_reading_dirty = false;
uint32_t last_cached_reading_write_ms = 0;

uint32_t wifi_connected_at_ms = 0;
uint32_t last_wifi_reconnect_attempt_ms = 0;
uint32_t last_udp_retry_ms = 0;

// Last application-level provisioning acknowledgement crosses the ESP-NOW
// callback/task boundary, so use real C++ atomics rather than volatile.
std::atomic<uint32_t> provision_ack_sensor_id{0};
std::atomic<bool> provision_ack_received{false};

// Identity ACKs can arrive asynchronously over ESP-NOW or later through UDP.
// They are captured now so replacement/assignment UI can require a persisted
// application ACK in the next lifecycle chunk.
std::atomic<uint32_t> identity_ack_sensor_id{0};
std::atomic<uint16_t> identity_ack_request_id{0};
std::atomic<uint8_t> identity_ack_slot{0};
std::atomic<bool> identity_ack_accepted{false};
std::atomic<bool> identity_ack_received{false};
uint16_t next_identity_request_id = 1;

// Automatic identity confirmation is useful once when a sensor becomes active,
// but normal 5-second service telemetry must not trigger another ASSIGN every
// cycle. Keep this cooldown longer than the normal two-minute service window.
// Explicit provisioning and Identify commands bypass this table.
constexpr uint32_t AUTO_IDENTITY_COOLDOWN_MS =
    3UL * 60UL * 1000UL;

struct AutoIdentityThrottle {
  uint32_t sensor_id = 0;
  uint32_t last_sent_ms = 0;
};

AutoIdentityThrottle
    auto_identity_throttle[MAX_PLANTS];

bool automaticIdentityDue(
    uint32_t sensor_id) {
  const uint32_t now = millis();

  for (size_t i = 0;
       i < MAX_PLANTS;
       ++i) {
    const AutoIdentityThrottle& entry =
        auto_identity_throttle[i];

    if (entry.sensor_id != sensor_id) {
      continue;
    }

    return entry.last_sent_ms == 0 ||
           (now - entry.last_sent_ms) >=
               AUTO_IDENTITY_COOLDOWN_MS;
  }

  return true;
}

void noteAutomaticIdentitySent(
    uint32_t sensor_id) {
  const uint32_t now = millis();
  int empty_index = -1;

  for (size_t i = 0;
       i < MAX_PLANTS;
       ++i) {
    AutoIdentityThrottle& entry =
        auto_identity_throttle[i];

    if (entry.sensor_id == sensor_id) {
      entry.last_sent_ms = now;
      return;
    }

    if (entry.sensor_id == 0 &&
        empty_index < 0) {
      empty_index =
          static_cast<int>(i);
    }
  }

  if (empty_index >= 0) {
    AutoIdentityThrottle& entry =
        auto_identity_throttle[
            empty_index];

    entry.sensor_id = sensor_id;
    entry.last_sent_ms = now;
  }
}

FrontlightLevel frontlight_level =
    FrontlightLevel::Off;
bool frontlight_pwm_ready = false;

// ============================================================================
// FRONTLIGHT SETTINGS
// ============================================================================

const char* frontlightLevelName(
    FrontlightLevel level) {
  switch (level) {
    case FrontlightLevel::Low:
      return "LOW";
    case FrontlightLevel::Medium:
      return "MEDIUM";
    case FrontlightLevel::High:
      return "HIGH";
    case FrontlightLevel::Off:
    default:
      return "OFF";
  }
}

uint8_t frontlightDuty(
    FrontlightLevel level) {
  switch (level) {
    case FrontlightLevel::Low:
      return 48;    // ~19%
    case FrontlightLevel::Medium:
      return 120;   // ~47%
    case FrontlightLevel::High:
      return 255;   // 100%
    case FrontlightLevel::Off:
    default:
      return 0;
  }
}

void frontlightHardwareOff() {
  if (frontlight_pwm_ready) {
    ledcWrite(
        FRONTLIGHT_PWM_CHANNEL,
        0);
    ledcDetachPin(
        static_cast<uint8_t>(
            PIN_BACKLIGHT));
    frontlight_pwm_ready = false;
  }

  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, LOW);
}

bool applyFrontlightSetting() {
  const uint8_t duty =
      frontlightDuty(
          frontlight_level);

  if (duty == 0) {
    frontlightHardwareOff();

    Serial.println(
        "T5 frontlight: OFF.");
    return true;
  }

  if (!frontlight_pwm_ready) {
    const double actual_hz =
        ledcSetup(
            FRONTLIGHT_PWM_CHANNEL,
            FRONTLIGHT_PWM_HZ,
            FRONTLIGHT_PWM_BITS);

    if (actual_hz <= 0.0) {
      Serial.println(
          "T5 frontlight PWM setup failed.");
      frontlightHardwareOff();
      return false;
    }

    ledcAttachPin(
        static_cast<uint8_t>(
            PIN_BACKLIGHT),
        FRONTLIGHT_PWM_CHANNEL);

    frontlight_pwm_ready = true;
  }

  ledcWrite(
      FRONTLIGHT_PWM_CHANNEL,
      duty);

  Serial.printf(
      "T5 frontlight: %s (duty %u/255).\n",
      frontlightLevelName(
          frontlight_level),
      duty);

  return true;
}

void loadFrontlightSetting() {
  frontlight_level =
      FrontlightLevel::Off;

  Preferences ui_preferences;

  // Open read/write so a brand-new T5 can create the independent plantui
  // namespace on first boot. Opening a nonexistent namespace read-only fails.
  if (!ui_preferences.begin(
          FRONTLIGHT_NAMESPACE,
          false)) {
    Serial.println(
        "Frontlight NVS unavailable; using OFF for this boot.");
    return;
  }

  constexpr uint8_t UNSET = 0xFF;
  uint8_t stored =
      ui_preferences.getUChar(
          FRONTLIGHT_KEY,
          UNSET);

  if (stored == UNSET) {
    stored =
        static_cast<uint8_t>(
            FrontlightLevel::Off);

    const size_t written =
        ui_preferences.putUChar(
            FRONTLIGHT_KEY,
            stored);

    if (written != 1) {
      Serial.println(
          "Frontlight default could not be persisted; using OFF.");
    } else {
      Serial.println(
          "Frontlight setting initialized to OFF.");
    }
  }

  ui_preferences.end();

  if (stored <=
      static_cast<uint8_t>(
          FrontlightLevel::High)) {
    frontlight_level =
        static_cast<FrontlightLevel>(
            stored);
  } else {
    frontlight_level =
        FrontlightLevel::Off;
  }

  Serial.printf(
      "Saved T5 frontlight: %s.\n",
      frontlightLevelName(
          frontlight_level));
}

bool saveFrontlightSetting(
    FrontlightLevel level) {
  Preferences ui_preferences;

  if (!ui_preferences.begin(
          FRONTLIGHT_NAMESPACE,
          false)) {
    Serial.println(
        "Frontlight NVS save failed.");
    return false;
  }

  const size_t written =
      ui_preferences.putUChar(
          FRONTLIGHT_KEY,
          static_cast<uint8_t>(
              level));

  ui_preferences.end();

  if (written != 1) {
    Serial.println(
        "Frontlight NVS write failed.");
    return false;
  }

  frontlight_level = level;
  return applyFrontlightSetting();
}

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

uint32_t cachedReadingChecksum(const CachedReadingRecord& source) {
  CachedReadingRecord copy = source;
  copy.checksum = 0;

  return plant::fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

bool loadCachedReading() {
  Preferences cache_preferences;

  if (!cache_preferences.begin(
          LAST_READING_NAMESPACE, true)) {
    Serial.println(
        "Last-reading cache unavailable.");
    return false;
  }

  const size_t stored_size =
      cache_preferences.getBytesLength(
          LAST_READING_KEY);

  bool valid = false;

  if (stored_size == sizeof(cached_reading)) {
    const size_t read =
        cache_preferences.getBytes(
            LAST_READING_KEY,
            &cached_reading,
            sizeof(cached_reading));

    valid =
        read == sizeof(cached_reading) &&
        cached_reading.magic ==
            LAST_READING_MAGIC &&
        cached_reading.version ==
            LAST_READING_VERSION &&
        cached_reading.structure_size ==
            sizeof(CachedReadingRecord) &&
        cached_reading.checksum ==
            cachedReadingChecksum(
                cached_reading) &&
        plant::validatePacket(
            cached_reading.packet);
  }

  cache_preferences.end();

  cached_reading_valid = valid;

  if (valid) {
    Serial.printf(
        "Restored last-known sensor 0x%08lX: %u%%.\n",
        static_cast<unsigned long>(
            cached_reading.packet.sensor_id),
        cached_reading.packet.moisture_percent);
  }

  return valid;
}

bool saveCachedReading(
    const plant::ReadingPacket& packet) {
  CachedReadingRecord record{};

  record.magic = LAST_READING_MAGIC;
  record.version = LAST_READING_VERSION;
  record.structure_size =
      sizeof(CachedReadingRecord);
  record.packet = packet;
  record.checksum =
      cachedReadingChecksum(record);

  Preferences cache_preferences;

  if (!cache_preferences.begin(
          LAST_READING_NAMESPACE, false)) {
    Serial.println(
        "Last-reading cache save failed: Preferences.begin().");
    return false;
  }

  const size_t written =
      cache_preferences.putBytes(
          LAST_READING_KEY,
          &record,
          sizeof(record));

  cache_preferences.end();

  if (written != sizeof(record)) {
    Serial.printf(
        "Last-reading cache save failed: wrote %u/%u bytes.\n",
        static_cast<unsigned>(written),
        static_cast<unsigned>(
            sizeof(record)));
    return false;
  }

  cached_reading = record;
  cached_reading_valid = true;
  cached_reading_dirty = false;
  last_cached_reading_write_ms = millis();

  Serial.printf(
      "Cached last-known reading for 0x%08lX.\n",
      static_cast<unsigned long>(
          packet.sensor_id));

  return true;
}

void queueCachedReading(
    const plant::ReadingPacket& packet) {
  pending_cached_packet = packet;
  cached_reading_dirty = true;

  // First-ever reading is worth persisting immediately. Later writes are
  // throttled so service-mode reports do not hammer flash.
  if (!cached_reading_valid) {
    saveCachedReading(
        pending_cached_packet);
  }
}

void serviceCachedReading() {
  if (!cached_reading_dirty)
    return;

  if ((millis() -
       last_cached_reading_write_ms) <
      LAST_READING_CACHE_MIN_WRITE_MS) {
    return;
  }

  saveCachedReading(
      pending_cached_packet);
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

uint8_t slotNumberForSensor(
    uint32_t sensor_id) {
  const int index =
      findPersistedPlant(sensor_id);

  if (index < 0)
    return 0;

  return static_cast<uint8_t>(
      index + 1);
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
    if (mac != nullptr &&
        memcmp(
            config_data.plants[index].mac,
            mac,
            6) != 0) {
      memcpy(
          config_data.plants[index].mac,
          mac,
          6);

      // A local ESP-NOW beacon gives us the MAC required by Locate. Persist
      // it only when learned/changed, not on every beacon.
      saveConfig();
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

bool plantMacKnown(const uint8_t* mac) {
  if (mac == nullptr)
    return false;

  for (uint8_t i = 0; i < 6; ++i) {
    if (mac[i] != 0)
      return true;
  }

  return false;
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

bool timestampRecent(
    uint32_t timestamp_ms,
    uint32_t recent_window_ms) {
  return timestamp_ms != 0 &&
         (millis() - timestamp_ms) <=
             recent_window_ms;
}

String lastSeenAgeLabel(
    uint32_t timestamp_ms) {
  if (timestamp_ms == 0) {
    return String("never");
  }

  const uint32_t age_seconds =
      (millis() - timestamp_ms) /
      1000UL;

  if (age_seconds < 60) {
    return String(age_seconds) +
           String(" sec ago");
  }

  const uint32_t age_minutes =
      age_seconds / 60UL;

  if (age_minutes < 60) {
    return String(age_minutes) +
           String(" min ago");
  }

  const uint32_t age_hours =
      age_minutes / 60UL;

  return String(age_hours) +
         String(" hr ago");
}

bool ipAddressKnown(
    const IPAddress& address) {
  return address[0] != 0 ||
         address[1] != 0 ||
         address[2] != 0 ||
         address[3] != 0;
}

bool wifiTransportRecent(
    const LivePlant& live) {
  return timestampRecent(
      live.last_udp_seen_ms,
      WIFI_SENSOR_RECENT_MS);
}

bool wifiLocateRouteRecent(
    const LivePlant& live) {
  // A fresh UDP report is enough to know the provisioned sensor is awake
  // right now. Candidate 3 required a second closely spaced service report,
  // which delayed/withheld the Locate button even though the green service
  // window was already active. The short freshness window prevents stale
  // scheduled wakes from leaving Locate enabled for long.
  return timestampRecent(
      live.last_udp_seen_ms,
      WIFI_SENSOR_RECENT_MS);
}

bool espNowTransportRecent(
    const LivePlant& live) {
  return timestampRecent(
      live.last_espnow_seen_ms,
      ESPNOW_SENSOR_RECENT_MS);
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
// T5 POWER STATUS
// ============================================================================

bool readT5PowerState(
    T5PowerState& state) {
  state = {};

  // Use the same BQ25896 status bits Test 9 trusts before ship-mode cut.
  if (pmu_ready) {
    uint8_t reg0b = 0;
    uint8_t reg11 = 0;
    if (bqReadReg(BQ_REG0B, reg0b) &&
        bqReadReg(BQ_REG11, reg11)) {
      state.external_power =
          (reg0b & BQ_REG0B_PG_STAT) != 0 ||
          (reg11 & BQ_REG11_VBUS_GD) != 0;
    }
  }

  uint16_t soc = 0;
  uint16_t battery_mv = 0;

  const bool soc_ok =
      i2cReadU16LE(
          BQ27220_ADDR,
          BQ27220_REG_STATE_OF_CHARGE,
          soc);

  const bool voltage_ok =
      i2cReadU16LE(
          BQ27220_ADDR,
          BQ27220_REG_VOLTAGE,
          battery_mv);

  state.gauge_valid =
      soc_ok &&
      voltage_ok &&
      soc <= 100 &&
      battery_mv >= 2500 &&
      battery_mv <= 5000;

  if (state.gauge_valid) {
    state.percent =
        static_cast<uint8_t>(soc);
    state.battery_mv = battery_mv;
  }

  return state.gauge_valid;
}

void refreshT5PowerState() {
  T5PowerState next{};
  readT5PowerState(next);
  t5_power = next;
  t5_power_sampled = true;
  last_power_sample_ms = millis();
}

String t5PowerLabel() {
  if (!t5_power_sampled) {
    return String("BAT --");
  }

  if (!t5_power.gauge_valid) {
    return t5_power.external_power
        ? String("USB  BAT --")
        : String("BAT --");
  }

  return
      String(
          t5_power.external_power
              ? "USB  "
              : "BAT  ") +
      String(t5_power.percent) +
      "%";
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

  display.setTextDatum(
      textdatum_t::middle_right);

  display.drawString(
      t5PowerLabel(),
      w - 28,
      31);
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
        "Short IO48 press: setup / provisioning",
        display.width() / 2,
        display.height() / 2 + 15);
  }

  display.drawString(
      "Hold IO48: power off",
      display.width() / 2,
      display.height() - 55);

  finishFrame();
}

void drawPlantScreen(
    const plant::ReadingPacket& packet,
    const String& plant_name,
    bool last_known = false) {
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

  if (last_known) {
    display.setFont(&fonts::Font2);
    display.setTextColor(
        gray(80), TFT_WHITE);
    display.drawString(
        "LAST KNOWN READING - waiting for fresh sensor report",
        40, 154);
    display.setTextColor(
        TFT_BLACK, TFT_WHITE);
  }

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

  {
    const uint8_t slot =
        slotNumberForSensor(
            packet.sensor_id);

    const String identity_label =
        slot > 0
            ? String("#") +
                  String(slot) +
                  String("  ID: ") +
                  sensorIdToHex(
                      packet.sensor_id)
            : String("ID: ") +
                  sensorIdToHex(
                      packet.sensor_id);

    display.drawString(
        identity_label,
        w / 2, h - 80);
  }

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
      "Short IO48 press again to return to home Wi-Fi",
      display.width() / 2,
      465);

  finishFrame();
}

void drawPowerOffScreen() {
  if (!display_ready) return;

  beginFrame();

  display.drawXBitmap(
      0,
      0,
      t5_off_screen_xbm,
      T5_OFF_SCREEN_WIDTH,
      T5_OFF_SCREEN_HEIGHT,
      TFT_BLACK,
      TFT_WHITE);

  // finishFrame() waits for the physical e-paper refresh to complete before
  // shutdown, so this final OFF image remains visible with power removed.
  finishFrame();

  // Frontlight PWM is an application feature, not part of the hardware-verified
  // Reader/Test-9 power sequence. Quiesce it here after the OFF image is fully
  // refreshed; shutdownNow() then enters the exact proven Test-10 cutoff path.
  frontlightHardwareOff();
}

bool shouldRefreshFor(
    const plant::ReadingPacket& packet) {
  if (!have_displayed_packet)
    return true;

  if (displayed_packet_is_cached)
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
            displayed_sensor_id),
        displayed_packet_is_cached);
    return;
  }

  drawWaitingScreen();
}

void serviceT5PowerDisplay() {
  if ((millis() - last_power_sample_ms) <
      POWER_SAMPLE_INTERVAL_MS) {
    return;
  }

  const T5PowerState previous =
      t5_power;
  const bool had_sample =
      t5_power_sampled;

  refreshT5PowerState();

  if (!had_sample)
    return;

  const bool source_changed =
      previous.external_power !=
      t5_power.external_power;

  const bool validity_changed =
      previous.gauge_valid !=
      t5_power.gauge_valid;

  int percent_delta = 0;

  if (previous.gauge_valid &&
      t5_power.gauge_valid) {
    percent_delta =
        abs(
            static_cast<int>(
                previous.percent) -
            static_cast<int>(
                t5_power.percent));
  }

  const uint32_t since_refresh =
      millis() -
      last_display_refresh_ms;

  if ((source_changed ||
       validity_changed) &&
      since_refresh >=
          POWER_SOURCE_REFRESH_MIN_MS) {
    restoreNormalDisplay();
    return;
  }

  if (percent_delta >=
          T5_BATTERY_REFRESH_DELTA &&
      since_refresh >=
          MIN_NORMAL_REFRESH_MS) {
    restoreNormalDisplay();
  }
}

// ============================================================================
// RADIO MODE CONTROL
// ============================================================================

uint8_t currentRadioChannel() {
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;

  if (esp_wifi_get_channel(
          &primary,
          &secondary) != ESP_OK) {
    return 0;
  }

  return primary;
}


bool ensureEspNowPeer(const uint8_t* mac) {
  if (!esp_now_ready) {
    Serial.println(
        "ESP-NOW peer unavailable: radio is not initialized.");
    return false;
  }

  if (esp_now_is_peer_exist(mac))
    return true;

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);

  // Channel 0 means the current Wi-Fi channel. This is what lets ESP-NOW
  // coexist with a connected STA without hard-coding channel 1 or channel 6.
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

uint16_t allocateIdentityRequestId() {
  if (next_identity_request_id == 0) {
    next_identity_request_id = 1;
  }

  return next_identity_request_id++;
}

plant::IdentityPacket makeIdentityPacket(
    uint32_t sensor_id,
    plant::IdentityCommand command,
    uint8_t slot) {
  plant::IdentityPacket packet{};

  packet.magic = plant::PACKET_MAGIC;
  packet.version = plant::PROTOCOL_VERSION;
  packet.packet_size = sizeof(packet);
  packet.sensor_id = sensor_id;
  packet.command = command;
  packet.slot = slot;
  packet.request_id =
      allocateIdentityRequestId();

  plant::finalizePacket(packet);
  return packet;
}

void recordIdentityAck(
    const plant::IdentityAckPacket& ack,
    const char* transport_name) {
  identity_ack_sensor_id.store(
      ack.sensor_id,
      std::memory_order_relaxed);
  identity_ack_request_id.store(
      ack.request_id,
      std::memory_order_relaxed);
  identity_ack_slot.store(
      ack.slot,
      std::memory_order_relaxed);
  identity_ack_accepted.store(
      ack.accepted == 1,
      std::memory_order_relaxed);
  identity_ack_received.store(
      true,
      std::memory_order_release);

  Serial.printf(
      "Identity ACK via %s from 0x%08lX: slot #%u, request %u, %s.\n",
      transport_name,
      static_cast<unsigned long>(
          ack.sensor_id),
      ack.slot,
      ack.request_id,
      ack.accepted == 1
          ? "ACCEPTED"
          : "REJECTED");
}

bool sendIdentityEspNow(
    const uint8_t* mac,
    uint32_t sensor_id,
    plant::IdentityCommand command,
    uint8_t slot) {
  if (!ensureEspNowPeer(mac))
    return false;

  const plant::IdentityPacket packet =
      makeIdentityPacket(
          sensor_id,
          command,
          slot);

  const esp_err_t result =
      esp_now_send(
          mac,
          reinterpret_cast<const uint8_t*>(
              &packet),
          sizeof(packet));

  Serial.printf(
      "Identity %s #%u -> 0x%08lX via ESP-NOW ch%u, request %u: %s\n",
      command == plant::IdentityCommand::Assign
          ? "ASSIGN"
          : "CLEAR",
      slot,
      static_cast<unsigned long>(
          sensor_id),
      currentRadioChannel(),
      packet.request_id,
      esp_err_to_name(result));

  return result == ESP_OK;
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

  if (!home_wifi_connected ||
      WiFi.status() != WL_CONNECTED) {
    Serial.println(
        "T5 UDP bind skipped: home Wi-Fi is not connected.");
    return false;
  }

  // Give lwIP a moment to release any previous PCB/socket before rebinding.
  // This is intentionally outside the proven PMU/shutdown path.
  delay(25);

  Serial.printf(
      "T5 UDP bind: IP=%s mask=%s gateway=%s channel=%d RSSI=%d port=%u.\n",
      WiFi.localIP().toString().c_str(),
      WiFi.subnetMask().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(),
      WiFi.channel(),
      WiFi.RSSI(),
      plant::T5_UDP_PORT);

  if (!udp.begin(plant::T5_UDP_PORT)) {
    Serial.println(
        "T5 UDP listener failed to bind.");
    return false;
  }

  udp_ready = true;

  Serial.printf(
      "T5 UDP READY on %s:%u.\n",
      WiFi.localIP().toString().c_str(),
      plant::T5_UDP_PORT);

  return true;
}

bool sendIdentityUdp(
    const IPAddress& remote_ip,
    uint16_t remote_port,
    uint32_t sensor_id,
    plant::IdentityCommand command,
    uint8_t slot) {
  if (!udp_ready)
    return false;

  const plant::IdentityPacket packet =
      makeIdentityPacket(
          sensor_id,
          command,
          slot);

  if (!udp.beginPacket(
          remote_ip,
          remote_port)) {
    Serial.println(
        "Identity UDP beginPacket failed.");
    return false;
  }

  const size_t written =
      udp.write(
          reinterpret_cast<const uint8_t*>(
              &packet),
          sizeof(packet));

  const int end_result =
      udp.endPacket();

  const bool ok =
      written == sizeof(packet) &&
      end_result == 1;

  Serial.printf(
      "Identity %s #%u -> 0x%08lX via UDP %s:%u, request %u: %s\n",
      command == plant::IdentityCommand::Assign
          ? "ASSIGN"
          : "CLEAR",
      slot,
      static_cast<unsigned long>(
          sensor_id),
      remote_ip.toString().c_str(),
      remote_port,
      packet.request_id,
      ok ? "SENT" : "FAILED");

  return ok;
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

  Serial.printf(
      "T5 UDP RX: %d bytes from %s:%u.\n",
      packet_size,
      source_ip.toString().c_str(),
      source_port);

  if (packet_size ==
      sizeof(plant::IdentityAckPacket)) {
    plant::IdentityAckPacket ack{};

    const int read =
        udp.read(
            reinterpret_cast<uint8_t*>(
                &ack),
            sizeof(ack));

    if (read == sizeof(ack) &&
        plant::validatePacket(ack)) {
      recordIdentityAck(
          ack,
          "UDP");
    } else {
      Serial.println(
          "Rejected invalid UDP identity ACK.");
    }

    return;
  }

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
      "Sequence: %lu\n",
      static_cast<unsigned long>(
          packet.sequence));

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

  const int persistent_index =
      ensurePersistedPlant(
          packet.sensor_id,
          nullptr);
  if (persistent_index >= 0 &&
      automaticIdentityDue(
          packet.sensor_id)) {
    if (sendIdentityUdp(
            source_ip,
            source_port,
            packet.sensor_id,
            plant::IdentityCommand::Assign,
            static_cast<uint8_t>(
                persistent_index + 1))) {
      noteAutomaticIdentitySent(
          packet.sensor_id);
    }
  }

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
      sizeof(plant::IdentityAckPacket)) {
    plant::IdentityAckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    if (!plant::validatePacket(ack))
      return;

    recordIdentityAck(
        ack,
        "ESP-NOW");
    return;
  }

  if (length ==
      sizeof(plant::ProvisionAckPacket)) {
    plant::ProvisionAckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    if (!plant::validatePacket(ack))
      return;

    provision_ack_sensor_id.store(
        ack.sensor_id,
        std::memory_order_relaxed);

    provision_ack_received.store(
        ack.accepted == 1,
        std::memory_order_release);

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
  // ESP-NOW and Wi-Fi intentionally coexist on the radio's CURRENT channel.
  // Never call esp_wifi_set_channel() here while STA is associated.
  esp_now_ready = false;

  const esp_err_t deinit_result =
      esp_now_deinit();

  if (deinit_result != ESP_OK &&
      deinit_result != ESP_ERR_ESPNOW_NOT_INIT) {
    Serial.printf(
        "ESP-NOW pre-init deinit warning: %s\n",
        esp_err_to_name(deinit_result));
  }

  const esp_err_t init_result =
      esp_now_init();

  if (init_result != ESP_OK) {
    Serial.printf(
        "ESP-NOW init failed: %s\n",
        esp_err_to_name(init_result));
    return false;
  }

  const esp_err_t recv_result =
      esp_now_register_recv_cb(
          onEspNowReceive);

  if (recv_result != ESP_OK) {
    Serial.printf(
        "ESP-NOW receive callback failed: %s\n",
        esp_err_to_name(recv_result));
    esp_now_deinit();
    return false;
  }

  const esp_err_t send_result =
      esp_now_register_send_cb(
          onEspNowSent);

  if (send_result != ESP_OK) {
    Serial.printf(
        "ESP-NOW send callback failed: %s\n",
        esp_err_to_name(send_result));
    esp_now_deinit();
    return false;
  }

  esp_now_ready = true;

  Serial.printf(
      "ESP-NOW ready on current Wi-Fi channel %u (%s).\n",
      currentRadioChannel(),
      home_wifi_connected
          ? "home Wi-Fi coexistence"
          : "setup/discovery");

  return true;
}

bool sendProvisionPacket(
    uint32_t sensor_id) {
  if (!wifiCredentialsSaved()) {
    Serial.println(
        "Provisioning requires saved home Wi-Fi first.");
    return false;
  }

  if (!esp_now_ready) {
    Serial.println(
        "Provisioning unavailable: ESP-NOW is not ready.");
    return false;
  }

  const int index =
      findPersistedPlant(sensor_id);

  const int live_index =
      findLivePlant(sensor_id);

  if (index < 0 || live_index < 0) {
    Serial.println(
        "Provision target not found.");
    return false;
  }

  PersistedPlant& record =
      config_data.plants[index];

  const LivePlant& live =
      live_plants[live_index];

  if (!plantMacKnown(record.mac) ||
      !espNowTransportRecent(live)) {
    Serial.println(
        "Provision target is not currently reachable over ESP-NOW.");
    return false;
  }

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

  provision_ack_received.store(
      false,
      std::memory_order_relaxed);
  provision_ack_sensor_id.store(
      0,
      std::memory_order_relaxed);

  Serial.printf(
      "Provisioning sensor 0x%08lX as authoritative slot #%u on coexistence channel %u...\n",
      static_cast<unsigned long>(
          sensor_id),
      static_cast<unsigned>(
          index + 1),
      currentRadioChannel());

  // Give the XIAO its T5-authoritative slot before Wi-Fi provisioning. The
  // identity message is additive protocol-v3 traffic and does not alter the
  // proven ProvisionPacket layout.
  sendIdentityEspNow(
      record.mac,
      sensor_id,
      plant::IdentityCommand::Assign,
      static_cast<uint8_t>(
          index + 1));

  delay(25);

  // The unprovisioned XIAO locks onto this channel after hearing the automatic
  // beacon ACK. Keep a forgiving send window in case the web click lands near
  // a channel-hop boundary.
  for (uint8_t attempt = 1;
       attempt <= 12;
       ++attempt) {
    const esp_err_t result =
        esp_now_send(
            record.mac,
            reinterpret_cast<const uint8_t*>(&packet),
            sizeof(packet));

    Serial.printf(
        "Provision send %u/12: %s\n",
        attempt,
        esp_err_to_name(result));

    const uint32_t wait_start =
        millis();

    while ((millis() - wait_start) <
           450) {
      if (provision_ack_received.load(
              std::memory_order_acquire) &&
          provision_ack_sensor_id.load(
              std::memory_order_relaxed) ==
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
      "No application provisioning ACK received.");

  return false;
}

bool sendLocatePacket(
    uint32_t sensor_id) {
  const int persistent_index =
      findPersistedPlant(sensor_id);

  if (persistent_index < 0) {
    Serial.println(
        "Locate target not found.");
    return false;
  }

  const int live_index =
      findLivePlant(sensor_id);

  if (live_index < 0) {
    Serial.println(
        "Locate unavailable: sensor has not been seen since this T5 boot.");
    return false;
  }

  const PersistedPlant& record =
      config_data.plants[persistent_index];

  const LivePlant& live =
      live_plants[live_index];

  const uint8_t authoritative_slot =
      static_cast<uint8_t>(
          persistent_index + 1);

  plant::LocatePacket packet{};

  packet.magic = plant::PACKET_MAGIC;
  packet.version = plant::PROTOCOL_VERSION;
  packet.packet_size = sizeof(packet);
  packet.sensor_id = sensor_id;
  packet.flash_ms = 8000;

  plant::finalizePacket(packet);

  // ESP-NOW is the preferred locate path whenever a fresh beacon proves the
  // sensor is listening on the same current Wi-Fi channel. This works for both
  // brand-new and already-provisioned sensors.
  if (esp_now_ready &&
      plantMacKnown(record.mac) &&
      espNowTransportRecent(live)) {
    if (!ensureEspNowPeer(record.mac))
      return false;

    // Confirm the slot immediately before Locate so the physical sensor only
    // uses a numbered pattern after current T5 authority has been received.
    sendIdentityEspNow(
        record.mac,
        sensor_id,
        plant::IdentityCommand::Assign,
        authoritative_slot);
    delay(20);

    const esp_err_t result =
        esp_now_send(
            record.mac,
            reinterpret_cast<const uint8_t*>(
                &packet),
            sizeof(packet));

    Serial.printf(
        "Locate sensor 0x%08lX via ESP-NOW ch%u (%s): %s\n",
        static_cast<unsigned long>(
            sensor_id),
        currentRadioChannel(),
        macToString(record.mac).c_str(),
        esp_err_to_name(result));

    return result == ESP_OK;
  }

  // UDP remains a useful fallback while a provisioned sensor is awake even if
  // an ESP-NOW service beacon was missed.
  if (!home_wifi_connected ||
      !udp_ready ||
      !wifiLocateRouteRecent(live) ||
      !ipAddressKnown(live.source_ip)) {
    Serial.println(
        "Locate unavailable: no recent ESP-NOW or Wi-Fi route.");
    return false;
  }

  // Same ordering as ESP-NOW: slot confirmation first, Locate second.
  sendIdentityUdp(
      live.source_ip,
      plant::SENSOR_UDP_PORT,
      sensor_id,
      plant::IdentityCommand::Assign,
      authoritative_slot);
  delay(20);

  if (!udp.beginPacket(
          live.source_ip,
          plant::SENSOR_UDP_PORT)) {
    Serial.println(
        "Locate UDP beginPacket failed.");
    return false;
  }

  const size_t written =
      udp.write(
          reinterpret_cast<const uint8_t*>(
              &packet),
          sizeof(packet));

  const int end_result =
      udp.endPacket();

  const bool ok =
      written == sizeof(packet) &&
      end_result == 1;

  Serial.printf(
      "Locate sensor 0x%08lX via Wi-Fi %s:%u: %s\n",
      static_cast<unsigned long>(
          sensor_id),
      live.source_ip.toString().c_str(),
      plant::SENSOR_UDP_PORT,
      ok ? "SENT" : "FAILED");

  return ok;
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

  // When the local PlantMonitor AP is active, keep it alive and add STA rather
  // than tearing it down. The AP and ESP-NOW will follow the STA/home channel.
  if (!setup_mode) {
    dns_server.stop();
    WiFi.softAPdisconnect(false);
  }

  if (!WiFi.mode(
          setup_mode
              ? WIFI_AP_STA
              : WIFI_STA)) {
    Serial.println(
        "Could not enable home Wi-Fi STA.");
    return false;
  }

  WiFi.setAutoReconnect(true);

  // The T5 is the always-listening UDP hub. Do not let station power-save
  // defer broadcast reception while sensors are waiting for a short ACK.
  WiFi.setSleep(false);

  Serial.printf(
      "Connecting T5 to \"%s\"%s...\n",
      config_data.wifi_ssid,
      setup_mode
          ? " while keeping the setup AP active"
          : "");

  WiFi.begin(
      config_data.wifi_ssid,
      config_data.wifi_password);

  const uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start) >=
        HOME_WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(
          "T5 home Wi-Fi connection timed out.");

      WiFi.disconnect(false, false);
      home_wifi_connected = false;
      return false;
    }

    delay(100);
  }

  home_wifi_connected = true;
  wifi_connected_at_ms = millis();

  Serial.println(
      "T5 home Wi-Fi connected.");

  Serial.print("T5 IP: ");
  Serial.println(WiFi.localIP());

  Serial.printf(
      "Home channel: %d\n",
      WiFi.channel());

  Serial.printf(
      "Home network: mask=%s gateway=%s RSSI=%d sleep=OFF.\n",
      WiFi.subnetMask().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(),
      WiFi.RSSI());

  web_server.begin();

  // Battery-powered resets can settle differently from USB-powered resets.
  // Wait briefly after WL_CONNECTED before binding the UDP listener.
  delay(WIFI_UDP_SETTLE_MS);

  last_udp_retry_ms = millis();

  if (!startUdp()) {
    Serial.println(
        "Home Wi-Fi is up; UDP listener will retry automatically.");
  }

  if (!startProvisioningRadio()) {
    Serial.println(
        "WARNING: home Wi-Fi is up but ESP-NOW coexistence init failed.");
  }

  return true;
}

void serviceHomeNetwork() {
  if (!wifiCredentialsSaved()) {
    return;
  }

  const bool connected =
      WiFi.status() == WL_CONNECTED;

  if (!connected) {
    if (home_wifi_connected ||
        udp_ready) {
      Serial.println(
          "Home Wi-Fi lost; stopping UDP until Wi-Fi recovers.");
      stopUdp();
      home_wifi_connected = false;
      esp_now_ready = false;

      if (setup_mode) {
        // The local AP is still alive; keep ESP-NOW discovery available on
        // that current channel while STA recovery is attempted.
        startProvisioningRadio();
      }
    }

    if ((millis() -
         last_wifi_reconnect_attempt_ms) <
        HOME_WIFI_RECONNECT_INTERVAL_MS) {
      return;
    }

    last_wifi_reconnect_attempt_ms =
        millis();

    Serial.println(
        "Retrying T5 home Wi-Fi...");
    WiFi.reconnect();
    return;
  }

  if (!home_wifi_connected) {
    home_wifi_connected = true;
    wifi_connected_at_ms = millis();

    Serial.print(
        "T5 home Wi-Fi recovered, IP: ");
    Serial.println(WiFi.localIP());

    Serial.printf(
        "Recovered on channel %d.\n",
        WiFi.channel());

    web_server.begin();

    // Association/channel changes can invalidate ESP-NOW state/peers.
    if (!startProvisioningRadio()) {
      Serial.println(
          "WARNING: ESP-NOW coexistence restart failed after Wi-Fi recovery.");
    }
  }

  if (udp_ready)
    return;

  if ((millis() -
       wifi_connected_at_ms) <
      WIFI_UDP_SETTLE_MS) {
    return;
  }

  if ((millis() -
       last_udp_retry_ms) <
      UDP_RETRY_INTERVAL_MS) {
    return;
  }

  last_udp_retry_ms = millis();

  if (!startUdp()) {
    Serial.println(
        "UDP listener retry failed; will try again.");
  }
}

bool startSetupMode() {
  if (setup_mode)
    return true;

  // If credentials exist, keep/establish the home STA first. ESP-NOW and the
  // local setup AP will then share that same channel.
  if (wifiCredentialsSaved() &&
      WiFi.status() != WL_CONNECTED) {
    if (!connectHomeWifi()) {
      Serial.println(
          "Home Wi-Fi unavailable; setup AP will still start for recovery.");
    }
  }

  const bool sta_connected =
      WiFi.status() == WL_CONNECTED;

  if (!WiFi.mode(WIFI_AP_STA)) {
    Serial.println(
        "Could not enter AP+STA mode.");
    return false;
  }

  delay(SETUP_AP_RESTART_DELAY_MS);

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

  const uint8_t setup_channel =
      sta_connected
          ? static_cast<uint8_t>(
                WiFi.channel())
          : plant::PROVISION_ESPNOW_CHANNEL;

  if (!WiFi.softAP(
          setup_ssid.c_str(),
          config_data.setup_password,
          setup_channel,
          0,
          4)) {
    Serial.println(
        "Setup SoftAP failed.");
    return false;
  }

  setup_mode = true;

  // Starting/changing AP mode can invalidate ESP-NOW. Rebuild it now on the
  // radio's actual current channel without disconnecting the home STA.
  if (!startProvisioningRadio()) {
    Serial.println(
        "WARNING: setup AP started but ESP-NOW init failed.");
  }

  if (WiFi.status() == WL_CONNECTED) {
    home_wifi_connected = true;

    // AP/STA mode changes can disturb lwIP bindings on some Arduino builds;
    // rebind the known-good UDP listener explicitly.
    startUdp();
  }

  dns_server.start(
      SETUP_DNS_PORT,
      "*",
      WiFi.softAPIP());

  web_server.begin();

  Serial.println();
  Serial.println(
      "=== T5 SETUP / PROVISIONING (COEXISTENCE) ===");

  Serial.printf(
      "Setup SSID: %s\n",
      setup_ssid.c_str());

  Serial.printf(
      "Password: %s\n",
      config_data.setup_password);

  Serial.printf(
      "Setup AP: http://%s/ channel %u\n",
      WiFi.softAPIP().toString().c_str(),
      currentRadioChannel());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(
        "Home STA remains connected: http://%s/ channel %d; UDP=%s; ESP-NOW=%s.\n",
        WiFi.localIP().toString().c_str(),
        WiFi.channel(),
        udp_ready ? "READY" : "RETRYING",
        esp_now_ready ? "READY" : "ERROR");
  }

  drawSetupScreen();
  return true;
}

void stopSetupModeAndConnectHome() {
  if (setup_mode) {
    dns_server.stop();
    WiFi.softAPdisconnect(false);
    setup_mode = false;

    // Collapse AP+STA back to STA. Rebuild ESP-NOW afterward because Wi-Fi mode
    // changes can clear its internal state.
    WiFi.mode(WIFI_STA);
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (!connectHomeWifi()) {
      Serial.println(
          "Could not return to home Wi-Fi; re-entering setup mode.");

      startSetupMode();
      return;
    }
  } else {
    home_wifi_connected = true;
    web_server.begin();

    if (!udp_ready)
      startUdp();

    if (!startProvisioningRadio()) {
      Serial.println(
          "WARNING: ESP-NOW restart failed after closing setup AP.");
    }
  }

  restoreNormalDisplay();
}

// ============================================================================
// WEB UI
// ============================================================================

String buildWebPage() {
  String html;
  html.reserve(21000);

  html += F(
      "<!doctype html><html><head>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Plant Monitor</title>"
      "<style>"
      "body{font-family:Arial,sans-serif;background:#f2f2f2;color:#171717;margin:0}"
      ".wrap{max-width:850px;margin:auto;padding:18px}"
      ".card{background:#fff;border-radius:14px;padding:18px;margin:14px 0;box-shadow:0 2px 8px #0002}"
      ".sub{background:#fafafa;border:1px solid #ddd}"
      "h1{margin:4px 0}h2{margin-top:0}"
      "label{display:block;font-weight:bold;margin:12px 0 5px}"
      "input{box-sizing:border-box;width:100%;padding:11px;border:1px solid #aaa;border-radius:8px;font-size:16px}"
      "button{padding:11px 15px;border:0;border-radius:8px;background:#222;color:#fff;font-size:15px;cursor:pointer}"
      ".muted{color:#666}.pill{background:#eee;border-radius:8px;padding:10px}"
      ".good{background:#e7f4e8}.warn{background:#fff1d2}"
      ".transport{display:inline-block;margin:4px 6px 4px 0;padding:8px 10px;border-radius:999px;background:#eee;font-size:14px}"
      ".transport.live{background:#dff3e2;font-weight:bold}"
      ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:8px}"
      "code{font-size:14px}"
      "</style></head><body><div class='wrap'>");

  html += F("<h1>Plant Monitor</h1><div class='muted'>");

  if (home_wifi_connected &&
      WiFi.status() == WL_CONNECTED) {
    html += F("Home Wi-Fi: ");
    html += htmlEscape(String(config_data.wifi_ssid));
    html += F(" &bull; T5 ");
    html += WiFi.localIP().toString();
    html += F(" &bull; channel ");
    html += String(WiFi.channel());
  } else {
    html += F("Home Wi-Fi: disconnected");
  }

  if (setup_mode) {
    html += F("<br>Setup AP: ");
    html += htmlEscape(setup_ssid);
    html += F(" &bull; ");
    html += WiFi.softAPIP().toString();
  }

  html += F("</div>");

  refreshT5PowerState();

  html += F("<div class='card'><h2>T5 Hub Status</h2><div class='grid'>");
  html += F("<div class='pill'><strong>Power</strong><br>");
  html += t5_power.external_power ? F("USB / external") : F("Battery");
  html += F("</div><div class='pill'><strong>T5 battery</strong><br>");
  html += t5_power.gauge_valid ? String(t5_power.percent) + "%" : String("Unavailable");
  html += F("</div><div class='pill'><strong>Battery voltage</strong><br>");
  html += t5_power.gauge_valid ? String(t5_power.battery_mv) + " mV" : String("--");
  html += F("</div><div class='pill'><strong>Home Wi-Fi</strong><br>");
  html += WiFi.status() == WL_CONNECTED ? F("CONNECTED") : F("DOWN");
  html += F("</div><div class='pill'><strong>UDP listener</strong><br>");
  html += udp_ready ? F("READY") : F("RETRYING / OFF");
  html += F("</div><div class='pill'><strong>ESP-NOW</strong><br>");
  html += esp_now_ready ? F("READY") : F("OFF / ERROR");
  if (esp_now_ready) {
    html += F(" &middot; ch ");
    html += String(currentRadioChannel());
  }
  html += F("</div></div><p class='muted'>Wi-Fi, UDP and ESP-NOW share the same current 2.4 GHz channel; the T5 no longer switches away from home Wi-Fi to locate or provision sensors.</p></div>");

  html += F(
      "<div class='card'><h2>Display Frontlight</h2><p class='muted'>Current: <strong>");
  html += frontlightLevelName(frontlight_level);
  html += F(
      "</strong>. OFF is the battery-friendly default.</p>"
      "<form method='post' action='/frontlight'><div class='grid'>"
      "<button name='level' value='off' type='submit'>OFF</button>"
      "<button name='level' value='low' type='submit'>LOW</button>"
      "<button name='level' value='medium' type='submit'>MED</button>"
      "<button name='level' value='high' type='submit'>HIGH</button>"
      "</div></form></div>");

  html += F("<div class='card'><h2>Home Wi-Fi</h2>");
  if (wifiCredentialsSaved()) {
    html += F("<div class='pill good'>Saved: <strong>");
    html += htmlEscape(String(config_data.wifi_ssid));
    html += F("</strong></div>");
  } else {
    html += F("<div class='pill warn'>No home Wi-Fi saved.</div>");
  }
  html += F(
      "<form method='post' action='/wifi'>"
      "<label>Wi-Fi name (SSID)</label><input name='ssid' maxlength='32' required value='");
  html += htmlEscape(String(config_data.wifi_ssid));
  html += F(
      "'><label>Wi-Fi password</label><input type='password' name='password' maxlength='64' placeholder='Enter password'>"
      "<p class='muted'>Password is stored on the T5 but never displayed back.</p>"
      "<button type='submit'>Save Home Wi-Fi</button></form>");

  if (setup_mode) {
    html += F(
        "<p class='muted'>The local setup AP can stay active at the same time as home Wi-Fi. Saving new credentials will make the T5 try them without intentionally shutting down the setup portal.</p>"
        "<form method='post' action='/home' style='margin-top:12px'><button type='submit'>Close Local Setup AP</button></form>");
  } else {
    html += F(
        "<p class='muted'>Short-press the physical IO48-labeled button if you also want the local PlantMonitor setup AP. Home Wi-Fi/UDP stays active.</p>");
  }
  html += F("</div>");

  html += F(
      "<div class='card'><h2>Plant Sensors</h2>"
      "<p class='muted'>Green means the T5 saw that transport recently. Gray normally means the battery sensor is sleeping. During the XIAO green 2-minute service window, a provisioned sensor can show both Wi-Fi/UDP and ESP-NOW ONLINE at the same time.</p>"
      "<form method='get' action='/' style='margin-bottom:12px'><button type='submit'>Refresh Sensor Status</button></form>");

  bool any = false;

  for (size_t i = 0; i < MAX_PLANTS; ++i) {
    if (!config_data.plants[i].used)
      continue;

    any = true;
    const PersistedPlant& record = config_data.plants[i];
    const int live_index = findLivePlant(record.sensor_id);

    html += F("<div class='card sub'><h2>#");
    html += String(i + 1);
    html += F(" &mdash; ");
    html += htmlEscape(String(record.name));
    html += F("</h2><div class='muted'><strong>T5 slot #");
    html += String(i + 1);
    html += F("</strong><br><code>0x");
    html += sensorIdToHex(record.sensor_id);
    html += F("</code><br><code>");
    html += macToString(record.mac);
    html += F("</code></div><div style='margin:10px 0'>");
    html += record.provisioned
        ? F("<span class='pill good'>Wi-Fi provisioned</span>")
        : F("<span class='pill warn'>Wi-Fi not confirmed</span>");
    html += F("</div>");

    bool wifi_recent = false;
    bool espnow_recent = false;

    if (live_index >= 0) {
      const LivePlant& live = live_plants[live_index];
      wifi_recent = wifiTransportRecent(live);
      espnow_recent = espNowTransportRecent(live);

      html += F("<div style='margin:10px 0'>");
      html += wifi_recent
          ? F("<span class='transport live'>&#9679; Wi-Fi / UDP ONLINE &middot; ")
          : F("<span class='transport'>&#9675; Wi-Fi / UDP &middot; ");
      html += lastSeenAgeLabel(live.last_udp_seen_ms);
      if (ipAddressKnown(live.source_ip)) {
        html += F(" &middot; ");
        html += live.source_ip.toString();
      }
      html += F("</span>");

      html += espnow_recent
          ? F("<span class='transport live'>&#9679; ESP-NOW ONLINE &middot; ")
          : F("<span class='transport'>&#9675; ESP-NOW &middot; ");
      html += lastSeenAgeLabel(live.last_espnow_seen_ms);
      html += F("</span></div><div class='grid'>");

      html += F("<div class='pill'><strong>Moisture</strong><br>");
      html += String(live.packet.moisture_percent);
      html += F("% &mdash; ");
      html += plant::stateName(live.packet.moisture_state);
      html += F("</div><div class='pill'><strong>Battery</strong><br>");
      html += String(live.packet.battery_percent);
      html += F("%</div><div class='pill'><strong>Last sensor report</strong><br>");
      html += lastSeenAgeLabel(live.last_seen_ms);
      html += F("</div></div>");
    } else {
      html += F(
          "<div style='margin:10px 0'><span class='transport'>&#9675; Wi-Fi / UDP &middot; not seen this boot</span>"
          "<span class='transport'>&#9675; ESP-NOW &middot; not seen this boot</span></div>");
    }

    html += F("<form method='post' action='/rename'><input type='hidden' name='id' value='");
    html += sensorIdToHex(record.sensor_id);
    html += F("'><label>Plant name</label><input name='name' maxlength='31' required value='");
    html += htmlEscape(String(record.name));
    html += F("'><div style='margin-top:10px'><button type='submit'>Save Plant Name</button></div></form>");

    const bool locate_via_espnow =
        live_index >= 0 &&
        espnow_recent &&
        plantMacKnown(record.mac) &&
        esp_now_ready;

    const bool locate_via_wifi =
        live_index >= 0 &&
        wifiLocateRouteRecent(live_plants[live_index]) &&
        ipAddressKnown(live_plants[live_index].source_ip) &&
        udp_ready;

    if (locate_via_espnow || locate_via_wifi) {
      html += F("<form method='post' action='/locate' style='margin-top:12px'><input type='hidden' name='id' value='");
      html += sensorIdToHex(record.sensor_id);
      html += F("'><button type='submit'>Identify Sensor #");
      html += String(i + 1);
      html += F("</button></form><p class='muted'>");
      html += locate_via_espnow
          ? F("ESP-NOW is active now and will be preferred for Locate.")
          : F("A recent Wi-Fi/UDP route is active and will be used as fallback.");
      html += F("</p>");
    } else {
      html += F("<p class='muted'>Wake the sensor. A brand-new sensor will channel-hop until ESP-NOW turns green; a provisioned sensor's green service window can make both indicators green.</p>");
    }

    if (wifiCredentialsSaved() &&
        live_index >= 0 &&
        espnow_recent &&
        plantMacKnown(record.mac)) {
      html += F("<form method='post' action='/provision' style='margin-top:12px'><input type='hidden' name='id' value='");
      html += sensorIdToHex(record.sensor_id);
      html += F("'><button type='submit'>Provision Sensor #");
      html += String(i + 1);
      html += F("</button></form><p class='muted'>Provisioning sends this T5 slot identity first, then the unchanged Wi-Fi ProvisionPacket on the same current-channel ESP-NOW connection.</p>");
    }

    html += F("</div>");
  }

  if (!any) {
    html += F("<p>No sensors registered yet.</p><p class='muted'>Wake an unprovisioned XIAO. It will hop 2.4 GHz channels until the T5 hears it, then lock to the T5 channel for Locate/provisioning.</p>");
  }

  html += F("</div><div class='muted' style='text-align:center;padding:16px'>Plant names live only on the T5.</div></div></body></html>");
  return html;
}

void redirectToRoot() {
  web_server.sendHeader(
      "Location", "/", true);

  web_server.send(
      303,
      "text/plain",
      "Redirecting");
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

  if (setup_mode) {
    drawSetupScreen();

    // Send the HTTP response first, then join home Wi-Fi while keeping AP+STA.
    // The setup AP may briefly move channels to follow the home STA.
    deferred_home_connect = true;
  }

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
        plantNameFor(sensor_id),
        displayed_packet_is_cached);
  }

  redirectToRoot();
}

void handleFrontlight() {
  const String level =
      web_server.arg("level");

  FrontlightLevel requested =
      FrontlightLevel::Off;

  if (level == "low") {
    requested = FrontlightLevel::Low;
  } else if (level == "medium") {
    requested = FrontlightLevel::Medium;
  } else if (level == "high") {
    requested = FrontlightLevel::High;
  } else if (level != "off") {
    web_server.send(
        400,
        "text/plain",
        "Invalid frontlight level.");
    return;
  }

  if (!saveFrontlightSetting(
          requested)) {
    web_server.send(
        500,
        "text/plain",
        "Could not save/apply frontlight level.");
    return;
  }

  redirectToRoot();
}

void handleLocate() {
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

  bool espnow_recent = false;
  bool wifi_recent = false;
  const int live_index =
      findLivePlant(sensor_id);

  if (live_index >= 0) {
    espnow_recent =
        espNowTransportRecent(
            live_plants[live_index]);
    wifi_recent =
        wifiLocateRouteRecent(
            live_plants[live_index]);
  }

  const bool ok =
      sendLocatePacket(
          sensor_id);

  String response =
      "<!doctype html><html><body style='font-family:Arial;padding:30px'>";

  if (ok) {
    response +=
        "<h2>Locate command sent.</h2><p>";

    response +=
        (espnow_recent && esp_now_ready)
            ? "Sent over ESP-NOW on the current Wi-Fi channel."
            : (wifi_recent
                   ? "Sent over the recent home Wi-Fi/UDP route."
                   : "Locate transmission queued.");

    response +=
        " Watch the selected sensor repeat its numbered red identity pattern for about 8 seconds. Long = 10; short = 1.</p>";
  } else {
    response +=
        "<h2>Locate command could not be sent.</h2>"
        "<p>Wake the sensor and refresh the page. ESP-NOW or Wi-Fi/UDP must be green/recent.</p>";
  }

  response +=
      "<p><a href='/'>Back</a></p></body></html>";

  web_server.send(
      ok ? 200 : 503,
      "text/html; charset=utf-8",
      response);
}

void handleProvision() {
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
        "<p>The T5 stayed on home Wi-Fi. UDP and ESP-NOW remain available on the same channel; there is no radio-mode switch to undo.</p>"
        "<p>Wake the sensor into its green service window to see Wi-Fi/UDP and ESP-NOW status together and use Locate.</p>"
        "<p><a href='/'>Back to sensors</a></p>"
        "</body></html>";
  } else {
    response =
        "<!doctype html><html><body style='font-family:Arial;padding:30px'>"
        "<h2>No provisioning confirmation.</h2>"
        "<p>Make sure the XIAO is awake and its ESP-NOW indicator is green, then try again.</p>"
        "<p><a href='/'>Back</a></p>"
        "</body></html>";
  }

  web_server.send(
      ok ? 200 : 504,
      "text/html; charset=utf-8",
      response);
}

void handleConnectHome() {
  if (setup_mode) {
    web_server.send(
        200,
        "text/html; charset=utf-8",
        "<!doctype html><html><body style='font-family:Arial;padding:30px'>"
        "<h2>Closing local setup AP...</h2>"
        "<p>Home Wi-Fi, UDP and ESP-NOW remain active.</p>"
        "</body></html>");

    deferred_exit_setup = true;
    deferred_exit_at_ms =
        millis() + 700;
    return;
  }

  redirectToRoot();
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
      "/frontlight",
      HTTP_POST,
      handleFrontlight);

  web_server.on(
      "/locate",
      HTTP_POST,
      handleLocate);

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

  // Browsers routinely request these even though ESP PLANTS has no icon.
  // Register them explicitly so normal portal use does not look like a route
  // failure in the serial log.
  web_server.on(
      "/favicon.ico",
      HTTP_GET,
      redirectToRoot);

  web_server.on(
      "/apple-touch-icon.png",
      HTTP_GET,
      redirectToRoot);

  web_server.on(
      "/apple-touch-icon-precomposed.png",
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
  pmu_ready =
      configureBq25896ExactlyLikeReader();

  Serial.println(
      pmu_ready
          ? "BQ25896 Reader/Test-9 profile ready."
          : "Warning: BQ25896 Reader/Test-9 profile init failed.");

  refreshT5PowerState();

  if (t5_power.gauge_valid) {
    Serial.printf(
        "BQ27220 battery: %u%%, %u mV, external power=%s.\n",
        t5_power.percent,
        t5_power.battery_mv,
        t5_power.external_power
            ? "YES"
            : "NO");
  } else {
    Serial.println(
        "Warning: BQ27220 battery gauge read failed.");
  }
}


void shutdownNow() {
  if (!pmu_ready) {
    Serial.println(
        "BQ25896 Reader/Test-9 profile unavailable; shutdown rejected.");
    return;
  }

  // Fast user-facing rejection before doing a full e-paper refresh. The
  // golden Test-10 routine below re-checks battery-only again immediately
  // before the actual BATFET sequence.
  if (!batteryOnlyAccordingToReader()) {
    Serial.println(
        "External/VBUS power present (or PMU status unavailable); disconnect USB before shutdown.");
    return;
  }

  // Finish application-owned work while the application is still alive.
  if (cached_reading_dirty) {
    saveCachedReading(
        pending_cached_packet);
  }

  dns_server.stop();
  web_server.stop();
  stopUdp();
  home_wifi_connected = false;
  WiFi.mode(WIFI_OFF);

  Serial.println(
      "IO48 held: drawing OFF screen, then exact Display-Test10 cutoff.");

  // This blocks through the physical refresh, powers the EPD down, and also
  // detaches/forces the application frontlight LOW.
  drawPowerOffScreen();

  Serial.flush();

  // IMPORTANT: from here onward mirror ESP-PLANTS-T5-DISPLAY-TEST10.
  // No Reader deinit helper, no 100 ms pause, and no bool-returning shutdown
  // wrapper between the OFF refresh and BATFET_DIS.
  forceReaderSafePeripheralState();
  truePowerOffExactlyLikeDisplayTest10();
}

void serviceControlButton() {
  static bool was_pressed = false;
  static uint32_t pressed_since = 0;
  static bool long_action = false;

  const bool pressed =
      functionButtonPressed();

  if (pressed && !was_pressed) {
    pressed_since = millis();
    long_action = false;
  }

  if (pressed &&
      !long_action &&
      (millis() - pressed_since) >=
          FUNCTION_HOLD_MS) {
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
  // Discovery/service ESP-NOW beacons share the same automatic identity
  // cooldown as UDP. Whichever transport confirms first suppresses the other
  // for the rest of the normal two-minute service window.
  if (!event.via_udp &&
      mac != nullptr &&
      automaticIdentityDue(
          event.packet.sensor_id)) {
    if (sendIdentityEspNow(
            mac,
            event.packet.sensor_id,
            plant::IdentityCommand::Assign,
            static_cast<uint8_t>(
                persistent_index + 1))) {
      noteAutomaticIdentitySent(
          event.packet.sensor_id);
    }
  }

  const uint32_t seen_ms =
      millis();

  live.last_seen_ms = seen_ms;

  if (event.via_udp) {
    live.last_udp_seen_ms = seen_ms;
    live.source_ip = event.source_ip;

    PersistedPlant& persisted =
        config_data.plants[persistent_index];

    // A valid home-Wi-Fi/UDP reading is proof that this sensor has working
    // network credentials. Persist the 0->1 transition once instead of merely
    // changing the in-memory status until the T5 reboots.
    if (!persisted.provisioned) {
      persisted.provisioned = 1;

      if (!saveConfig()) {
        persisted.provisioned = 0;
        Serial.println(
            "Could not persist Wi-Fi-provisioned sensor state; will retry on the next UDP reading.");
      }
    }
  } else {
    live.last_espnow_seen_ms = seen_ms;

    if (mac != nullptr) {
      memcpy(
          live.mac,
          mac,
          6);
    }
  }

  queueCachedReading(
      event.packet);

  if (setup_mode)
    return;

  if (shouldRefreshFor(event.packet)) {
    drawPlantScreen(
        event.packet,
        plantNameFor(
            event.packet.sensor_id),
        false);

    displayed_packet_is_cached = false;

    displayed_packet =
        event.packet;

    displayed_sensor_id =
        event.packet.sensor_id;

    have_displayed_packet = true;
  } else {
    displayed_packet_is_cached = false;

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
  delay(250);

  Serial.println();
  Serial.println(
      "LILYGO T5 Pro Plant Receiver — ESP PLANTS full integration");
  Serial.printf(
      "Protocol version: %u\n",
      plant::PROTOCOL_VERSION);

  // Hardware-verified Test 9 / Test 10 bring-up order.
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, LOW);

  if (!pca9535Init()) {
    Serial.println(
        "FATAL: PCA9535 init failed.");
    while (true) delay(1000);
  }

  prepareBoardLikeReader();
  initPmu();

  if (!pmu_ready) {
    Serial.println(
        "FATAL: Reader/Test-9 BQ25896 profile init failed.");
    while (true) delay(1000);
  }

  loadConfig();
  loadFrontlightSetting();
  loadCachedReading();

  if (cached_reading_valid) {
    displayed_packet =
        cached_reading.packet;
    displayed_sensor_id =
        cached_reading.packet.sensor_id;
    have_displayed_packet = true;
    displayed_packet_is_cached = true;
  }

  receive_queue =
      xQueueCreate(
          10,
          sizeof(ReceivedEvent));

  if (receive_queue == nullptr) {
    Serial.println(
        "FATAL: receive queue allocation failed.");
    while (true) delay(1000);
  }

  if (!display.init_without_reset(false)) {
    Serial.println(
        "WARNING: e-paper init failed.");
  } else {
    display_ready = true;
  }

  // M5GFX bus init deliberately forces GPIO11 LOW first. Apply the persisted
  // frontlight level only after display initialization is complete.
  applyFrontlightSetting();

  registerWebRoutes();

  if (wifiCredentialsSaved() &&
      connectHomeWifi()) {
    Serial.println(
        udp_ready
            ? "Normal mode: home Wi-Fi / UDP ready."
            : "Normal mode: home Wi-Fi connected; UDP retrying.");

    if (have_displayed_packet) {
      drawPlantScreen(
          displayed_packet,
          plantNameFor(
              displayed_sensor_id),
          displayed_packet_is_cached);
    } else {
      drawWaitingScreen();
    }
  } else {
    Serial.println(
        "Starting local setup/provisioning mode.");

    startSetupMode();
  }

  Serial.println(
      "Physical IO48 button: short press = setup mode; "
      "2-second hold = OFF screen + true PMU shutdown. "
      "While truly off, RST stays dead and PWR/QON wakes the unit.");
}

void loop() {
  serviceControlButton();

  if (setup_mode) {
    dns_server.processNextRequest();
  }

  serviceHomeNetwork();
  serviceUdp();

  serviceCachedReading();
  serviceT5PowerDisplay();

  web_server.handleClient();

  if (deferred_home_connect) {
    deferred_home_connect = false;

    if (!connectHomeWifi()) {
      Serial.println(
          "Saved home Wi-Fi could not be joined; local setup AP remains available.");
    } else if (setup_mode) {
      // STA association may move the shared AP/ESP-NOW channel. Rebuild the AP
      // on the actual home channel so the local portal and ESP-NOW stay aligned.
      dns_server.stop();
      WiFi.softAPdisconnect(false);
      delay(100);

      const uint8_t home_channel =
          static_cast<uint8_t>(
              WiFi.channel());

      WiFi.softAPConfig(
          setup_ip,
          setup_gateway,
          setup_subnet);

      if (WiFi.softAP(
              setup_ssid.c_str(),
              config_data.setup_password,
              home_channel,
              0,
              4)) {
        dns_server.start(
            SETUP_DNS_PORT,
            "*",
            WiFi.softAPIP());

        startProvisioningRadio();
        startUdp();

        Serial.printf(
            "Setup AP now coexists with home Wi-Fi on channel %u.\n",
            home_channel);
      }
    }
  }

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
