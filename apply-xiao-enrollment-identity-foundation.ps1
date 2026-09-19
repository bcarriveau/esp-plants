Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ExpectedGitBlobSha = "808193c875cb3e63dd4f03d87ff048ef9af83e80"
$RelativePath = "firmware\xiao-soil-sensor\src\main.cpp"

function Get-GitBlobSha([string]$Text) {
    $body = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $header = [System.Text.Encoding]::ASCII.GetBytes("blob $($body.Length)`0")
    $combined = New-Object byte[] ($header.Length + $body.Length)
    [Array]::Copy($header, 0, $combined, 0, $header.Length)
    [Array]::Copy($body, 0, $combined, $header.Length, $body.Length)

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $hash = $sha1.ComputeHash($combined)
    }
    finally {
        $sha1.Dispose()
    }

    return (($hash | ForEach-Object { $_.ToString("x2") }) -join "")
}

function Replace-ExactlyOnce(
    [string]$Text,
    [string]$Old,
    [string]$New,
    [string]$Label
) {
    $first = $Text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "STOPPED: '$Label' was not found. Nothing has been written."
    }

    $second = $Text.IndexOf(
        $Old,
        $first + $Old.Length,
        [System.StringComparison]::Ordinal
    )

    if ($second -ge 0) {
        throw "STOPPED: '$Label' was found more than once. Nothing has been written."
    }

    Write-Host "  OK  $Label"
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$roots = @(
    (Get-Location).Path,
    $PSScriptRoot
) | Select-Object -Unique

$Target = $null
foreach ($root in $roots) {
    $candidate = Join-Path $root $RelativePath
    if (Test-Path -LiteralPath $candidate) {
        $Target = $candidate
        break
    }
}

if ($null -eq $Target) {
    throw @"
Could not find:
  $RelativePath

Run this script from the root of your esp-plants clone, or copy this .ps1 file
into the repository root and run it there.
"@
}

Write-Host ""
Write-Host "ESP PLANTS - XIAO enrollment identity foundation"
Write-Host "Target: $Target"
Write-Host ""

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$raw = [System.IO.File]::ReadAllText($Target, $utf8NoBom)
$hadCrLf = $raw.Contains("`r`n")
$text = $raw.Replace("`r`n", "`n")

$actualGitBlobSha = Get-GitBlobSha $text

Write-Host "GitHub audited blob: $ExpectedGitBlobSha"
Write-Host "Local normalized blob: $actualGitBlobSha"

if ($actualGitBlobSha -ne $ExpectedGitBlobSha) {
    throw @"
STOPPED BEFORE WRITING.

Your local firmware/xiao-soil-sensor/src/main.cpp is NOT the exact GitHub main.cpp
that these edits were audited against.

Expected Git blob:
  $ExpectedGitBlobSha

Found:
  $actualGitBlobSha

Do not force these edits. Update/reset this file to current GitHub main, or send
me your local file so the changes can be built from that exact copy.
"@
}

Write-Host ""
Write-Host "Source matches current GitHub main. Applying guarded edits..."

$text = Replace-ExactlyOnce $text @'
// Unprovisioned sensors stay awake long enough to be provisioned from the T5.
constexpr uint32_t PROVISION_WINDOW_MS = 120000;
constexpr uint32_t PROVISION_BEACON_INTERVAL_MS = 2500;
'@ @'
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

// Sensor-number visual language. One long green pulse means ten; each short
// green pulse means one. Example: #13 = one long + three short.
constexpr uint32_t IDENTITY_SHORT_PULSE_MS = 180;
constexpr uint32_t IDENTITY_LONG_PULSE_MS = 800;
constexpr uint32_t IDENTITY_ELEMENT_GAP_MS = 180;
'@ "add enrollment retry and LED identity timing"

$text = Replace-ExactlyOnce $text @'
SensorConfig config_data{};
Preferences preferences;

