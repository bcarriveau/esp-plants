#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_mac.h>

#include <atomic>

#include "plant_protocol.h"

namespace {

// ============================================================================
// REAL SEEED SOIL SENSOR HARDWARE
// ============================================================================

constexpr uint8_t PIN_BATTERY_ADC = 0;   // GPIO0
constexpr uint8_t PIN_SOIL_ADC = 1;      // GPIO1
constexpr uint8_t PIN_BUTTON = 2;        // GPIO2, active LOW
constexpr uint8_t PIN_LED_YELLOW = 18;   // Factory board D10
constexpr uint8_t PIN_LED_GREEN = 19;    // Factory board D8
constexpr uint8_t PIN_LED_RED = 20;      // Factory board D9
constexpr uint8_t PIN_SENSOR_PWM = 21;   // GPIO21

constexpr uint8_t PIN_FACTORY_LOW = 3;
constexpr uint8_t PIN_FACTORY_HIGH = 14;

constexpr uint32_t SENSOR_PWM_HZ = 200000;
constexpr uint8_t SENSOR_PWM_BITS = 7;
constexpr float SENSOR_PWM_DUTY = 0.68f;

// The probe only needs excitation while soil is actually being measured.
// Keep the proven 800 ms startup settling time, then shut PWM back off before
// any battery/network work.
constexpr uint32_t SENSOR_EXCITATION_SETTLE_MS = 800;
constexpr uint32_t BATTERY_RECOVERY_MS = 100;

// Default calibration used only until this individual sensor is calibrated.
// These preserve the values already proven on this physical test sensor so a
// firmware update does not silently change its scale.
//
// Seeed's factory firmware also stores per-sensor dry/wet calibration values.
// Phase 3E restores that behavior with a triple-press calibration workflow.
constexpr float DEFAULT_CAL_DRY_MV = 2875.0f;
constexpr float DEFAULT_CAL_WET_MV = 2104.0f;

// Sanity guard: dry voltage must remain higher than wet voltage, and the two
// endpoints must be separated enough to produce a useful percentage span.
constexpr float MIN_CALIBRATION_SPAN_MV = 150.0f;

constexpr float BATTERY_EMPTY_MV = 1200.0f;
constexpr float BATTERY_FULL_MV = 1500.0f;

constexpr uint8_t SOIL_SAMPLES = 10;
constexpr uint8_t BATTERY_SAMPLES = 16;

// ============================================================================
// NETWORK / SLEEP SETTINGS
// ============================================================================

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t UDP_ACK_TIMEOUT_MS = 2500;
constexpr uint8_t UDP_MAX_ATTEMPTS = 3;

// Adaptive local-check policy.
//
// The probe can wake, measure, and go back to sleep WITHOUT turning Wi-Fi on.
// Wi-Fi is only started when there is something useful to report.
constexpr uint32_t DRY_CHECK_SECONDS = 15 * 60;
constexpr uint32_t ALMOST_DRY_CHECK_SECONDS = 15 * 60;
constexpr uint32_t NORMAL_CHECK_SECONDS = 30 * 60;
constexpr uint32_t WET_CHECK_SECONDS = 30 * 60;

// A watering event gets a fast first follow-up, then 10-minute checks until
// readings settle or the watch window is exhausted.
constexpr uint32_t WATERING_FIRST_FOLLOWUP_SECONDS = 5 * 60;
constexpr uint32_t WATERING_FOLLOWUP_SECONDS = 10 * 60;
constexpr uint8_t WATERING_RISE_PERCENT = 8;
constexpr uint8_t MEANINGFUL_CHANGE_PERCENT = 4;
constexpr uint8_t SETTLED_CHANGE_PERCENT = 2;
constexpr uint8_t SETTLED_CONFIRMATIONS = 2;
constexpr uint8_t MAX_WATERING_FOLLOWUPS = 6;

// Even if nothing changes, report periodically so the T5 knows the sensor is
// alive and gets a fresh battery reading.
constexpr uint32_t HEARTBEAT_SECONDS = 6 * 60 * 60;

// If a report is required but home Wi-Fi cannot be reached, retry much sooner
// than the normal plant-check interval.
constexpr uint32_t WIFI_FAILURE_SLEEP_SECONDS = 5 * 60;

// Unprovisioned sensors stay awake long enough to be provisioned from the T5.
// To protect an AA cell, only three automatic windows are allowed before the
// sensor enters button-only deep sleep.
constexpr uint32_t PROVISION_WINDOW_MS = 120000;
constexpr uint32_t PROVISION_BEACON_INTERVAL_MS = 2500;
constexpr uint8_t PROVISION_MAX_FAILED_WINDOWS = 3;
constexpr uint32_t PROVISION_FIRST_RETRY_SLEEP_SECONDS = 5 * 60;
constexpr uint32_t PROVISION_SECOND_RETRY_SLEEP_SECONDS = 15 * 60;

// Unprovisioned physical indication: alternate red/green while a provisioning
// window is actively searching for the T5.
constexpr uint32_t UNPROVISIONED_LED_PHASE_MS = 350;

// Sensor-number visual language. RED is reserved for the deliberate number
// pattern so it is visually distinct from the solid GREEN service-ready state.
// One long red pulse means ten; each short red pulse means one.
// Example: #13 = one long + three short.
constexpr uint32_t IDENTITY_SHORT_PULSE_MS = 300;
constexpr uint32_t IDENTITY_LONG_PULSE_MS = 1000;
constexpr uint32_t IDENTITY_ELEMENT_GAP_MS = 300;
constexpr uint32_t IDENTITY_TO_SERVICE_PAUSE_MS = 600;

// A brand-new sensor does not know the T5/home channel yet. It walks the
// 2.4 GHz channels until it hears the T5's ESP-NOW ACK, then locks there.
constexpr uint8_t PROVISION_CHANNEL_MIN = 1;
constexpr uint8_t PROVISION_CHANNEL_MAX = 13;
constexpr uint32_t PROVISION_CHANNEL_DWELL_MS = 320;
constexpr uint32_t PROVISION_CHANNEL_LOCK_TIMEOUT_MS = 6500;

// A GPIO2 button wake enters a two-minute service window.
// The green LED stays on for this entire manual-awake period.
constexpr uint32_t SERVICE_WINDOW_MS = 120000;

// While already awake in service mode, a deliberate 10-second hold erases
// saved home Wi-Fi. This replaces the old risky "hold during boot" erase.
constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;

// Retry home Wi-Fi periodically during service mode without going to sleep.
constexpr uint32_t SERVICE_WIFI_RETRY_MS = 10000;

// While the green service LED is on, automatically take/send a fresh reading.
// This lets pulling the probe out, wiping it off, watering, etc. show up
// without pressing the button again.
constexpr uint32_t SERVICE_SAMPLE_INTERVAL_MS = 5000;

// Factory-style calibration:
// triple short-press while already awake in green service mode.
// Red flashes for dry placement, then green flashes for wet placement.
constexpr uint8_t CALIBRATION_TRIGGER_CLICKS = 3;
constexpr uint32_t CALIBRATION_CLICK_WINDOW_MS = 1400;
constexpr uint32_t CALIBRATION_PLACEMENT_MS = 10000;
constexpr uint32_t CALIBRATION_STAGE_PAUSE_MS = 3000;
constexpr uint8_t CALIBRATION_SAMPLES = 10;
constexpr uint32_t CALIBRATION_SAMPLE_DELAY_MS = 200;

constexpr uint8_t BROADCAST_MAC[6] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================================
// PERSISTENT SENSOR CONFIG
// ============================================================================

constexpr uint32_t SENSOR_CONFIG_MAGIC = 0x53434647UL;  // "SCFG"
constexpr uint16_t SENSOR_CONFIG_VERSION = 1;
constexpr char CONFIG_NAMESPACE[] = "plantnet";
constexpr char CONFIG_KEY[] = "cfg";

struct SensorConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t structure_size;
  char wifi_ssid[33];
  char wifi_password[65];
  uint16_t t5_udp_port;
  uint16_t reserved;
  uint32_t checksum;
};

SensorConfig config_data{};
Preferences preferences;

// T5 slot identity is intentionally separate from Wi-Fi provisioning.
// The T5 remains authoritative; this is only a cached physical-display number
// for the XIAO. Slot 0 means unassigned; valid T5 slots are 1..16.
constexpr uint32_t IDENTITY_MAGIC = 0x49443131UL;  // "ID11"
constexpr uint16_t IDENTITY_VERSION = 1;
constexpr char IDENTITY_NAMESPACE[] = "plantid";
constexpr char IDENTITY_KEY[] = "id";
constexpr uint8_t MAX_IDENTITY_SLOT = 16;

struct IdentityData {
  uint32_t magic;
  uint16_t version;
  uint16_t structure_size;
  uint8_t slot;
  uint8_t reserved[3];
  uint32_t checksum;
};

IdentityData identity_data{};
uint8_t cached_identity_slot = 0;

// Calibration lives in its own NVS namespace/key so adding calibration does
// not invalidate or rewrite the already-proven Wi-Fi provisioning record.
constexpr uint32_t CALIBRATION_MAGIC = 0x43414C31UL;  // "CAL1"
constexpr uint16_t CALIBRATION_VERSION = 1;
constexpr char CALIBRATION_NAMESPACE[] = "plantcal";
constexpr char CALIBRATION_KEY[] = "cal";

struct CalibrationData {
  uint32_t magic;
  uint16_t version;
  uint16_t structure_size;
  float dry_mv;
  float wet_mv;
  uint32_t checksum;
};

CalibrationData calibration_data{};
float active_cal_dry_mv = DEFAULT_CAL_DRY_MV;
float active_cal_wet_mv = DEFAULT_CAL_WET_MV;

uint32_t sensor_id = 0;
RTC_DATA_ATTR uint32_t sequence_number = 1;

// Number of failed provisioning attempts since the last manual reset or
// successful provisioning. RTC memory preserves it across deep-sleep retries.
RTC_DATA_ATTR uint8_t provisioning_failed_windows = 0;

constexpr uint32_t ADAPTIVE_STATE_MAGIC = 0x41504431UL;  // "APD1"

struct AdaptiveRtcState {
  uint32_t magic;

  uint8_t have_observed;
  uint8_t last_observed_percent;
  uint8_t last_observed_state;
  uint8_t reserved0;

  uint8_t have_reported;
  uint8_t last_reported_percent;
  uint8_t last_reported_state;
  uint8_t reserved1;

  uint8_t watering_watch_active;
  uint8_t watering_followups_done;
  uint8_t watering_stable_count;
  uint8_t reserved2;

  uint32_t seconds_since_report;
  uint32_t planned_sleep_seconds;
};

RTC_DATA_ATTR AdaptiveRtcState adaptive{};

WiFiUDP udp;

// True only after the original wake press has been released and the sensor is
// actively in the two-minute manual service window. Blocking Wi-Fi/UDP helpers
// use this to yield immediately when GPIO2 is pressed so the 10-second erase
// hold cannot be starved by network timeouts.
bool service_mode_active = false;
bool provisioning_mode_active = false;
bool esp_now_ready = false;
bool sensor_excitation_active = false;
std::atomic<bool> provision_channel_locked{false};
std::atomic<uint8_t> discovered_t5_channel{0};
std::atomic<uint32_t> last_t5_espnow_ack_ms{0};

std::atomic<bool> provision_received{false};
std::atomic<uint32_t> locate_request_ms{0};
SensorConfig pending_config{};

bool identity_confirmed_this_wake = false;

struct PendingIdentityEvent {
  uint8_t source_mac[6];
  plant::IdentityPacket packet;
};

QueueHandle_t identity_queue = nullptr;

struct AdcReading {
  uint32_t raw;
  uint32_t mv;
};

struct Measurement {
  AdcReading soil;
  AdcReading battery;
  uint8_t moisture_percent;
  plant::MoistureState moisture_state;
  uint8_t battery_percent;
};

// ============================================================================
// HELPERS
// ============================================================================

void initStatusLeds() {
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
}

void allStatusLedsOff() {
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
}

void serviceLedOn() {
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void updateUnprovisionedIndicator(
    uint32_t now_ms) {
  const bool show_red =
      ((now_ms / UNPROVISIONED_LED_PHASE_MS) & 1U) == 0;

  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(
      PIN_LED_RED,
      show_red ? HIGH : LOW);
  digitalWrite(
      PIN_LED_GREEN,
      show_red ? LOW : HIGH);
}

void pulseIdentityRed(
    uint32_t on_ms) {
  allStatusLedsOff();
  digitalWrite(PIN_LED_RED, HIGH);
  delay(on_ms);
  digitalWrite(PIN_LED_RED, LOW);
}

void flashSensorNumberOnce(
    uint8_t slot) {
  if (slot == 0 ||
      slot > MAX_IDENTITY_SLOT) {
    return;
  }

  allStatusLedsOff();

  const uint8_t short_count =
      slot >= 10
          ? static_cast<uint8_t>(slot - 10)
          : slot;

  if (slot >= 10) {
    pulseIdentityRed(
        IDENTITY_LONG_PULSE_MS);

    if (short_count > 0) {
      delay(IDENTITY_ELEMENT_GAP_MS);
    }
  }

  for (uint8_t i = 0;
       i < short_count;
       ++i) {
    pulseIdentityRed(
        IDENTITY_SHORT_PULSE_MS);

    if (i + 1 < short_count) {
      delay(IDENTITY_ELEMENT_GAP_MS);
    }
  }

  allStatusLedsOff();
}

bool wokeByButton() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

void printWakeReason() {
  const esp_sleep_wakeup_cause_t cause =
      esp_sleep_get_wakeup_cause();

  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wake reason: TOP BUTTON (GPIO2)");
      break;

    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wake reason: scheduled timer");
      break;

    default:
      Serial.printf(
          "Wake reason: reset/power/programming (%d)\n",
          static_cast<int>(cause));
      break;
  }
}

void initializeAdaptiveStateIfNeeded() {
  if (adaptive.magic == ADAPTIVE_STATE_MAGIC) {
    return;
  }

  memset(&adaptive, 0, sizeof(adaptive));
  adaptive.magic = ADAPTIVE_STATE_MAGIC;

  Serial.println(
      "Adaptive history initialized (first reading will be reported).");
}

void accountForWakeTime() {
  initializeAdaptiveStateIfNeeded();

  if (esp_sleep_get_wakeup_cause() ==
          ESP_SLEEP_WAKEUP_TIMER &&
      adaptive.planned_sleep_seconds > 0) {
    const uint32_t remaining =
        UINT32_MAX - adaptive.seconds_since_report;

    if (adaptive.planned_sleep_seconds > remaining) {
      adaptive.seconds_since_report = UINT32_MAX;
    } else {
      adaptive.seconds_since_report +=
          adaptive.planned_sleep_seconds;
    }
  }

  // Clear after accounting so resets/button wakes do not accidentally count
  // a sleep interval that never actually completed.
  adaptive.planned_sleep_seconds = 0;
}

uint8_t absolutePercentDelta(
    uint8_t a,
    uint8_t b) {
  return a > b ? a - b : b - a;
}

uint32_t baseCheckSeconds(
    plant::MoistureState state) {
  switch (state) {
    case plant::MoistureState::Dry:
      return DRY_CHECK_SECONDS;

    case plant::MoistureState::AlmostDry:
      return ALMOST_DRY_CHECK_SECONDS;

    case plant::MoistureState::Wet:
      return WET_CHECK_SECONDS;

    case plant::MoistureState::Normal:
    default:
      return NORMAL_CHECK_SECONDS;
  }
}

bool armWateringWatchIfRise(
    const Measurement& measurement,
    const char* context) {
  if (!adaptive.have_observed) {
    return false;
  }

  const int rise =
      static_cast<int>(
          measurement.moisture_percent) -
      static_cast<int>(
          adaptive.last_observed_percent);

  if (rise < WATERING_RISE_PERCENT) {
    return false;
  }

  adaptive.watering_watch_active = 1;
  adaptive.watering_followups_done = 0;
  adaptive.watering_stable_count = 0;

  Serial.printf(
      "Watering event armed from %s: %u%% -> %u%% (+%d%%).\n",
      context,
      adaptive.last_observed_percent,
      measurement.moisture_percent,
      rise);

  return true;
}

void rememberObservation(
    const Measurement& measurement) {
  adaptive.have_observed = 1;
  adaptive.last_observed_percent =
      measurement.moisture_percent;
  adaptive.last_observed_state =
      static_cast<uint8_t>(
          measurement.moisture_state);
}

void rememberSuccessfulReport(
    const Measurement& measurement) {
  adaptive.have_reported = 1;
  adaptive.last_reported_percent =
      measurement.moisture_percent;
  adaptive.last_reported_state =
      static_cast<uint8_t>(
          measurement.moisture_state);
  adaptive.seconds_since_report = 0;
}

struct AdaptiveDecision {
  bool send;
  bool watering_detected;
  bool state_changed;
  bool meaningful_change;
  bool heartbeat_due;
  uint32_t next_check_seconds;
  const char* reason;
};

AdaptiveDecision evaluateScheduledMeasurement(
    const Measurement& measurement) {
  AdaptiveDecision decision{};
  decision.next_check_seconds =
      baseCheckSeconds(
          measurement.moisture_state);
  decision.reason = "stable/no-report";

  const bool had_observation =
      adaptive.have_observed != 0;

  const uint8_t previous_observed_percent =
      adaptive.last_observed_percent;

  const plant::MoistureState previous_observed_state =
      static_cast<plant::MoistureState>(
          adaptive.last_observed_state);

  uint8_t observed_delta = 0;
  int observed_signed_change = 0;

  if (had_observation) {
    observed_delta =
        absolutePercentDelta(
            measurement.moisture_percent,
            previous_observed_percent);

    observed_signed_change =
        static_cast<int>(
            measurement.moisture_percent) -
        static_cast<int>(
            previous_observed_percent);
  }

  decision.watering_detected =
      had_observation &&
      observed_signed_change >=
          WATERING_RISE_PERCENT;

  if (adaptive.have_reported) {
    decision.state_changed =
        static_cast<uint8_t>(
            measurement.moisture_state) !=
        adaptive.last_reported_state;

    decision.meaningful_change =
        absolutePercentDelta(
            measurement.moisture_percent,
            adaptive.last_reported_percent) >=
        MEANINGFUL_CHANGE_PERCENT;
  }

  decision.heartbeat_due =
      adaptive.have_reported &&
      adaptive.seconds_since_report >=
          HEARTBEAT_SECONDS;

  if (!adaptive.have_reported) {
    decision.send = true;
    decision.reason = "first baseline";
  }

  if (decision.state_changed) {
    decision.send = true;
    decision.reason = "moisture state changed";
  }

  if (decision.meaningful_change) {
    decision.send = true;
    decision.reason = "moisture changed >= 4%";
  }

  if (decision.heartbeat_due) {
    decision.send = true;
    decision.reason = "6-hour heartbeat";
  }

  if (decision.watering_detected) {
    adaptive.watering_watch_active = 1;
    adaptive.watering_followups_done = 0;
    adaptive.watering_stable_count = 0;

    decision.send = true;
    decision.next_check_seconds =
        WATERING_FIRST_FOLLOWUP_SECONDS;
    decision.reason = "watering rise detected";
  } else if (adaptive.watering_watch_active) {
    adaptive.watering_followups_done++;

    if (had_observation &&
        observed_delta <=
            SETTLED_CHANGE_PERCENT) {
      if (adaptive.watering_stable_count <
          UINT8_MAX) {
        adaptive.watering_stable_count++;
      }
    } else {
      adaptive.watering_stable_count = 0;
    }

    const bool settled =
        adaptive.watering_stable_count >=
            SETTLED_CONFIRMATIONS;

    const bool watch_limit_reached =
        adaptive.watering_followups_done >=
            MAX_WATERING_FOLLOWUPS;

    if (settled || watch_limit_reached) {
      adaptive.watering_watch_active = 0;
      adaptive.watering_followups_done = 0;
      adaptive.watering_stable_count = 0;

      decision.next_check_seconds =
          baseCheckSeconds(
              measurement.moisture_state);

      Serial.printf(
          "Post-watering watch ended: %s.\n",
          settled
              ? "reading settled"
              : "follow-up limit reached");
    } else {
      decision.next_check_seconds =
          WATERING_FOLLOWUP_SECONDS;

      Serial.printf(
          "Post-watering watch: follow-up %u/%u, "
          "observed delta=%u%%, stable count=%u/%u.\n",
          adaptive.watering_followups_done,
          MAX_WATERING_FOLLOWUPS,
          observed_delta,
          adaptive.watering_stable_count,
          SETTLED_CONFIRMATIONS);
    }
  }

  rememberObservation(measurement);
  return decision;
}

void printAdaptiveDecision(
    const AdaptiveDecision& decision,
    const Measurement& measurement) {
  Serial.println();
  Serial.println("=== ADAPTIVE SCHEDULER ===");

  Serial.printf(
      "Current: %u%% (%s)\n",
      measurement.moisture_percent,
      plant::stateName(
          measurement.moisture_state));

  if (adaptive.have_reported) {
    Serial.printf(
        "Last reported: %u%% (%s)\n",
        adaptive.last_reported_percent,
        plant::stateName(
            static_cast<plant::MoistureState>(
                adaptive.last_reported_state)));

    Serial.printf(
        "Heartbeat age: %lu sec\n",
        static_cast<unsigned long>(
            adaptive.seconds_since_report));
  } else {
    Serial.println(
        "Last reported: none");
  }

  Serial.printf(
      "Decision: %s (%s)\n",
      decision.send
          ? "SEND"
          : "SKIP WI-FI",
      decision.reason);

  Serial.printf(
      "Next local soil check: %lu sec (%lu min)\n",
      static_cast<unsigned long>(
          decision.next_check_seconds),
      static_cast<unsigned long>(
          decision.next_check_seconds / 60));
}

uint8_t clampPercent(float value) {
  if (value <= 0.0f) return 0;
  if (value >= 100.0f) return 100;
  return static_cast<uint8_t>(value + 0.5f);
}