// Calibration lives in its own NVS namespace/key so adding calibration does
'@ @'
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
'@ "add separate cached T5 slot identity storage"

$text = Replace-ExactlyOnce $text @'
uint32_t sensor_id = 0;
RTC_DATA_ATTR uint32_t sequence_number = 1;

constexpr uint32_t ADAPTIVE_STATE_MAGIC = 0x41504431UL;  // "APD1"
'@ @'
uint32_t sensor_id = 0;
RTC_DATA_ATTR uint32_t sequence_number = 1;

// Number of failed provisioning attempts since the last manual reset or
// successful provisioning. RTC memory preserves it across deep-sleep retries.
RTC_DATA_ATTR uint8_t provisioning_failed_windows = 0;

constexpr uint32_t ADAPTIVE_STATE_MAGIC = 0x41504431UL;  // "APD1"
'@ "track failed provisioning attempts in RTC memory"

$text = Replace-ExactlyOnce $text @'
void serviceLedOn() {
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

bool wokeByButton() {
'@ @'
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

void pulseGreen(
    uint32_t on_ms) {
  allStatusLedsOff();
  digitalWrite(PIN_LED_GREEN, HIGH);
  delay(on_ms);
  digitalWrite(PIN_LED_GREEN, LOW);
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
    pulseGreen(IDENTITY_LONG_PULSE_MS);

    if (short_count > 0) {
      delay(IDENTITY_ELEMENT_GAP_MS);
    }
  }

  for (uint8_t i = 0;
       i < short_count;
       ++i) {
    pulseGreen(IDENTITY_SHORT_PULSE_MS);

    if (i + 1 < short_count) {
      delay(IDENTITY_ELEMENT_GAP_MS);
    }
  }

  allStatusLedsOff();
}

bool wokeByButton() {
'@ "add red-green setup indicator and long-short number encoder"

$text = Replace-ExactlyOnce $text @'
void clearConfig() {
  if (preferences.begin(CONFIG_NAMESPACE, false)) {
    preferences.remove(CONFIG_KEY);
    preferences.end();
  }

  initializeEmptyConfig();
  Serial.println("Saved home Wi-Fi erased.");
}


uint32_t calibrationChecksum(
'@ @'
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
      "Cached T5 slot: #%u (awaiting T5 authority/confirmation).\\n",
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
      "Cached T5 slot saved: #%u.\\n",
      cached_identity_slot);

  return true;
}

bool clearIdentitySlot() {
  bool removed = false;

  if (preferences.begin(
          IDENTITY_NAMESPACE,
          false)) {
    removed =
        preferences.remove(
            IDENTITY_KEY);
    preferences.end();
  }

  initializeEmptyIdentity();

  Serial.println(
      "Cached T5 slot cleared.");
  return removed;
}


uint32_t calibrationChecksum(
'@ "add identity NVS load-save-clear helpers"

$text = Replace-ExactlyOnce $text @'
  esp_deep_sleep_start();

  while (true) {
    delay(1000);
  }
}

// ============================================================================
// ESP-NOW PROVISIONING
'@ @'
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
        "WARNING: top-button wake setup failed: %s\\n",
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
      "Provisioning attempt failed: %s\\n",
      reason);
  Serial.printf(
      "Failed provisioning attempts: %u/%u.\\n",
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
      "Provisioning retry will wake in %lu minutes.\\n",
      static_cast<unsigned long>(
          retry_sleep_seconds / 60));

  sleepFor(
      retry_sleep_seconds);
}

// ============================================================================
// ESP-NOW PROVISIONING
'@ "add button-only sleep and centralized provisioning-strike handling"

$text = Replace-ExactlyOnce $text @'
  allStatusLedsOff();

  // Provisioning mode normally carries a steady green awake indicator.
  serviceLedOn();

  Serial.println(
      "LOCATE: flash complete.");
}