uint32_t configChecksum(const SensorConfig& source) {
  SensorConfig copy = source;
  copy.checksum = 0;

  return plant::fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

void initializeEmptyConfig() {
  memset(&config_data, 0, sizeof(config_data));
  config_data.magic = SENSOR_CONFIG_MAGIC;
  config_data.version = SENSOR_CONFIG_VERSION;
  config_data.structure_size = sizeof(SensorConfig);
  config_data.t5_udp_port = plant::T5_UDP_PORT;
  config_data.checksum = configChecksum(config_data);
}

bool saveConfig(const SensorConfig& source) {
  SensorConfig copy = source;
  copy.magic = SENSOR_CONFIG_MAGIC;
  copy.version = SENSOR_CONFIG_VERSION;
  copy.structure_size = sizeof(SensorConfig);

  if (copy.t5_udp_port == 0) {
    copy.t5_udp_port = plant::T5_UDP_PORT;
  }

  copy.checksum = configChecksum(copy);

  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    Serial.println("Sensor NVS save failed: Preferences.begin().");
    return false;
  }

  const size_t written =
      preferences.putBytes(
          CONFIG_KEY,
          &copy,
          sizeof(copy));

  preferences.end();

  if (written != sizeof(copy)) {
    Serial.printf(
        "Sensor NVS save failed: wrote %u of %u bytes.\n",
        static_cast<unsigned>(written),
        static_cast<unsigned>(sizeof(copy)));
    return false;
  }

  config_data = copy;
  Serial.println("Home Wi-Fi saved in sensor NVS.");
  return true;
}

bool loadConfig() {
  initializeEmptyConfig();

  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    Serial.println("Sensor NVS unavailable.");
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
        config_data.magic == SENSOR_CONFIG_MAGIC &&
        config_data.version == SENSOR_CONFIG_VERSION &&
        config_data.structure_size == sizeof(SensorConfig) &&
        config_data.checksum == configChecksum(config_data) &&
        config_data.wifi_ssid[0] != '\0';
  }

  preferences.end();

  if (!valid) {
    initializeEmptyConfig();
  }

  return valid;
}

void clearConfig() {
  if (preferences.begin(CONFIG_NAMESPACE, false)) {
    preferences.remove(CONFIG_KEY);
    preferences.end();
  }

  initializeEmptyConfig();
  Serial.println("Saved home Wi-Fi erased.");
}


uint32_t identityChecksum(
    const IdentityData& source) {
  IdentityData copy = source;
  copy.checksum = 0;

  return plant::fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

void initializeEmptyIdentity() {
  memset(
      &identity_data,
      0,
      sizeof(identity_data));

  identity_data.magic = IDENTITY_MAGIC;
  identity_data.version = IDENTITY_VERSION;
  identity_data.structure_size =
      sizeof(IdentityData);
  identity_data.slot = 0;
  identity_data.checksum =
      identityChecksum(identity_data);

  cached_identity_slot = 0;
}

bool loadIdentity() {
  initializeEmptyIdentity();

  if (!preferences.begin(
          IDENTITY_NAMESPACE,
          true)) {
    Serial.println(
        "Cached T5 slot: unassigned.");
    return false;
  }

  const size_t stored_size =
      preferences.getBytesLength(
          IDENTITY_KEY);

  IdentityData stored{};
  bool valid = false;

  if (stored_size == sizeof(stored)) {
    const size_t read =
        preferences.getBytes(
            IDENTITY_KEY,
            &stored,
            sizeof(stored));

    valid =
        read == sizeof(stored) &&
        stored.magic == IDENTITY_MAGIC &&
        stored.version == IDENTITY_VERSION &&
        stored.structure_size ==
            sizeof(IdentityData) &&
        stored.slot >= 1 &&
        stored.slot <= MAX_IDENTITY_SLOT &&
        stored.checksum ==
            identityChecksum(stored);
  }

  preferences.end();

  if (!valid) {
    initializeEmptyIdentity();
    Serial.println(
        "Cached T5 slot: unassigned.");
    return false;
  }

  identity_data = stored;
  cached_identity_slot = stored.slot;

  Serial.printf(
      "Cached T5 slot: #%u (awaiting T5 authority/confirmation).\n",
      cached_identity_slot);

  return true;
}

bool saveIdentitySlot(
    uint8_t slot) {
  if (slot < 1 ||
      slot > MAX_IDENTITY_SLOT) {
    return false;
  }

  IdentityData updated{};
  updated.magic = IDENTITY_MAGIC;
  updated.version = IDENTITY_VERSION;
  updated.structure_size =
      sizeof(IdentityData);
  updated.slot = slot;
  updated.checksum =
      identityChecksum(updated);

  if (!preferences.begin(
          IDENTITY_NAMESPACE,
          false)) {
    Serial.println(
        "Identity NVS save failed: Preferences.begin().");
    return false;
  }

  const size_t written =
      preferences.putBytes(
          IDENTITY_KEY,
          &updated,
          sizeof(updated));

  preferences.end();

  if (written != sizeof(updated)) {
    Serial.println(
        "Identity NVS save failed.");
    return false;
  }

  identity_data = updated;
  cached_identity_slot = slot;

  Serial.printf(
      "Cached T5 slot saved: #%u.\n",
      cached_identity_slot);

  return true;
}

bool clearIdentitySlot() {
  if (cached_identity_slot == 0) {
    initializeEmptyIdentity();
    return true;
  }

  if (!preferences.begin(
          IDENTITY_NAMESPACE,
          false)) {
    Serial.println(
        "Identity NVS clear failed: Preferences.begin().");
    return false;
  }

  const bool removed =
      preferences.remove(
          IDENTITY_KEY);

  preferences.end();

  if (!removed) {
    Serial.println(
        "Identity NVS clear failed.");
    return false;
  }

  initializeEmptyIdentity();

  Serial.println(
      "Cached T5 slot cleared.");
  return true;
}

bool applyIdentityPacket(
    const plant::IdentityPacket& packet) {
  if (!plant::validatePacket(packet) ||
      packet.sensor_id != sensor_id) {
    return false;
  }

  if (packet.command ==
      plant::IdentityCommand::Assign) {
    if (packet.slot < 1 ||
        packet.slot > MAX_IDENTITY_SLOT) {
      return false;
    }

    if (cached_identity_slot !=
        packet.slot) {
      if (!saveIdentitySlot(
              packet.slot)) {
        return false;
      }
    }

    identity_confirmed_this_wake = true;

    Serial.printf(
        "T5 authority confirmed: Sensor #%u (request %u).\n",
        packet.slot,
        packet.request_id);
    return true;
  }

  if (packet.command ==
      plant::IdentityCommand::Clear) {
    if (cached_identity_slot != 0 &&
        !clearIdentitySlot()) {
      return false;
    }

    identity_confirmed_this_wake = true;

    Serial.printf(
        "T5 authority cleared local slot assignment (request %u).\n",
        packet.request_id);
    return true;
  }

  return false;
}


uint32_t calibrationChecksum(
    const CalibrationData& source) {
  CalibrationData copy = source;
  copy.checksum = 0;

  return plant::fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

void useDefaultCalibration() {
  memset(
      &calibration_data,
      0,
      sizeof(calibration_data));

  calibration_data.magic =
      CALIBRATION_MAGIC;
  calibration_data.version =
      CALIBRATION_VERSION;
  calibration_data.structure_size =
      sizeof(CalibrationData);
  calibration_data.dry_mv =
      DEFAULT_CAL_DRY_MV;
  calibration_data.wet_mv =
      DEFAULT_CAL_WET_MV;
  calibration_data.checksum =
      calibrationChecksum(
          calibration_data);

  active_cal_dry_mv =
      calibration_data.dry_mv;
  active_cal_wet_mv =
      calibration_data.wet_mv;
}

bool calibrationValuesValid(
    float dry_mv,
    float wet_mv) {
  if (!isfinite(dry_mv) ||
      !isfinite(wet_mv)) {
    return false;
  }

  if (dry_mv <= wet_mv) {
    return false;
  }

  if ((dry_mv - wet_mv) <
      MIN_CALIBRATION_SPAN_MV) {
    return false;
  }

  // Broad ADC sanity limits.  These intentionally do not force Seeed's
  // generic 2.75 V / 1.20 V defaults on an individually calibrated probe.
  if (dry_mv < 200.0f ||
      dry_mv > 3300.0f ||
      wet_mv < 100.0f ||
      wet_mv > 3200.0f) {
    return false;
  }

  return true;
}

bool loadCalibration() {
  useDefaultCalibration();

  if (!preferences.begin(
          CALIBRATION_NAMESPACE,
          true)) {
    Serial.println(
        "Calibration NVS unavailable; "
        "using built-in defaults.");
    return false;
  }

  const size_t stored_size =
      preferences.getBytesLength(
          CALIBRATION_KEY);

  CalibrationData stored{};
  bool valid = false;

  if (stored_size == sizeof(stored)) {
    const size_t read =
        preferences.getBytes(
            CALIBRATION_KEY,
            &stored,
            sizeof(stored));

    valid =
        read == sizeof(stored) &&
        stored.magic ==
            CALIBRATION_MAGIC &&
        stored.version ==
            CALIBRATION_VERSION &&
        stored.structure_size ==
            sizeof(CalibrationData) &&
        stored.checksum ==
            calibrationChecksum(stored) &&
        calibrationValuesValid(
            stored.dry_mv,
            stored.wet_mv);
  }

  preferences.end();

  if (valid) {
    calibration_data = stored;
    active_cal_dry_mv =
        stored.dry_mv;
    active_cal_wet_mv =
        stored.wet_mv;

    Serial.printf(
        "Loaded saved soil calibration: "
        "dry=%.0f mV, wet=%.0f mV, span=%.0f mV.\n",
        active_cal_dry_mv,
        active_cal_wet_mv,
        active_cal_dry_mv -
            active_cal_wet_mv);

    return true;
  }

  Serial.printf(
      "No valid saved soil calibration; "
      "using defaults dry=%.0f mV, wet=%.0f mV.\n",
      active_cal_dry_mv,
      active_cal_wet_mv);

  return false;
}

bool saveCalibration(
    float dry_mv,
    float wet_mv) {
  if (!calibrationValuesValid(
          dry_mv,
          wet_mv)) {
    Serial.printf(
        "Calibration rejected: dry=%.0f mV, "
        "wet=%.0f mV, span=%.0f mV.\n",
        dry_mv,
        wet_mv,
        dry_mv - wet_mv);
    return false;
  }

  CalibrationData updated{};
  updated.magic =
      CALIBRATION_MAGIC;
  updated.version =
      CALIBRATION_VERSION;
  updated.structure_size =
      sizeof(CalibrationData);
  updated.dry_mv =
      dry_mv;
  updated.wet_mv =
      wet_mv;
  updated.checksum =
      calibrationChecksum(updated);

  if (!preferences.begin(
          CALIBRATION_NAMESPACE,
          false)) {
    Serial.println(
        "Calibration NVS save failed: "
        "Preferences.begin().");
    return false;
  }

  const size_t written =
      preferences.putBytes(
          CALIBRATION_KEY,
          &updated,
          sizeof(updated));

  preferences.end();

  if (written != sizeof(updated)) {
    Serial.printf(
        "Calibration NVS save failed: "
        "wrote %u of %u bytes.\n",
        static_cast<unsigned>(
            written),
        static_cast<unsigned>(
            sizeof(updated)));
    return false;
  }

  calibration_data = updated;
  active_cal_dry_mv =
      updated.dry_mv;
  active_cal_wet_mv =
      updated.wet_mv;

  Serial.printf(
      "Calibration SAVED: dry=%.0f mV, "
      "wet=%.0f mV, span=%.0f mV.\n",
      active_cal_dry_mv,
      active_cal_wet_mv,
      active_cal_dry_mv -
          active_cal_wet_mv);

  return true;
}

uint32_t createSensorId() {
  const uint64_t chip_id = ESP.getEfuseMac();
  return static_cast<uint32_t>(
      chip_id ^ (chip_id >> 32));
}

void printMac(const uint8_t* mac) {
  Serial.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5]);
}

AdcReading readAveraged(
    uint8_t pin,
    uint8_t samples,
    uint32_t delay_ms) {
  uint64_t raw_sum = 0;
  uint64_t mv_sum = 0;

  (void)analogRead(pin);
  (void)analogReadMilliVolts(pin);
  delay(10);

  for (uint8_t i = 0; i < samples; ++i) {
    raw_sum += analogRead(pin);
    mv_sum += analogReadMilliVolts(pin);

    if (delay_ms > 0) {
      delay(delay_ms);
    }
  }

  AdcReading result{};
  result.raw =
      static_cast<uint32_t>(raw_sum / samples);
  result.mv =
      static_cast<uint32_t>(mv_sum / samples);

  return result;
}

uint8_t moisturePercentFromMillivolts(float mv) {
  const float span =
      active_cal_dry_mv -
      active_cal_wet_mv;

  if (span <= 0.0f) return 0;

  const float pct =
      ((active_cal_dry_mv - mv) /
       span) * 100.0f;

  return clampPercent(pct);
}

plant::MoistureState stateFromPercent(uint8_t pct) {
  if (pct <= 20) return plant::MoistureState::Dry;
  if (pct <= 40) return plant::MoistureState::AlmostDry;
  if (pct <= 80) return plant::MoistureState::Normal;

  return plant::MoistureState::Wet;
}

uint8_t batteryPercentFromMillivolts(float mv) {
  const float pct =
      ((mv - BATTERY_EMPTY_MV) /
       (BATTERY_FULL_MV - BATTERY_EMPTY_MV)) *
      100.0f;

  return clampPercent(pct);
}

bool startSensorExcitation() {
  if (sensor_excitation_active) {
    return true;
  }

  if (!ledcAttach(
          PIN_SENSOR_PWM,
          SENSOR_PWM_HZ,
          SENSOR_PWM_BITS)) {
    return false;
  }

  const uint32_t max_duty =
      (1UL << SENSOR_PWM_BITS) - 1UL;

  const uint32_t duty =
      static_cast<uint32_t>(
          (max_duty * SENSOR_PWM_DUTY) + 0.5f);

  if (!ledcWrite(PIN_SENSOR_PWM, duty)) {
    ledcDetach(PIN_SENSOR_PWM);
    pinMode(PIN_SENSOR_PWM, OUTPUT);
    digitalWrite(PIN_SENSOR_PWM, LOW);
    return false;
  }

  sensor_excitation_active = true;
  return true;
}

void stopSensorExcitation() {
  if (sensor_excitation_active) {
    ledcWrite(PIN_SENSOR_PWM, 0);
    ledcDetach(PIN_SENSOR_PWM);
    sensor_excitation_active = false;
  }

  pinMode(PIN_SENSOR_PWM, OUTPUT);
  digitalWrite(PIN_SENSOR_PWM, LOW);
}

bool takeMeasurement(
    Measurement& result,
    const Measurement* battery_reference = nullptr) {
  result = Measurement{};

  if (!startSensorExcitation()) {
    Serial.println(
        "Soil excitation failed while taking measurement.");
    return false;
  }

  delay(SENSOR_EXCITATION_SETTLE_MS);

  result.soil =
      readAveraged(
          PIN_SOIL_ADC,
          SOIL_SAMPLES,
          200);

  // The probe is no longer needed once the soil ADC has been captured.
  // Shut it off before battery/network work instead of leaving 200 kHz PWM
  // running for the entire awake/service/provisioning window.
  stopSensorExcitation();

  if (battery_reference != nullptr) {
    // Service mode already has a battery sample captured before Wi-Fi starts.
    // Reuse it while Wi-Fi remains deliberately awake so radio load/sag is not
    // misreported as rapid AA capacity loss every five seconds.
    result.battery =
        battery_reference->battery;
    result.battery_percent =
        battery_reference->battery_percent;
  } else {
    delay(BATTERY_RECOVERY_MS);

    result.battery =
        readAveraged(
            PIN_BATTERY_ADC,
            BATTERY_SAMPLES,
            20);

    result.battery_percent =
        batteryPercentFromMillivolts(
            static_cast<float>(result.battery.mv));
  }

  result.moisture_percent =
      moisturePercentFromMillivolts(
          static_cast<float>(result.soil.mv));

  result.moisture_state =
      stateFromPercent(
          result.moisture_percent);

  return true;
}

plant::ReadingPacket makeReadingPacket(
    const Measurement& measurement) {
  plant::ReadingPacket packet{};

  packet.magic = plant::PACKET_MAGIC;
  packet.version = plant::PROTOCOL_VERSION;
  packet.packet_size = sizeof(packet);
  packet.sensor_id = sensor_id;
  packet.sequence = sequence_number++;

  packet.moisture_raw =
      static_cast<uint16_t>(
          measurement.soil.raw > 65535
              ? 65535
              : measurement.soil.raw);

  packet.moisture_percent =
      measurement.moisture_percent;

  packet.moisture_state =
      measurement.moisture_state;

  packet.battery_mv =
      static_cast<uint16_t>(
          measurement.battery.mv > 65535
              ? 65535
              : measurement.battery.mv);

  packet.battery_percent =
      measurement.battery_percent;

  packet.reserved_rssi = 0;
  packet.awake_ms = millis();

  plant::finalizePacket(packet);
  return packet;
}

void printMeasurement(const Measurement& measurement) {
  Serial.println("\n--- REAL SENSOR MEASUREMENT ---");

  Serial.printf(
      "Soil raw: %lu\n",
      static_cast<unsigned long>(
          measurement.soil.raw));

  Serial.printf(
      "Soil voltage: %lu mV\n",
      static_cast<unsigned long>(
          measurement.soil.mv));

  Serial.printf(
      "Moisture: %u%% (%s)\n",
      measurement.moisture_percent,
      plant::stateName(
          measurement.moisture_state));

  Serial.printf(
      "Battery: %u%% (%lu mV)\n",
      measurement.battery_percent,
      static_cast<unsigned long>(
          measurement.battery.mv));
}

[[noreturn]] void sleepFor(uint32_t seconds) {
  if (seconds == 0) {
    seconds = NORMAL_CHECK_SECONDS;
  }

  initializeAdaptiveStateIfNeeded();
  adaptive.planned_sleep_seconds = seconds;

  // Do not enter deep sleep while the active-low wake button is still held.
  // Otherwise the chip could immediately wake itself again.
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(
        "Waiting for top button release before sleep...");

    while (digitalRead(PIN_BUTTON) == LOW) {
      delay(20);
    }

    delay(80);
  }

  Serial.printf(
      "Deep sleeping for %lu seconds.\n",
      static_cast<unsigned long>(seconds));

  Serial.println(
      "Wake sources: timer OR top button.");
  Serial.flush();

  allStatusLedsOff();

  udp.stop();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  stopSensorExcitation();

  esp_sleep_enable_timer_wakeup(
      static_cast<uint64_t>(seconds) *
      1000000ULL);

  // ESP32-C6 GPIO2 is RTC-capable. The board button is active LOW.
  // EXT1 gives us an unambiguous ESP_SLEEP_WAKEUP_EXT1 wake reason.
  const esp_err_t button_wake_result =
      esp_sleep_enable_ext1_wakeup(
          1ULL << PIN_BUTTON,
          ESP_EXT1_WAKEUP_ANY_LOW);

  if (button_wake_result != ESP_OK) {
    Serial.printf(
        "WARNING: top-button wake setup failed: %s\n",
        esp_err_to_name(button_wake_result));
    Serial.flush();
  }

  esp_deep_sleep_start();

  while (true) {
    delay(1000);
  }
}

[[noreturn]] void sleepUntilButtonOnly() {
  initializeAdaptiveStateIfNeeded();
  adaptive.planned_sleep_seconds = 0;

  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(
        "Waiting for top button release before button-only sleep...");

    while (digitalRead(PIN_BUTTON) == LOW) {
      delay(20);
    }

    delay(80);
  }

  Serial.println(
      "Entering timerless deep sleep after three failed provisioning attempts.");
  Serial.println(
      "Wake source: TOP BUTTON ONLY.");
  Serial.flush();

  allStatusLedsOff();

  udp.stop();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  stopSensorExcitation();

  esp_sleep_disable_wakeup_source(
      ESP_SLEEP_WAKEUP_TIMER);

  const esp_err_t button_wake_result =
      esp_sleep_enable_ext1_wakeup(
          1ULL << PIN_BUTTON,
          ESP_EXT1_WAKEUP_ANY_LOW);

  if (button_wake_result != ESP_OK) {
    Serial.printf(
        "WARNING: top-button wake setup failed: %s\n",
        esp_err_to_name(button_wake_result));
    Serial.flush();
  }

  esp_deep_sleep_start();

  while (true) {
    delay(1000);
  }
}

[[noreturn]] void finishFailedProvisioningAttempt(
    const char* reason) {
  provisioning_mode_active = false;
  allStatusLedsOff();

  if (provisioning_failed_windows <
      UINT8_MAX) {
    provisioning_failed_windows++;
  }

  Serial.printf(
      "Provisioning attempt failed: %s\n",
      reason);
  Serial.printf(
      "Failed provisioning attempts: %u/%u.\n",
      provisioning_failed_windows,
      PROVISION_MAX_FAILED_WINDOWS);

  if (provisioning_failed_windows >=
      PROVISION_MAX_FAILED_WINDOWS) {
    Serial.println(
        "Three provisioning attempts failed. Automatic retries are disabled.");
    Serial.println(
        "Press the top button to start a fresh three-attempt setup cycle.");

    sleepUntilButtonOnly();
  }

  const uint32_t retry_sleep_seconds =
      provisioning_failed_windows == 1
          ? PROVISION_FIRST_RETRY_SLEEP_SECONDS
          : PROVISION_SECOND_RETRY_SLEEP_SECONDS;

  Serial.printf(
      "Provisioning retry will wake in %lu minutes.\n",
      static_cast<unsigned long>(
          retry_sleep_seconds / 60));

  sleepFor(
      retry_sleep_seconds);
}