[[noreturn]] void runProvisioningMode(
    const Measurement& measurement) {
  serviceLedOn();
  provisioning_mode_active = true;
'@ @'
  allStatusLedsOff();

  if (provisioning_mode_active) {
    updateUnprovisionedIndicator(
        millis());
  } else {
    serviceLedOn();
  }

  Serial.println(
      "LOCATE: flash complete.");
}

[[noreturn]] void runProvisioningMode(
    const Measurement& measurement) {
  allStatusLedsOff();
  provisioning_mode_active = true;
'@ "restore red-green state after Locate and start provisioning with LEDs off"

$text = Replace-ExactlyOnce $text @'
  Serial.println();
  Serial.println(
      "=== SENSOR WI-FI PROVISIONING MODE ===");

  Serial.println(
      "Searching 2.4 GHz channels for the T5 ESP-NOW receiver. "
      "Once found, this sensor locks to that channel for Locate/provisioning.");

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println(
        "Fatal: could not start Wi-Fi STA.");
    sleepFor(WIFI_FAILURE_SLEEP_SECONDS);
  }
'@ @'
  Serial.println();
  Serial.println(
      "=== SENSOR WI-FI PROVISIONING MODE ===");

  const uint8_t attempt_number =
      static_cast<uint8_t>(
          provisioning_failed_windows + 1);

  Serial.printf(
      "Provisioning attempt %u/%u. Unprovisioned LED pattern: RED/GREEN alternating.\\n",
      attempt_number,
      PROVISION_MAX_FAILED_WINDOWS);

  Serial.println(
      "Searching 2.4 GHz channels for the T5 ESP-NOW receiver. "
      "Once found, this sensor locks to that channel for Locate/provisioning.");

  if (!WiFi.mode(WIFI_STA)) {
    finishFailedProvisioningAttempt(
        "could not start Wi-Fi STA");
  }
'@ "report attempt number and count Wi-Fi start failures as strikes"

$text = Replace-ExactlyOnce $text @'
  if (!setProvisionChannel(channel)) {
    sleepFor(WIFI_FAILURE_SLEEP_SECONDS);
  }

  if (!startEspNowCurrentChannel()) {
    sleepFor(WIFI_FAILURE_SLEEP_SECONDS);
  }
'@ @'
  if (!setProvisionChannel(channel)) {
    finishFailedProvisioningAttempt(
        "could not set initial ESP-NOW channel");
  }

  if (!startEspNowCurrentChannel()) {
    finishFailedProvisioningAttempt(
        "could not start ESP-NOW");
  }
'@ "count radio initialization failures as provisioning strikes"

$text = Replace-ExactlyOnce $text @'
  while ((millis() - start_ms) <
         PROVISION_WINDOW_MS) {
    const uint32_t locate_ms =
'@ @'
  while ((millis() - start_ms) <
         PROVISION_WINDOW_MS) {
    updateUnprovisionedIndicator(
        millis());

    const uint32_t locate_ms =
'@ "drive red-green indication non-blocking during provisioning"

$text = Replace-ExactlyOnce $text @'
      if (saveConfig(pending_config)) {
        Serial.println(
            "Provisioning complete. Rebooting into home Wi-Fi mode...");

        Serial.flush();
        delay(1200);
        ESP.restart();
      }
'@ @'
      if (saveConfig(pending_config)) {
        provisioning_failed_windows = 0;

        allStatusLedsOff();
        digitalWrite(PIN_LED_GREEN, HIGH);

        Serial.println(
            "Provisioning complete. Failed-attempt counter reset.");
        Serial.println(
            "Rebooting into home Wi-Fi mode...");

        Serial.flush();
        delay(1200);
        ESP.restart();
      }
'@ "reset strikes only after provisioning is actually saved"

$text = Replace-ExactlyOnce $text @'
  Serial.println(
      "Provisioning window expired.");

  provisioning_mode_active = false;
  sleepFor(60);
}
'@ @'
  Serial.println(
      "Provisioning window expired.");

  finishFailedProvisioningAttempt(
      "two-minute provisioning window expired");
}
'@ "replace one-minute infinite retry with three-strike lifecycle"