// ============================================================================
// ESP-NOW PROVISIONING
// ============================================================================

void sendProvisionAck(const uint8_t* mac) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  plant::ProvisionAckPacket ack{};
  ack.magic = plant::PACKET_MAGIC;
  ack.version = plant::PROTOCOL_VERSION;
  ack.packet_size = sizeof(ack);
  ack.sensor_id = sensor_id;
  ack.accepted = 1;
  plant::finalizePacket(ack);

  esp_now_send(
      mac,
      reinterpret_cast<const uint8_t*>(&ack),
      sizeof(ack));
}

void sendIdentityAckEspNow(
    const uint8_t* mac,
    const plant::IdentityPacket& request,
    bool accepted) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    const esp_err_t peer_result =
        esp_now_add_peer(&peer);

    if (peer_result != ESP_OK) {
      Serial.printf(
          "Identity ACK peer add failed: %s\n",
          esp_err_to_name(peer_result));
      return;
    }
  }

  plant::IdentityAckPacket ack{};
  ack.magic = plant::PACKET_MAGIC;
  ack.version = plant::PROTOCOL_VERSION;
  ack.packet_size = sizeof(ack);
  ack.sensor_id = sensor_id;
  ack.slot =
      request.command ==
              plant::IdentityCommand::Clear
          ? 0
          : request.slot;
  ack.accepted = accepted ? 1 : 0;
  ack.request_id =
      request.request_id;

  plant::finalizePacket(ack);

  const esp_err_t result =
      esp_now_send(
          mac,
          reinterpret_cast<const uint8_t*>(
              &ack),
          sizeof(ack));

  Serial.printf(
      "Identity ACK via ESP-NOW: request %u, slot #%u, %s (%s).\n",
      ack.request_id,
      ack.slot,
      accepted ? "ACCEPTED" : "REJECTED",
      esp_err_to_name(result));
}

void queueIdentityFromEspNow(
    const uint8_t* source_mac,
    const plant::IdentityPacket& packet) {
  if (identity_queue == nullptr)
    return;

  PendingIdentityEvent event{};
  memcpy(
      event.source_mac,
      source_mac,
      6);
  event.packet = packet;

  if (xQueueSend(
          identity_queue,
          &event,
          0) != pdPASS) {
    Serial.println(
        "Identity queue full; T5 will resend confirmation.");
  }
}

void servicePendingEspNowIdentity() {
  if (identity_queue == nullptr)
    return;

  PendingIdentityEvent event{};

  while (xQueueReceive(
             identity_queue,
             &event,
             0) == pdTRUE) {
    const bool accepted =
        applyIdentityPacket(
            event.packet);

    sendIdentityAckEspNow(
        event.source_mac,
        event.packet,
        accepted);
  }
}

uint8_t currentRadioChannel();

bool acceptLocatePacket(
    const uint8_t* data,
    int length,
    const char* transport_name) {
  if (length !=
      sizeof(plant::LocatePacket)) {
    return false;
  }

  plant::LocatePacket packet{};
  memcpy(
      &packet,
      data,
      sizeof(packet));

  if (!plant::validatePacket(packet))
    return false;

  if (packet.sensor_id != sensor_id)
    return false;

  uint32_t flash_ms =
      packet.flash_ms;

  if (flash_ms < 2000)
    flash_ms = 2000;

  if (flash_ms > 15000)
    flash_ms = 15000;

  locate_request_ms.store(
      flash_ms,
      std::memory_order_release);

  Serial.printf(
      "Locate request accepted via %s: flash for %lu ms.\n",
      transport_name,
      static_cast<unsigned long>(
          flash_ms));

  return true;
}

void handleEspNowPacket(
    const uint8_t* source_mac,
    const uint8_t* data,
    int length) {
  if (length ==
      sizeof(plant::IdentityPacket)) {
    plant::IdentityPacket packet{};
    memcpy(
        &packet,
        data,
        sizeof(packet));

    if (plant::validatePacket(packet) &&
        packet.sensor_id == sensor_id) {
      queueIdentityFromEspNow(
          source_mac,
          packet);
    }

    return;
  }

  if (acceptLocatePacket(
          data,
          length,
          "ESP-NOW")) {
    return;
  }

  if (length ==
      sizeof(plant::AckPacket)) {
    plant::AckPacket ack{};
    memcpy(&ack, data, sizeof(ack));

    if (plant::validatePacket(ack) &&
        ack.sensor_id == sensor_id &&
        ack.accepted == 1) {
      if (provisioning_mode_active) {
        const uint8_t channel =
            currentRadioChannel();

        discovered_t5_channel.store(
            channel,
            std::memory_order_release);
        last_t5_espnow_ack_ms.store(
            millis(),
            std::memory_order_release);
        provision_channel_locked.store(
            true,
            std::memory_order_release);

        Serial.printf(
            "T5 ESP-NOW discovered; locking provisioning to channel %u.\n",
            channel);
      }
    }

    return;
  }

  if (length !=
      sizeof(plant::ProvisionPacket)) {
    return;
  }

  plant::ProvisionPacket packet{};
  memcpy(&packet, data, sizeof(packet));

  if (!plant::validatePacket(packet)) return;
  if (packet.sensor_id != sensor_id) return;
  if (packet.wifi_ssid[0] == '\0') return;

  SensorConfig candidate{};
  candidate.magic = SENSOR_CONFIG_MAGIC;
  candidate.version = SENSOR_CONFIG_VERSION;
  candidate.structure_size = sizeof(SensorConfig);

  memcpy(
      candidate.wifi_ssid,
      packet.wifi_ssid,
      sizeof(candidate.wifi_ssid));

  memcpy(
      candidate.wifi_password,
      packet.wifi_password,
      sizeof(candidate.wifi_password));

  candidate.t5_udp_port =
      packet.t5_udp_port != 0
          ? packet.t5_udp_port
          : plant::T5_UDP_PORT;

  candidate.wifi_ssid[
      sizeof(candidate.wifi_ssid) - 1] = '\0';

  candidate.wifi_password[
      sizeof(candidate.wifi_password) - 1] = '\0';

  candidate.checksum =
      configChecksum(candidate);

  pending_config = candidate;

  sendProvisionAck(source_mac);
  provision_received.store(
      true,
      std::memory_order_release);

  Serial.println();
  Serial.println("Provision packet accepted.");
  Serial.printf(
      "Home Wi-Fi: %s\n",
      candidate.wifi_ssid);
}

void onEspNowReceive(
    const esp_now_recv_info_t* info,
    const uint8_t* data,
    int length) {
  handleEspNowPacket(
      info->src_addr,
      data,
      length);
}

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

bool setProvisionChannel(
    uint8_t channel) {
  const esp_err_t result =
      esp_wifi_set_channel(
          channel,
          WIFI_SECOND_CHAN_NONE);

  if (result != ESP_OK) {
    Serial.printf(
        "Provision channel %u failed: %s\n",
        channel,
        esp_err_to_name(result));
    return false;
  }

  return true;
}

bool addBroadcastPeer() {
  if (esp_now_is_peer_exist(BROADCAST_MAC)) {
    return true;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  const esp_err_t result =
      esp_now_add_peer(&peer);

  if (result != ESP_OK) {
    Serial.printf(
        "Broadcast peer failed: %s\n",
        esp_err_to_name(result));
    return false;
  }

  return true;
}

bool startEspNowCurrentChannel() {
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

  if (!addBroadcastPeer()) {
    esp_now_deinit();
    return false;
  }

  esp_now_ready = true;

  Serial.printf(
      "ESP-NOW ready on current Wi-Fi channel %u.\n",
      currentRadioChannel());

  return true;
}

void sendEspNowReadingBeacon(
    const plant::ReadingPacket& reading,
    const char* label) {
  if (!esp_now_ready)
    return;

  const esp_err_t result =
      esp_now_send(
          BROADCAST_MAC,
          reinterpret_cast<const uint8_t*>(&reading),
          sizeof(reading));

  Serial.printf(
      "%s ESP-NOW beacon ch%u: %s\n",
      label,
      currentRadioChannel(),
      esp_err_to_name(result));
}


void sendProvisionBeacon(
    const plant::ReadingPacket& reading) {
  const esp_err_t result =
      esp_now_send(
          BROADCAST_MAC,
          reinterpret_cast<const uint8_t*>(&reading),
          sizeof(reading));

  Serial.printf(
      "Provision beacon: %s\n",
      esp_err_to_name(result));
}

void runLocateFlash(
    uint32_t duration_ms) {
  const uint32_t started =
      millis();

  if (identity_confirmed_this_wake &&
      cached_identity_slot >= 1 &&
      cached_identity_slot <=
          MAX_IDENTITY_SLOT) {
    Serial.printf(
        "IDENTIFY: Sensor #%u, RED long=10 / short=1, repeating for %lu ms.\n",
        cached_identity_slot,
        static_cast<unsigned long>(
            duration_ms));

    while ((millis() - started) <
           duration_ms) {
      flashSensorNumberOnce(
          cached_identity_slot);

      if ((millis() - started) <
          duration_ms) {
        delay(600);
      }
    }

    delay(
        IDENTITY_TO_SERVICE_PAUSE_MS);
  } else {
    // Never show a potentially stale cached number without current T5
    // confirmation. Keep the old generic flash as a safe fallback.
    Serial.printf(
        "LOCATE: no T5-confirmed slot this wake; using generic LEDs for %lu ms.\n",
        static_cast<unsigned long>(
            duration_ms));

    bool on = false;

    while ((millis() - started) <
           duration_ms) {
      on = !on;

      digitalWrite(
          PIN_LED_YELLOW,
          on ? HIGH : LOW);
      digitalWrite(
          PIN_LED_GREEN,
          on ? HIGH : LOW);
      digitalWrite(
          PIN_LED_RED,
          on ? HIGH : LOW);

      delay(140);
    }
  }

  allStatusLedsOff();

  if (provisioning_mode_active) {
    updateUnprovisionedIndicator(
        millis());
  } else {
    serviceLedOn();
  }

  Serial.println(
      "IDENTIFY/LOCATE complete.");
}

[[noreturn]] void runProvisioningMode(
    const Measurement& measurement) {
  allStatusLedsOff();
  provisioning_mode_active = true;
  provision_channel_locked.store(
      false,
      std::memory_order_release);
  discovered_t5_channel.store(
      0,
      std::memory_order_release);
  last_t5_espnow_ack_ms.store(
      0,
      std::memory_order_release);

  Serial.println();
  Serial.println(
      "=== SENSOR WI-FI PROVISIONING MODE ===");

  const uint8_t attempt_number =
      static_cast<uint8_t>(
          provisioning_failed_windows + 1);

  Serial.printf(
      "Provisioning attempt %u/%u. Unprovisioned LED pattern: RED/GREEN alternating.\n",
      attempt_number,
      PROVISION_MAX_FAILED_WINDOWS);

  Serial.println(
      "Searching 2.4 GHz channels for the T5 ESP-NOW receiver. "
      "Once found, this sensor locks to that channel for Locate/provisioning.");

  if (!WiFi.mode(WIFI_STA)) {
    finishFailedProvisioningAttempt(
        "could not start Wi-Fi STA");
  }

  WiFi.disconnect(false, false);
  delay(100);

  uint8_t channel =
      PROVISION_CHANNEL_MIN;

  if (!setProvisionChannel(channel)) {
    finishFailedProvisioningAttempt(
        "could not set initial ESP-NOW channel");
  }

  if (!startEspNowCurrentChannel()) {
    finishFailedProvisioningAttempt(
        "could not start ESP-NOW");
  }

  const plant::ReadingPacket beacon =
      makeReadingPacket(measurement);

  uint32_t last_beacon_ms = 0;
  uint32_t channel_started_ms =
      millis();
  const uint32_t start_ms = millis();

  while ((millis() - start_ms) <
         PROVISION_WINDOW_MS) {
    updateUnprovisionedIndicator(
        millis());

    // ESP-NOW callbacks only queue identity requests. NVS writes and the
    // application-level ACK happen here on the normal Arduino task.
    servicePendingEspNowIdentity();

    const uint32_t locate_ms =
        locate_request_ms.exchange(
            0,
            std::memory_order_acq_rel);

    if (locate_ms > 0) {
      runLocateFlash(locate_ms);
    }

    if (provision_received.load(
            std::memory_order_acquire)) {
      if (saveConfig(pending_config)) {
        provisioning_failed_windows = 0;

        // Drain any slot assignment that arrived immediately before the
        // ProvisionPacket so the number survives this reboot too.
        servicePendingEspNowIdentity();

        if (identity_confirmed_this_wake &&
            cached_identity_slot > 0) {
          Serial.printf(
              "Provisioned as Sensor #%u; showing RED physical identity once.\n",
              cached_identity_slot);

          flashSensorNumberOnce(
              cached_identity_slot);
          delay(
              IDENTITY_TO_SERVICE_PAUSE_MS);
        }

        serviceLedOn();

        Serial.println(
            "Provisioning complete. Failed-attempt counter reset.");
        Serial.println(
            "Rebooting into home Wi-Fi mode...");

        Serial.flush();
        delay(900);
        ESP.restart();
      }

      provision_received.store(
          false,
          std::memory_order_release);
    }

    bool locked =
        provision_channel_locked.load(
            std::memory_order_acquire);

    if (locked) {
      const uint32_t last_ack =
          last_t5_espnow_ack_ms.load(
              std::memory_order_acquire);

      if (last_ack != 0 &&
          (millis() - last_ack) >=
              PROVISION_CHANNEL_LOCK_TIMEOUT_MS) {
        Serial.printf(
            "T5 disappeared from channel %u; resuming channel search.\n",
            currentRadioChannel());

        provision_channel_locked.store(
            false,
            std::memory_order_release);
        discovered_t5_channel.store(
            0,
            std::memory_order_release);
        locked = false;
        channel_started_ms = millis();
        last_beacon_ms = 0;
      }
    }

    if (!locked &&
        (millis() - channel_started_ms) >=
            PROVISION_CHANNEL_DWELL_MS) {
      channel++;

      if (channel >
          PROVISION_CHANNEL_MAX) {
        channel =
            PROVISION_CHANNEL_MIN;
      }

      if (setProvisionChannel(channel)) {
        channel_started_ms =
            millis();
        last_beacon_ms = 0;
      }
    }

    const uint32_t beacon_interval =
        locked
            ? PROVISION_BEACON_INTERVAL_MS
            : PROVISION_CHANNEL_DWELL_MS;

    if (last_beacon_ms == 0 ||
        (millis() - last_beacon_ms) >=
            beacon_interval) {
      last_beacon_ms = millis();

      if (locked) {
        Serial.printf(
            "Waiting for T5 provisioning (sensor 0x%08lX, locked ch%u)...\n",
            static_cast<unsigned long>(sensor_id),
            currentRadioChannel());
      } else {
        Serial.printf(
            "Searching for T5: ESP-NOW channel %u (sensor 0x%08lX)...\n",
            currentRadioChannel(),
            static_cast<unsigned long>(sensor_id));
      }

      sendProvisionBeacon(beacon);
    }

    delay(20);
  }

  Serial.println(
      "Provisioning window expired.");

  finishFailedProvisioningAttempt(
      "two-minute provisioning window expired");
}

// ============================================================================
// HOME WI-FI + UDP NORMAL TRANSPORT
// ============================================================================

bool connectHomeWifi() {
  Serial.printf(
      "Connecting to home Wi-Fi \"%s\"...\n",
      config_data.wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(
      config_data.wifi_ssid,
      config_data.wifi_password);

  const uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (service_mode_active &&
        digitalRead(PIN_BUTTON) == LOW) {
      Serial.println(
          "Service button pressed; pausing Wi-Fi connect for hold detection.");
      WiFi.disconnect(false, false);
      return false;
    }

    if ((millis() - start) >=
        WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(
          "Home Wi-Fi connection timed out.");
      return false;
    }

    delay(100);
  }

  Serial.println("Home Wi-Fi connected.");
  Serial.print("Sensor IP: ");
  Serial.println(WiFi.localIP());

  Serial.printf(
      "RSSI: %d dBm, channel: %d\n",
      WiFi.RSSI(),
      WiFi.channel());

  if (service_mode_active) {
    if (!startEspNowCurrentChannel()) {
      Serial.println(
          "WARNING: service-mode ESP-NOW coexistence init failed; UDP will still operate.");
    }
  }

  return true;
}

void sendIdentityAckUdp(
    const IPAddress& remote_ip,
    uint16_t remote_port,
    const plant::IdentityPacket& request,
    bool accepted) {
  plant::IdentityAckPacket ack{};

  ack.magic = plant::PACKET_MAGIC;
  ack.version = plant::PROTOCOL_VERSION;
  ack.packet_size = sizeof(ack);
  ack.sensor_id = sensor_id;
  ack.slot =
      request.command ==
              plant::IdentityCommand::Clear
          ? 0
          : request.slot;
  ack.accepted = accepted ? 1 : 0;
  ack.request_id =
      request.request_id;

  plant::finalizePacket(ack);

  if (!udp.beginPacket(
          remote_ip,
          remote_port)) {
    Serial.println(
        "Identity UDP ACK beginPacket failed.");
    return;
  }

  const size_t written =
      udp.write(
          reinterpret_cast<const uint8_t*>(
              &ack),
          sizeof(ack));

  const int end_result =
      udp.endPacket();

  Serial.printf(
      "Identity ACK via UDP: request %u, slot #%u, %s (%s).\n",
      ack.request_id,
      ack.slot,
      accepted ? "ACCEPTED" : "REJECTED",
      written == sizeof(ack) &&
              end_result == 1
          ? "SENT"
          : "FAILED");
}

bool handleIdentityUdpPacket(
    int packet_size) {
  if (packet_size !=
      sizeof(plant::IdentityPacket)) {
    return false;
  }

  const IPAddress remote_ip =
      udp.remoteIP();
  const uint16_t remote_port =
      udp.remotePort();

  plant::IdentityPacket packet{};

  const int read =
      udp.read(
          reinterpret_cast<uint8_t*>(
              &packet),
          sizeof(packet));

  if (read != sizeof(packet) ||
      !plant::validatePacket(packet) ||
      packet.sensor_id != sensor_id) {
    Serial.println(
        "Rejected invalid UDP identity packet.");
    return true;
  }

  const bool accepted =
      applyIdentityPacket(
          packet);

  sendIdentityAckUdp(
      remote_ip,
      remote_port,
      packet,
      accepted);

  return true;
}

bool readUdpAck(
    uint32_t expected_sequence,
    uint32_t& next_wake_seconds) {
  const uint32_t start = millis();

  while ((millis() - start) <
         UDP_ACK_TIMEOUT_MS) {
    if (service_mode_active &&
        locate_request_ms.load(
            std::memory_order_acquire) > 0) {
      Serial.println(
          "ESP-NOW Locate received; pausing UDP ACK wait to flash now.");
      return false;
    }

    if (service_mode_active &&
        digitalRead(PIN_BUTTON) == LOW) {
      Serial.println(
          "Service button pressed; pausing UDP ACK wait for hold detection.");
      return false;
    }

    const int packet_size = udp.parsePacket();

    if (packet_size > 0) {
      if (handleIdentityUdpPacket(
              packet_size)) {
        // Identity confirmation deliberately precedes the normal reading ACK.
        // Keep waiting for the matching AckPacket.
      } else if (packet_size ==
                 sizeof(plant::AckPacket)) {
        plant::AckPacket ack{};

        const int read =
            udp.read(
                reinterpret_cast<uint8_t*>(&ack),
                sizeof(ack));

        if (read == sizeof(ack) &&
            plant::validatePacket(ack) &&
            ack.sensor_id == sensor_id &&
            ack.sequence == expected_sequence &&
            ack.accepted == 1) {
          next_wake_seconds =
              ack.next_wake_seconds;

          Serial.print("UDP ACK from ");
          Serial.print(udp.remoteIP());
          Serial.print(":");
          Serial.println(udp.remotePort());

          return true;
        }
      } else if (packet_size ==
                 sizeof(plant::LocatePacket)) {
        plant::LocatePacket locate{};

        const int read =
            udp.read(
                reinterpret_cast<uint8_t*>(&locate),
                sizeof(locate));

        if (read == sizeof(locate)) {
          acceptLocatePacket(
              reinterpret_cast<const uint8_t*>(
                  &locate),
              sizeof(locate),
              "Wi-Fi/UDP");
        }
      } else {
        while (udp.available()) {
          udp.read();
        }
      }
    }

    delay(10);
  }

  return false;
}

void serviceUdpLocateCommands() {
  if (!service_mode_active ||
      WiFi.status() != WL_CONNECTED) {
    return;
  }

  int packet_size =
      udp.parsePacket();

  while (packet_size > 0) {
    if (handleIdentityUdpPacket(
            packet_size)) {
      // Continue draining queued service commands.
    } else if (packet_size ==
               sizeof(plant::LocatePacket)) {
      plant::LocatePacket locate{};

      const int read =
          udp.read(
              reinterpret_cast<uint8_t*>(
                  &locate),
              sizeof(locate));

      if (read == sizeof(locate)) {
        acceptLocatePacket(
            reinterpret_cast<const uint8_t*>(
                &locate),
            sizeof(locate),
            "Wi-Fi/UDP");
      }
    } else {
      while (udp.available()) {
        udp.read();
      }
    }

    packet_size =
        udp.parsePacket();
  }
}

IPAddress directedBroadcastAddress() {
  const IPAddress ip = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();

  return IPAddress(
      static_cast<uint8_t>(
          ip[0] | static_cast<uint8_t>(~mask[0])),
      static_cast<uint8_t>(
          ip[1] | static_cast<uint8_t>(~mask[1])),
      static_cast<uint8_t>(
          ip[2] | static_cast<uint8_t>(~mask[2])),
      static_cast<uint8_t>(
          ip[3] | static_cast<uint8_t>(~mask[3])));
}

bool sendReadingUdp(
    const plant::ReadingPacket& reading,
    uint32_t& next_wake_seconds) {
  if (!udp.begin(plant::SENSOR_UDP_PORT)) {
    Serial.println(
        "Could not open sensor UDP port.");
    return false;
  }

  const IPAddress broadcast_ip =
      directedBroadcastAddress();

  Serial.print("LAN broadcast: ");
  Serial.println(broadcast_ip);

  Serial.printf(
      "T5 UDP destination port: %u%s\n",
      config_data.t5_udp_port,
      config_data.t5_udp_port == plant::T5_UDP_PORT
          ? ""
          : " (saved non-default port)");

  for (uint8_t attempt = 1;
       attempt <= UDP_MAX_ATTEMPTS;
       ++attempt) {
    Serial.printf(
        "UDP sequence %lu, attempt %u/%u\n",
        static_cast<unsigned long>(
            reading.sequence),
        attempt,
        UDP_MAX_ATTEMPTS);

    if (!udp.beginPacket(
            broadcast_ip,
            config_data.t5_udp_port)) {
      Serial.println(
          "UDP beginPacket failed.");
      delay(250);
      continue;
    }

    const size_t written =
        udp.write(
            reinterpret_cast<const uint8_t*>(&reading),
            sizeof(reading));

    const int end_result =
        udp.endPacket();

    if (written != sizeof(reading) ||
        end_result != 1) {
      Serial.println(
          "UDP packet send failed.");
      delay(250);
      continue;
    }

    if (readUdpAck(
            reading.sequence,
            next_wake_seconds)) {
      Serial.println(
          "HOME WI-FI EXCHANGE SUCCEEDED");
      return true;
    }

    if (service_mode_active &&
        digitalRead(PIN_BUTTON) == LOW) {
      Serial.println(
          "Service button pressed; aborting remaining UDP retries.");
      return false;
    }

    Serial.println("UDP ACK timeout.");
    delay(250);
  }

  Serial.println(
      "HOME WI-FI EXCHANGE FAILED");
  return false;
}


void setOnlyStatusLed(
    uint8_t pin,
    bool on) {
  allStatusLedsOff();

  if (on) {
    digitalWrite(pin, HIGH);
  }
}

void blinkPlacementWindow(
    uint8_t led_pin,
    const char* label) {
  Serial.printf(
      "%s: you have 10 seconds to position the probe.\n",
      label);

  const uint32_t started =
      millis();

  bool led_on = false;

  while ((millis() - started) <
         CALIBRATION_PLACEMENT_MS) {
    led_on = !led_on;
    setOnlyStatusLed(
        led_pin,
        led_on);
    delay(250);
  }

  setOnlyStatusLed(
      led_pin,
      false);
}

float readCalibrationSoilMillivolts(
    const char* label) {
  Serial.printf(
      "%s: averaging %u soil samples...\n",
      label,
      CALIBRATION_SAMPLES);

  if (!startSensorExcitation()) {
    Serial.println(
        "Calibration soil excitation failed.");
    return NAN;
  }

  delay(SENSOR_EXCITATION_SETTLE_MS);

  uint64_t mv_sum = 0;

  // Prime the ADC in the same manner as normal readings.
  (void)analogRead(PIN_SOIL_ADC);
  (void)analogReadMilliVolts(
      PIN_SOIL_ADC);
  delay(10);

  for (uint8_t i = 0;
       i < CALIBRATION_SAMPLES;
       ++i) {
    const uint32_t mv =
        analogReadMilliVolts(
            PIN_SOIL_ADC);

    mv_sum += mv;

    Serial.printf(
        "  sample %u/%u: %lu mV\n",
        static_cast<unsigned>(i + 1),
        static_cast<unsigned>(
            CALIBRATION_SAMPLES),
        static_cast<unsigned long>(mv));

    delay(
        CALIBRATION_SAMPLE_DELAY_MS);
  }

  stopSensorExcitation();

  const float average =
      static_cast<float>(mv_sum) /
      static_cast<float>(
          CALIBRATION_SAMPLES);

  Serial.printf(
      "%s average: %.0f mV\n",
      label,
      average);

  return average;
}

void blinkCalibrationResult(
    bool success) {
  const uint8_t pin =
      success
          ? PIN_LED_GREEN
          : PIN_LED_RED;

  allStatusLedsOff();

  for (uint8_t i = 0; i < 2; ++i) {
    digitalWrite(pin, HIGH);
    delay(180);
    digitalWrite(pin, LOW);
    delay(180);
  }

  delay(350);
}

bool runFactoryStyleCalibration() {
  Serial.println();
  Serial.println(
      "=== SOIL CALIBRATION ===");
  Serial.println(
      "Factory-style two-point calibration started.");
  Serial.println(
      "Stage 1: completely DRY soil.");
  Serial.println(
      "Red LED will flash for 10 seconds.");

  blinkPlacementWindow(
      PIN_LED_RED,
      "DRY stage");

  const float dry_mv =
      readCalibrationSoilMillivolts(
          "DRY");

  Serial.println(
      "Dry capture complete. "
      "Waiting 3 seconds...");
  delay(
      CALIBRATION_STAGE_PAUSE_MS);

  Serial.println();
  Serial.println(
      "Stage 2: fully WET soil.");
  Serial.println(
      "Green LED will flash for 10 seconds.");

  blinkPlacementWindow(
      PIN_LED_GREEN,
      "WET stage");

  const float wet_mv =
      readCalibrationSoilMillivolts(
          "WET");

  Serial.println(
      "Wet capture complete. "
      "Waiting 3 seconds...");
  delay(
      CALIBRATION_STAGE_PAUSE_MS);

  const bool valid =
      calibrationValuesValid(
          dry_mv,
          wet_mv);

  if (!valid) {
    Serial.printf(
        "CALIBRATION FAILED: "
        "dry=%.0f mV, wet=%.0f mV. "
        "Dry must be higher than wet with at least %.0f mV span.\n",
        dry_mv,
        wet_mv,
        MIN_CALIBRATION_SPAN_MV);

    blinkCalibrationResult(false);
    serviceLedOn();
    return false;
  }

  if (!saveCalibration(
          dry_mv,
          wet_mv)) {
    Serial.println(
        "CALIBRATION FAILED: "
        "could not save NVS.");
    blinkCalibrationResult(false);
    serviceLedOn();
    return false;
  }

  Serial.println(
      "CALIBRATION SUCCESS.");
  Serial.println(
      "Two green flashes = saved.");

  blinkCalibrationResult(true);

  // New endpoints invalidate comparisons against percentages calculated with
  // the old scale.  Start a fresh adaptive baseline on the next sample.
  memset(
      &adaptive,
      0,
      sizeof(adaptive));
  adaptive.magic =
      ADAPTIVE_STATE_MAGIC;

  serviceLedOn();
  return true;
}

// ============================================================================
// TOP-BUTTON SERVICE MODE
// ============================================================================

bool sendFreshServiceReading(
    uint32_t& t5_requested_wake_seconds,
    const Measurement& battery_reference) {
  Measurement measurement{};

  if (!takeMeasurement(
          measurement,
          &battery_reference)) {
    Serial.println(
        "Service measurement failed; keeping the service window alive.");
    return false;
  }

  printMeasurement(measurement);

  if (service_mode_active &&
      digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(
        "Service button pressed during measurement; UDP send deferred.");
    return false;
  }

  armWateringWatchIfRise(
      measurement,
      "service auto-sample");

  rememberObservation(measurement);

  const plant::ReadingPacket reading =
      makeReadingPacket(measurement);

  sendEspNowReadingBeacon(
      reading,
      "Service");

  const bool acknowledged =
      sendReadingUdp(
          reading,
          t5_requested_wake_seconds);

  if (acknowledged) {
    rememberSuccessfulReport(
        measurement);
  }

  return acknowledged;
}

[[noreturn]] void runServiceMode(
    const Measurement& initial_measurement) {
  serviceLedOn();

  Serial.println();
  Serial.println(
      "=== TWO-MINUTE BUTTON SERVICE MODE ===");
  Serial.println(
      "Green LED stays ON while the sensor is manually awake.");
  Serial.println(
      "Fresh soil readings are sampled automatically every 5 seconds.");
  Serial.println(
      "Battery value reuses the pre-Wi-Fi service-entry sample.");
  Serial.println(
      "Single short press: send immediately and restart the 2-minute timer.");
  Serial.println(
      "Triple short press: calibrate DRY then WET soil.");
  Serial.println(
      "Hold 10 seconds while already awake: erase saved Wi-Fi.");
  Serial.println(
      "While this green service window is active, Wi-Fi/UDP and ESP-NOW run together so the T5 can Locate this sensor directly.");

  uint32_t t5_requested_wake_seconds = 0;

  armWateringWatchIfRise(
      initial_measurement,
      "button-wake reading");

  rememberObservation(
      initial_measurement);

  // A button wake naturally enters this function with GPIO2 still LOW.
  // Do not start Wi-Fi/UDP or count a reset hold until that original wake
  // press has been released.
  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println(
        "Release wake button to start the 2-minute service window.");

    while (digitalRead(PIN_BUTTON) == LOW) {
      delay(20);
    }

    delay(80);
  }

  Serial.println(
      "Wake button released; 2-minute service timer started.");

  // Keep GREEN visible while Wi-Fi and the T5 identity handshake happen.
  // Once the T5 confirms the slot, the deliberate RED number pattern briefly
  // takes over, then service returns to solid GREEN.
  serviceLedOn();
  identity_confirmed_this_wake = false;

  service_mode_active = true;

  bool wifi_connected =
      connectHomeWifi();

  bool identity_number_shown = false;

  if (wifi_connected) {
    const plant::ReadingPacket first_reading =
        makeReadingPacket(
            initial_measurement);

    sendEspNowReadingBeacon(
        first_reading,
        "Service");

    if (sendReadingUdp(
            first_reading,
            t5_requested_wake_seconds)) {
      rememberSuccessfulReport(
          initial_measurement);
    }

    // ESP-NOW identity may have arrived while the UDP exchange was running.
    servicePendingEspNowIdentity();

    if (identity_confirmed_this_wake &&
        cached_identity_slot > 0) {
      Serial.printf(
          "Button wake confirmed by T5 as Sensor #%u; showing RED identity.\n",
          cached_identity_slot);

      flashSensorNumberOnce(
          cached_identity_slot);
      identity_number_shown = true;
      delay(
          IDENTITY_TO_SERVICE_PAUSE_MS);
    }
  } else {
    Serial.println(
        "Home Wi-Fi unavailable. "
        "Service mode will stay awake and retry.");
  }

  serviceLedOn();

  uint32_t last_activity_ms =
      millis();

  uint32_t last_wifi_attempt_ms =
      millis();

  uint32_t last_service_sample_ms =
      millis();

  bool button_was_pressed = false;
  uint32_t button_pressed_since_ms = 0;
  bool reset_action_fired = false;

  uint8_t short_click_count = 0;
  uint32_t last_short_click_ms = 0;

  while ((millis() - last_activity_ms) <
         SERVICE_WINDOW_MS) {
    const bool button_pressed =
        digitalRead(PIN_BUTTON) == LOW;

    // Button handling is deliberately FIRST. Network retries and the 5-second
    // auto-sample are suspended for the entire press, so even a dead T5 cannot
    // starve the 10-second Wi-Fi erase action.
    if (button_pressed && !button_was_pressed) {
      button_was_pressed = true;
      button_pressed_since_ms = millis();
      reset_action_fired = false;

      last_activity_ms = millis();

      Serial.println(
          "Top button pressed; "
          "service timer restarted.");
    }

    if (button_pressed &&
        button_was_pressed &&
        !reset_action_fired &&
        (millis() - button_pressed_since_ms) >=
            FACTORY_RESET_HOLD_MS) {
      reset_action_fired = true;

      Serial.println();
      Serial.println(
          "10-second hold confirmed: "
          "erasing saved home Wi-Fi.");

      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, HIGH);

      provisioning_failed_windows = 0;
      clearConfig();

      Serial.println(
          "Wi-Fi erased. Provisioning failure counter reset.");
      Serial.println(
          "Restarting into provisioning mode...");
      Serial.flush();

      delay(1200);
      ESP.restart();
    }

    if (button_pressed) {
      delay(20);
      continue;
    }

    if (!button_pressed && button_was_pressed) {
      button_was_pressed = false;

      if (!reset_action_fired) {
        last_activity_ms = millis();

        short_click_count++;
        last_short_click_ms =
            millis();

        Serial.printf(
            "Short press %u/%u.\n",
            short_click_count,
            CALIBRATION_TRIGGER_CLICKS);

        if (short_click_count >=
            CALIBRATION_TRIGGER_CLICKS) {
          short_click_count = 0;

          // Calibration intentionally owns the LEDs and pauses ordinary
          // 5-second service sampling while dry/wet reference points are
          // captured.
          runFactoryStyleCalibration();

          last_activity_ms =
              millis();
          last_service_sample_ms =
              millis();

          // Immediately show a reading using the newly saved endpoints.
          if (WiFi.status() ==
              WL_CONNECTED) {
            Serial.println(
                "Calibration complete: "
                "sending a reading with the new scale.");

            sendFreshServiceReading(
                t5_requested_wake_seconds,
                initial_measurement);
          }
        }
      }
    }

    // Accept identity/Locate commands over both transports while the
    // provisioned sensor is deliberately awake.
    servicePendingEspNowIdentity();
    serviceUdpLocateCommands();

    if (!identity_number_shown &&
        identity_confirmed_this_wake &&
        cached_identity_slot > 0) {
      Serial.printf(
          "Late T5 identity confirmation: Sensor #%u; showing RED identity.\n",
          cached_identity_slot);

      flashSensorNumberOnce(
          cached_identity_slot);
      identity_number_shown = true;
      delay(
          IDENTITY_TO_SERVICE_PAUSE_MS);
      serviceLedOn();
      last_activity_ms = millis();
    }

    const uint32_t locate_ms =
        locate_request_ms.exchange(
            0,
            std::memory_order_acq_rel);

    if (locate_ms > 0) {
      last_activity_ms = millis();

      runLocateFlash(
          locate_ms);

      last_service_sample_ms =
          millis();

      continue;
    }

    // Do not fire a single-click action immediately, because the factory-style
    // triple press must be distinguishable. Once the multi-click window
    // expires, one or two clicks simply request a fresh reading.
    if (short_click_count > 0 &&
        (millis() - last_short_click_ms) >=
            CALIBRATION_CLICK_WINDOW_MS) {
      const uint8_t completed_clicks =
          short_click_count;

      short_click_count = 0;

      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(
            "%u short press%s: service timer restarted; identifying then sending fresh reading.\n",
            completed_clicks,
            completed_clicks == 1
                ? ""
                : "es");

        // Triple-click calibration has already been consumed above and resets
        // short_click_count to zero, so this only runs for ordinary one/two
        // click service actions. Never show an unconfirmed cached number.
        if (identity_confirmed_this_wake &&
            cached_identity_slot > 0) {
          flashSensorNumberOnce(
              cached_identity_slot);
          delay(
              IDENTITY_TO_SERVICE_PAUSE_MS);
          serviceLedOn();
        }

        sendFreshServiceReading(
            t5_requested_wake_seconds,
            initial_measurement);

        last_service_sample_ms =
            millis();
      } else {
        Serial.println(
            "Short press completed, "
            "but home Wi-Fi is not connected yet.");
      }
    }

    // Keep Wi-Fi alive for the service window. If coverage disappears,
    // stay awake and periodically retry instead of immediately sleeping.
    if (WiFi.status() != WL_CONNECTED) {
      wifi_connected = false;

      if ((millis() - last_wifi_attempt_ms) >=
          SERVICE_WIFI_RETRY_MS) {
        last_wifi_attempt_ms = millis();

        Serial.println(
            "Service mode: retrying home Wi-Fi...");

        wifi_connected =
            connectHomeWifi();

        if (wifi_connected) {
          Serial.println(
              "Service mode: Wi-Fi restored.");

          sendFreshServiceReading(
              t5_requested_wake_seconds,
              initial_measurement);

          last_service_sample_ms =
              millis();
        }
      }
    } else {
      wifi_connected = true;
    }

    // The sensor is deliberately awake in service mode, so don't make the
    // user keep pressing the button to see soil changes. Re-sample and send
    // on a short cadence for the entire green-LED window.
    if (wifi_connected &&
        short_click_count == 0 &&
        (millis() - last_service_sample_ms) >=
            SERVICE_SAMPLE_INTERVAL_MS) {
      last_service_sample_ms =
          millis();

      Serial.println();
      Serial.println(
          "Service auto-sample: taking fresh reading...");

      sendFreshServiceReading(
          t5_requested_wake_seconds,
          initial_measurement);
    }

    delay(20);
  }

  service_mode_active = false;

  Serial.println();
  Serial.println(
      "Two-minute service window expired.");
  Serial.println(
      "Green LED OFF; returning to adaptive deep sleep.");

  const plant::MoistureState last_state =
      adaptive.have_observed
          ? static_cast<plant::MoistureState>(
                adaptive.last_observed_state)
          : initial_measurement.moisture_state;

  uint32_t next_local_check_seconds =
      baseCheckSeconds(last_state);

  if (adaptive.watering_watch_active) {
    next_local_check_seconds =
        adaptive.watering_followups_done == 0
            ? WATERING_FIRST_FOLLOWUP_SECONDS
            : WATERING_FOLLOWUP_SECONDS;

    Serial.println(
        "Watering watch is active: do NOT use the normal WET 30-minute sleep.");
  }

  Serial.printf(
      "Next local soil check: %lu min.\n",
      static_cast<unsigned long>(
          next_local_check_seconds / 60));

  sleepFor(next_local_check_seconds);
}

}  // namespace