$text = Replace-ExactlyOnce $text @'
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, HIGH);

      clearConfig();

      Serial.println(
          "Wi-Fi erased. Restarting into provisioning mode...");
'@ @'
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, HIGH);

      provisioning_failed_windows = 0;
      clearConfig();

      Serial.println(
          "Wi-Fi erased. Provisioning failure counter reset.");
      Serial.println(
          "Restarting into provisioning mode...");
'@ "reset provisioning strikes on intentional Wi-Fi erase"

$text = Replace-ExactlyOnce $text @'
  printWakeReason();
  accountForWakeTime();

  sensor_id = createSensorId();
'@ @'
  printWakeReason();
  accountForWakeTime();

  if (provisioning_failed_windows >
      PROVISION_MAX_FAILED_WINDOWS) {
    provisioning_failed_windows = 0;
  }

  sensor_id = createSensorId();
'@ "sanitize retained provisioning counter after firmware/reset changes"

$text = Replace-ExactlyOnce $text @'
  analogReadResolution(12);

  loadCalibration();

  Measurement measurement{};
'@ @'
  analogReadResolution(12);

  loadCalibration();
  loadIdentity();

  Measurement measurement{};
'@ "load cached T5 slot separately from Wi-Fi config"

$text = Replace-ExactlyOnce $text @'
  if (!provisioned) {
    // Provisioning already uses a two-minute window. Keep the green LED on so
    // the physical behavior matches manual service mode.
    runProvisioningMode(measurement);
  }
'@ @'
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
'@ "make button wake restart setup and preserve strike-3 shelf-safe state"

# Safety assertions before the one and only write.
if ($text.Contains("sleepFor(60);")) {
    throw "STOPPED: old infinite one-minute provisioning retry remains. Nothing has been written."
}

if (-not $text.Contains("sleepUntilButtonOnly();")) {
    throw "STOPPED: button-only sleep path was not installed. Nothing has been written."
}

if (-not $text.Contains("finishFailedProvisioningAttempt(")) {
    throw "STOPPED: provisioning strike handler was not installed. Nothing has been written."
}

if (-not $text.Contains("flashSensorNumberOnce(")) {
    throw "STOPPED: number-pattern encoder was not installed. Nothing has been written."
}

if (-not $text.Contains("Cached T5 slot:")) {
    throw "STOPPED: identity NVS foundation was not installed. Nothing has been written."
}

$output = if ($hadCrLf) { $text.Replace("`n", "`r`n") } else { $text }

[System.IO.File]::WriteAllText(
    $Target,
    $output,
    $utf8NoBom
)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "Updated only:"
Write-Host "  $RelativePath"
Write-Host ""
Write-Host "Foundation behavior now:"
Write-Host "  - active unprovisioned setup alternates RED/GREEN"
Write-Host "  - failed attempt #1 -> sleep 5 minutes"
Write-Host "  - failed attempt #2 -> sleep 15 minutes"
Write-Host "  - failed attempt #3 -> timerless deep sleep, button wake only"
Write-Host "  - early Wi-Fi/ESP-NOW startup failures count as attempts too"
Write-Host "  - manual button wake starts a fresh three-attempt cycle"
Write-Host "  - intentional Wi-Fi erase starts a fresh three-attempt cycle"
Write-Host "  - cached T5 slot has its own NVS record, separate from Wi-Fi"
Write-Host "  - shared slot flasher supports short=1, long=10 (#13 = long + 3 short)"
Write-Host ""
Write-Host "NOTE: T5 assignment/confirmation is intentionally NOT wired yet."
Write-Host "The cached slot and number flasher are foundation for the next protocol/T5 step."
Write-Host ""
Write-Host "Review the GitKraken diff, then build the XIAO firmware."