void setup() {
  // Capture the wake reason before doing any network work.
  const bool button_service_wake =
      wokeByButton();

  initStatusLeds();

  if (button_service_wake) {
    // Immediate visible confirmation that the top button successfully woke it.
    serviceLedOn();
  }

  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println(
      "XIAO Soil Sensor — Phase 3E Factory-Style Calibration");
  Serial.printf(
      "Protocol version: %u\n",
      plant::PROTOCOL_VERSION);

  printWakeReason();
  accountForWakeTime();

  if (provisioning_failed_windows >
      PROVISION_MAX_FAILED_WINDOWS) {
    provisioning_failed_windows = 0;
  }

  sensor_id = createSensorId();

  identity_queue =
      xQueueCreate(
          4,
          sizeof(PendingIdentityEvent));

  if (identity_queue == nullptr) {
    Serial.println(
        "FATAL: identity event queue allocation failed.");
    while (true) {
      delay(1000);
    }
  }

  uint8_t sta_mac[6] = {0};

  if (esp_read_mac(
          sta_mac,
          ESP_MAC_WIFI_STA) == ESP_OK) {
    Serial.printf(
        "XIAO STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        sta_mac[0],
        sta_mac[1],
        sta_mac[2],
        sta_mac[3],
        sta_mac[4],
        sta_mac[5]);
  } else {
    Serial.println(
        "XIAO STA MAC: unavailable");
  }

  Serial.printf(
      "Sensor ID: 0x%08lX\n",
      static_cast<unsigned long>(sensor_id));

  pinMode(PIN_FACTORY_LOW, OUTPUT);
  digitalWrite(PIN_FACTORY_LOW, LOW);

  pinMode(PIN_FACTORY_HIGH, OUTPUT);
  digitalWrite(PIN_FACTORY_HIGH, HIGH);

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  analogReadResolution(12);

  loadCalibration();
  loadIdentity();

  Measurement measurement{};

  if (!takeMeasurement(measurement)) {
    Serial.println(
        "Fatal: initial soil measurement failed.");
    sleepFor(WIFI_FAILURE_SLEEP_SECONDS);
  }

  printMeasurement(measurement);

  const bool provisioned =
      loadConfig();

  if (!provisioned) {
    if (button_service_wake) {
      provisioning_failed_windows = 0;

      Serial.println(
          "Manual wake: provisioning failure counter reset for a fresh setup cycle.");
    }

    if (provisioning_failed_windows >=
        PROVISION_MAX_FAILED_WINDOWS) {
      Serial.println(
          "Unprovisioned sensor is in battery-safe button-only state.");

      sleepUntilButtonOnly();
    }

    runProvisioningMode(measurement);
  }

  if (button_service_wake) {
    // Never immediately sleep after a top-button wake. This function stays
    // awake for two minutes of inactivity and owns the eventual sleep.
    runServiceMode(measurement);
  }

  // Scheduled timer wake:
  // Measure first, then decide whether Wi-Fi is worth turning on.
  const AdaptiveDecision decision =
      evaluateScheduledMeasurement(
          measurement);

  printAdaptiveDecision(
      decision,
      measurement);

  if (!decision.send) {
    Serial.println(
        "No meaningful change: Wi-Fi stays OFF.");
    sleepFor(
        decision.next_check_seconds);
  }

  if (!connectHomeWifi()) {
    Serial.println(
        "Report was needed, but Wi-Fi failed. "
        "Retrying local check in 5 minutes.");
    sleepFor(
        WIFI_FAILURE_SLEEP_SECONDS);
  }

  const plant::ReadingPacket reading =
      makeReadingPacket(measurement);

  uint32_t t5_requested_wake_seconds = 0;

  const bool acknowledged =
      sendReadingUdp(
          reading,
          t5_requested_wake_seconds);

  if (acknowledged) {
    rememberSuccessfulReport(
        measurement);

    Serial.printf(
        "T5 ACK requested %lu sec; "
        "adaptive sensor scheduler owns the next local check.\n",
        static_cast<unsigned long>(
            t5_requested_wake_seconds));

    sleepFor(
        decision.next_check_seconds);
  }

  Serial.println(
      "Reading was not ACKed. "
      "Retrying local check in 5 minutes.");

  sleepFor(
      WIFI_FAILURE_SLEEP_SECONDS);
}

void loop() {
  // setup() owns one wake cycle. Deep sleep restarts setup().
}
