Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Expected = @{
    "firmware\t5-hub\src\main.cpp" = "db4d5ead072caccdc4df3a5ce119554748e3cc4e"
    "firmware\xiao-soil-sensor\src\main.cpp" = "9ff608aa2fd5413505f2bb6c28c0c9ffd5c6e08c"
    "firmware\t5-hub\include\plant_protocol.h" = "fe02130c1cf606d7e2c9c03b9a773ca230755c1c"
    "firmware\xiao-soil-sensor\include\plant_protocol.h" = "fe02130c1cf606d7e2c9c03b9a773ca230755c1c"
}

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

$RepoRoot = $null

foreach ($root in $roots) {
    $probe = Join-Path $root "firmware\t5-hub\src\main.cpp"
    if (Test-Path -LiteralPath $probe) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw @"
Could not find the ESP PLANTS repository root.

Run this script from the root of your esp-plants clone, or copy this .ps1 file
into that root and run it there.
"@
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Files = @{}

Write-Host ""
Write-Host "ESP PLANTS - T5/XIAO authoritative identity protocol"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# Guard EVERY file before changing anything in memory.
foreach ($relative in $Expected.Keys) {
    $path = Join-Path $RepoRoot $relative

    if (-not (Test-Path -LiteralPath $path)) {
        throw "STOPPED: missing $relative. Nothing has been written."
    }

    $raw = [System.IO.File]::ReadAllText($path, $utf8NoBom)
    $hadCrLf = $raw.Contains("`r`n")
    $lf = $raw.Replace("`r`n", "`n")
    $actual = Get-GitBlobSha $lf
    $wanted = $Expected[$relative]

    Write-Host "$relative"
    Write-Host "  GitHub audited blob: $wanted"
    Write-Host "  Local normalized blob: $actual"

    if ($actual -ne $wanted) {
        throw @"
STOPPED BEFORE WRITING.

$relative does not match the exact GitHub main source audited for this change.

Expected:
  $wanted
Found:
  $actual

Do not force this script. Push/sync the intended source first or send the exact
local files so the edit can be rebuilt against them.
"@
    }

    $Files[$relative] = @{
        Text = $lf
        HadCrLf = $hadCrLf
        Path = $path
    }
}

Write-Host ""
Write-Host "All four files match GitHub main. Applying guarded edits in memory..."

# ---------------------------------------------------------------------------
# 1) PROTOCOL - apply the exact same additive packet definitions to both copies.
# ---------------------------------------------------------------------------

$protocolOld = @'
// Identification command used by either setup-mode ESP-NOW or the normal
// home-Wi-Fi UDP path. It does not alter provisioning, calibration, or readings.
struct __attribute__((packed)) LocatePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint16_t flash_ms;
  uint32_t checksum;
};

inline uint32_t fnv1a(const uint8_t* data, size_t length) {
'@

$protocolNew = @'
// Identification command used by either setup-mode ESP-NOW or the normal
// home-Wi-Fi UDP path. It does not alter provisioning, calibration, or readings.
struct __attribute__((packed)) LocatePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint16_t flash_ms;
  uint32_t checksum;
};

// T5-authoritative physical slot identity. This is additive protocol-v3
// traffic; the existing Reading/Ack/Provision/Locate packet layouts remain
// unchanged. Slot 0 is used only with Clear; Assign accepts slots 1..16.
enum class IdentityCommand : uint8_t {
  Assign = 1,
  Clear = 2
};

struct __attribute__((packed)) IdentityPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  IdentityCommand command;
  uint8_t slot;
  uint16_t request_id;
  uint32_t checksum;
};

struct __attribute__((packed)) IdentityAckPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint8_t slot;
  uint8_t accepted;
  uint16_t request_id;
  uint32_t checksum;
};

inline uint32_t fnv1a(const uint8_t* data, size_t length) {
'@

$assertOld = @'
static_assert(
    sizeof(ProvisionPacket) <= 255,
    "ProvisionPacket packet_size field is only 8 bits.");

static_assert(
    sizeof(LocatePacket) <= 255,
    "LocatePacket packet_size field is only 8 bits.");

}  // namespace plant
'@

$assertNew = @'
static_assert(sizeof(ReadingPacket) == 30, "ReadingPacket layout changed.");
static_assert(sizeof(AckPacket) == 23, "AckPacket layout changed.");
static_assert(sizeof(ProvisionPacket) == 114, "ProvisionPacket layout changed.");
static_assert(sizeof(ProvisionAckPacket) == 15, "ProvisionAckPacket layout changed.");
static_assert(sizeof(LocatePacket) == 16, "LocatePacket layout changed.");
static_assert(sizeof(IdentityPacket) == 18, "IdentityPacket layout changed.");
static_assert(sizeof(IdentityAckPacket) == 18, "IdentityAckPacket layout changed.");

static_assert(
    sizeof(ProvisionPacket) <= 255,
    "ProvisionPacket packet_size field is only 8 bits.");

}  // namespace plant
'@

foreach ($relative in @(
    "firmware\t5-hub\include\plant_protocol.h",
    "firmware\xiao-soil-sensor\include\plant_protocol.h"
)) {
    $text = [string]$Files[$relative].Text
    $text = Replace-ExactlyOnce $text $protocolOld $protocolNew "$relative - add IdentityPacket/IdentityAckPacket"
    $text = Replace-ExactlyOnce $text $assertOld $assertNew "$relative - lock protocol packet sizes"
    $Files[$relative].Text = $text
}

# ---------------------------------------------------------------------------
# 2) T5 - authoritative slot number, identity transport, ACK capture, UI.
# ---------------------------------------------------------------------------

$t5Key = "firmware\t5-hub\src\main.cpp"
$t5 = [string]$Files[$t5Key].Text

$t5 = Replace-ExactlyOnce $t5 @'
constexpr uint32_t WIFI_SENSOR_RECENT_MS = 12000;
constexpr uint32_t ESPNOW_SENSOR_RECENT_MS = 8000;

// Wi-Fi and ESP-NOW share the same 2.4 GHz radio/channel. The T5 keeps
'@ @'
constexpr uint32_t WIFI_SENSOR_RECENT_MS = 12000;
constexpr uint32_t ESPNOW_SENSOR_RECENT_MS = 8000;
constexpr uint32_t IDENTITY_CONFIRM_INTERVAL_MS = 5000;

// Wi-Fi and ESP-NOW share the same 2.4 GHz radio/channel. The T5 keeps
'@ "T5 - add identity confirmation throttle"

$t5 = Replace-ExactlyOnce $t5 @'
  uint32_t last_seen_ms;
  uint32_t last_udp_seen_ms;
  uint32_t last_espnow_seen_ms;
  IPAddress source_ip;
};
'@ @'
  uint32_t last_seen_ms;
  uint32_t last_udp_seen_ms;
  uint32_t last_espnow_seen_ms;
  uint32_t last_identity_sent_ms;
  IPAddress source_ip;
};
'@ "T5 - track last identity confirmation per live sensor"

$t5 = Replace-ExactlyOnce $t5 @'
// Last application-level provisioning acknowledgement crosses the ESP-NOW
// callback/task boundary, so use real C++ atomics rather than volatile.
std::atomic<uint32_t> provision_ack_sensor_id{0};
std::atomic<bool> provision_ack_received{false};

FrontlightLevel frontlight_level =
'@ @'
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

FrontlightLevel frontlight_level =
'@ "T5 - add identity ACK state"

$t5 = Replace-ExactlyOnce $t5 @'
int findLivePlant(uint32_t sensor_id) {
'@ @'
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
'@ "T5 - expose persistent array slot as authoritative #1-#16"

$t5 = Replace-ExactlyOnce $t5 @'
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

// ============================================================================
// UDP NORMAL TRANSPORT
// ============================================================================
'@ @'
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
'@ "T5 - add identity packet construction and ESP-NOW sender"

$t5 = Replace-ExactlyOnce $t5 @'
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

void sendUdpAck(
'@ @'
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
'@ "T5 - add identity UDP sender"

$t5 = Replace-ExactlyOnce $t5 @'
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
'@ @'
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
'@ "T5 - accept IdentityAckPacket on UDP listener"

$t5 = Replace-ExactlyOnce $t5 @'
  Serial.print("From IP: ");
  Serial.println(source_ip);

  queueReceivedReading(
      nullptr,
      packet,
      source_ip,
      true);

  sendUdpAck(
'@ @'
  Serial.print("From IP: ");
  Serial.println(source_ip);

  const int persistent_index =
      ensurePersistedPlant(
          packet.sensor_id,
          nullptr);

  if (persistent_index >= 0) {
    sendIdentityUdp(
        source_ip,
        source_port,
        packet.sensor_id,
        plant::IdentityCommand::Assign,
        static_cast<uint8_t>(
            persistent_index + 1));
  }

  queueReceivedReading(
      nullptr,
      packet,
      source_ip,
      true);

  sendUdpAck(
'@ "T5 - confirm authoritative slot before normal UDP ACK"

$t5 = Replace-ExactlyOnce $t5 @'
  if (length ==
      sizeof(plant::ProvisionAckPacket)) {
    plant::ProvisionAckPacket ack{};
'@ @'
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
'@ "T5 - accept ESP-NOW IdentityAckPacket"

$t5 = Replace-ExactlyOnce $t5 @'
  Serial.printf(
      "Provisioning sensor 0x%08lX on coexistence channel %u...\n",
      static_cast<unsigned long>(
          sensor_id),
      currentRadioChannel());

  // The unprovisioned XIAO locks onto this channel after hearing the automatic
'@ @'
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
'@ "T5 - assign slot before sending Wi-Fi provisioning packet"

$t5 = Replace-ExactlyOnce $t5 @'
  const PersistedPlant& record =
      config_data.plants[persistent_index];

  const LivePlant& live =
      live_plants[live_index];

  plant::LocatePacket packet{};
'@ @'
  const PersistedPlant& record =
      config_data.plants[persistent_index];

  const LivePlant& live =
      live_plants[live_index];

  const uint8_t authoritative_slot =
      static_cast<uint8_t>(
          persistent_index + 1);

  plant::LocatePacket packet{};
'@ "T5 - resolve authoritative slot for Locate"

$t5 = Replace-ExactlyOnce $t5 @'
  if (esp_now_ready &&
      plantMacKnown(record.mac) &&
      espNowTransportRecent(live)) {
    if (!ensureEspNowPeer(record.mac))
      return false;

    const esp_err_t result =
        esp_now_send(
'@ @'
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
'@ "T5 - confirm slot immediately before ESP-NOW Locate"

$t5 = Replace-ExactlyOnce $t5 @'
  if (!udp.beginPacket(
          live.source_ip,
          plant::SENSOR_UDP_PORT)) {
    Serial.println(
        "Locate UDP beginPacket failed.");
    return false;
  }

  const size_t written =
'@ @'
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
'@ "T5 - confirm slot immediately before UDP Locate"

$t5 = Replace-ExactlyOnce $t5 @'
  if (persistent_index < 0 ||
      live_index < 0) {
    return;
  }

  LivePlant& live =
      live_plants[live_index];

  live.packet = event.packet;
'@ @'
  if (persistent_index < 0 ||
      live_index < 0) {
    return;
  }

  LivePlant& live =
      live_plants[live_index];

  live.packet = event.packet;

  // ESP-NOW discovery/provisioning beacons do not pass through serviceUdp().
  // Confirm the current T5 slot here, throttled so a channel-hopping sensor
  // does not receive an identity packet on every 320 ms beacon.
  if (!event.via_udp &&
      mac != nullptr &&
      (live.last_identity_sent_ms == 0 ||
       (millis() - live.last_identity_sent_ms) >=
           IDENTITY_CONFIRM_INTERVAL_MS)) {
    if (sendIdentityEspNow(
            mac,
            event.packet.sensor_id,
            plant::IdentityCommand::Assign,
            static_cast<uint8_t>(
                persistent_index + 1))) {
      live.last_identity_sent_ms =
          millis();
    }
  }
'@ "T5 - confirm slot on ESP-NOW discovery/service beacons"

$t5 = Replace-ExactlyOnce $t5 @'
  display.drawString(
      String("ID: ") +
          sensorIdToHex(packet.sensor_id),
      w / 2, h - 80);
'@ @'
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
'@ "T5 - show authoritative slot on normal receiver display"

$t5 = Replace-ExactlyOnce $t5 @'
    html += F("<div class='card sub'><h2>");
    html += htmlEscape(String(record.name));
    html += F("</h2><div class='muted'><code>0x");
'@ @'
    html += F("<div class='card sub'><h2>#");
    html += String(i + 1);
    html += F(" &mdash; ");
    html += htmlEscape(String(record.name));
    html += F("</h2><div class='muted'><strong>T5 slot #");
    html += String(i + 1);
    html += F("</strong><br><code>0x");
'@ "T5 - make slot number prominent on setup page"

$t5 = Replace-ExactlyOnce $t5 @'
      html += F("'><button type='submit'>Flash / Locate Sensor</button></form><p class='muted'>");
'@ @'
      html += F("'><button type='submit'>Identify Sensor #");
      html += String(i + 1);
      html += F("</button></form><p class='muted'>");
'@ "T5 - label Locate with authoritative sensor number"

$t5 = Replace-ExactlyOnce $t5 @'
      html += F("'><button type='submit'>Send Wi-Fi to Sensor</button></form><p class='muted'>Provisioning uses the same current-channel ESP-NOW connection; the T5 does not leave home Wi-Fi.</p>");
'@ @'
      html += F("'><button type='submit'>Provision Sensor #");
      html += String(i + 1);
      html += F("</button></form><p class='muted'>Provisioning sends this T5 slot identity first, then the unchanged Wi-Fi ProvisionPacket on the same current-channel ESP-NOW connection.</p>");
'@ "T5 - label provisioning with authoritative sensor number"

$t5 = Replace-ExactlyOnce $t5 @'
        " Watch the selected sensor for rapid LED flashes for about 8 seconds.</p>";
'@ @'
        " Watch the selected sensor repeat its numbered green identity pattern for about 8 seconds. Long = 10; short = 1.</p>";
'@ "T5 - describe numbered Identify result"

$Files[$t5Key].Text = $t5

# ---------------------------------------------------------------------------
# 3) XIAO - persist/apply authoritative identity, ACK, numbered wake/Locate.
# ---------------------------------------------------------------------------

$xiaoKey = "firmware\xiao-soil-sensor\src\main.cpp"
$xiao = [string]$Files[$xiaoKey].Text

# Fix literal "\\n" strings introduced by the previous guarded foundation
# script. They compiled but printed backslash-n instead of a newline.
$xiao = Replace-ExactlyOnce $xiao '      "Cached T5 slot saved: #%u.\\n",' '      "Cached T5 slot saved: #%u.\n",' "XIAO - fix identity save serial newline"
$xiao = Replace-ExactlyOnce $xiao '        "WARNING: top-button wake setup failed: %s\\n",' '        "WARNING: top-button wake setup failed: %s\n",' "XIAO - fix button-only wake warning newline"
$xiao = Replace-ExactlyOnce $xiao '      "Provisioning attempt failed: %s\\n",' '      "Provisioning attempt failed: %s\n",' "XIAO - fix failed provisioning reason newline"
$xiao = Replace-ExactlyOnce $xiao '      "Failed provisioning attempts: %u/%u.\\n",' '      "Failed provisioning attempts: %u/%u.\n",' "XIAO - fix failed provisioning count newline"
$xiao = Replace-ExactlyOnce $xiao '      "Provisioning retry will wake in %lu minutes.\\n",' '      "Provisioning retry will wake in %lu minutes.\n",' "XIAO - fix retry serial newline"
$xiao = Replace-ExactlyOnce $xiao '      "Provisioning attempt %u/%u. Unprovisioned LED pattern: RED/GREEN alternating.\\n",' '      "Provisioning attempt %u/%u. Unprovisioned LED pattern: RED/GREEN alternating.\n",' "XIAO - fix provisioning attempt serial newline"

$xiao = Replace-ExactlyOnce $xiao @'
std::atomic<bool> provision_received{false};
std::atomic<uint32_t> locate_request_ms{0};
SensorConfig pending_config{};

struct AdcReading {
'@ @'
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
'@ "XIAO - add queued ESP-NOW identity events"

$xiao = Replace-ExactlyOnce $xiao @'
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
'@ @'
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
'@ "XIAO - apply authoritative Assign/Clear to cached identity NVS"

$xiao = Replace-ExactlyOnce $xiao @'
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

uint8_t currentRadioChannel();
'@ @'
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
'@ "XIAO - queue/apply ESP-NOW identity and send persistence ACK"

$xiao = Replace-ExactlyOnce $xiao @'
void handleEspNowPacket(
    const uint8_t* source_mac,
    const uint8_t* data,
    int length) {
  if (acceptLocatePacket(
          data,
          length,
          "ESP-NOW")) {
    return;
  }

  if (length ==
      sizeof(plant::AckPacket)) {
'@ @'
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
'@ "XIAO - accept ESP-NOW IdentityPacket before Locate/Ack"

$xiao = Replace-ExactlyOnce $xiao @'
  while ((millis() - start_ms) <
         PROVISION_WINDOW_MS) {
    updateUnprovisionedIndicator(
        millis());

    const uint32_t locate_ms =
'@ @'
  while ((millis() - start_ms) <
         PROVISION_WINDOW_MS) {
    updateUnprovisionedIndicator(
        millis());

    // ESP-NOW callbacks only queue identity requests. NVS writes and the
    // application-level ACK happen here on the normal Arduino task.
    servicePendingEspNowIdentity();

    const uint32_t locate_ms =
'@ "XIAO - service identity queue during unprovisioned channel search"

$xiao = Replace-ExactlyOnce $xiao @'
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
'@ @'
      if (saveConfig(pending_config)) {
        provisioning_failed_windows = 0;

        // Drain any slot assignment that arrived immediately before the
        // ProvisionPacket so the number survives this reboot too.
        servicePendingEspNowIdentity();

        if (identity_confirmed_this_wake &&
            cached_identity_slot > 0) {
          Serial.printf(
              "Provisioned as Sensor #%u; showing physical identity once.\n",
              cached_identity_slot);

          flashSensorNumberOnce(
              cached_identity_slot);
          delay(250);
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
'@ "XIAO - persist/display confirmed slot before provisioning reboot"

$xiao = Replace-ExactlyOnce $xiao @'
bool readUdpAck(
    uint32_t expected_sequence,
    uint32_t& next_wake_seconds) {
'@ @'
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
'@ "XIAO - add UDP identity apply/ACK helper"

$xiao = Replace-ExactlyOnce $xiao @'
    if (packet_size > 0) {
      if (packet_size ==
          sizeof(plant::AckPacket)) {
'@ @'
    if (packet_size > 0) {
      if (handleIdentityUdpPacket(
              packet_size)) {
        // Identity confirmation deliberately precedes the normal reading ACK.
        // Keep waiting for the matching AckPacket.
      } else if (packet_size ==
                 sizeof(plant::AckPacket)) {
'@ "XIAO - process T5 identity before matching normal UDP ACK"

$xiao = Replace-ExactlyOnce $xiao @'
  while (packet_size > 0) {
    if (packet_size ==
        sizeof(plant::LocatePacket)) {
'@ @'
  while (packet_size > 0) {
    if (handleIdentityUdpPacket(
            packet_size)) {
      // Continue draining queued service commands.
    } else if (packet_size ==
               sizeof(plant::LocatePacket)) {
'@ "XIAO - process identity on service UDP command path"

$xiao = Replace-ExactlyOnce $xiao @'
void runLocateFlash(
    uint32_t duration_ms) {
  Serial.printf(
      "LOCATE: flashing all status LEDs for %lu ms.\n",
      static_cast<unsigned long>(
          duration_ms));

  const uint32_t started =
      millis();

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
'@ @'
void runLocateFlash(
    uint32_t duration_ms) {
  const uint32_t started =
      millis();

  if (identity_confirmed_this_wake &&
      cached_identity_slot >= 1 &&
      cached_identity_slot <=
          MAX_IDENTITY_SLOT) {
    Serial.printf(
        "IDENTIFY: Sensor #%u, long=10 short=1, repeating for %lu ms.\n",
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
'@ "XIAO - replace generic Locate with T5-confirmed numbered identity pattern"

$xiao = Replace-ExactlyOnce $xiao @'
  Serial.println(
      "Wake button released; 2-minute service timer started.");

  service_mode_active = true;

  bool wifi_connected =
'@ @'
  Serial.println(
      "Wake button released; 2-minute service timer started.");

  // The initial green light proved the button wake. Turn it off while the T5
  // confirms identity; once confirmed the number pattern plays before solid.
  allStatusLedsOff();
  identity_confirmed_this_wake = false;

  service_mode_active = true;

  bool wifi_connected =
'@ "XIAO - require current T5 confirmation before numbered service wake"

$xiao = Replace-ExactlyOnce $xiao @'
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
  } else {
    Serial.println(
        "Home Wi-Fi unavailable. "
        "Service mode will stay awake and retry.");
  }

  uint32_t last_activity_ms =
'@ @'
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
          "Button wake confirmed by T5 as Sensor #%u.\n",
          cached_identity_slot);

      flashSensorNumberOnce(
          cached_identity_slot);
      identity_number_shown = true;
      delay(250);
    }
  } else {
    Serial.println(
        "Home Wi-Fi unavailable. "
        "Service mode will stay awake and retry.");
  }

  serviceLedOn();

  uint32_t last_activity_ms =
'@ "XIAO - show confirmed number once before solid-green service mode"

$xiao = Replace-ExactlyOnce $xiao @'
    // Accept Locate commands over the home network while the provisioned
    // sensor is deliberately awake in service mode. A Locate received during
    // the blocking ACK wait is queued by readUdpAck() and handled here too.
    serviceUdpLocateCommands();

    const uint32_t locate_ms =
'@ @'
    // Accept identity/Locate commands over both transports while the
    // provisioned sensor is deliberately awake.
    servicePendingEspNowIdentity();
    serviceUdpLocateCommands();

    if (!identity_number_shown &&
        identity_confirmed_this_wake &&
        cached_identity_slot > 0) {
      Serial.printf(
          "Late T5 identity confirmation: Sensor #%u.\n",
          cached_identity_slot);

      flashSensorNumberOnce(
          cached_identity_slot);
      identity_number_shown = true;
      serviceLedOn();
      last_activity_ms = millis();
    }

    const uint32_t locate_ms =
'@ "XIAO - service late identity confirmations during two-minute wake"

$xiao = Replace-ExactlyOnce $xiao @'
  sensor_id = createSensorId();

  Serial.print("XIAO MAC: ");
'@ @'
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

  Serial.print("XIAO MAC: ");
'@ "XIAO - allocate small identity event queue before networking"

$Files[$xiaoKey].Text = $xiao

# ---------------------------------------------------------------------------
# 4) Cross-file safety checks BEFORE any writes.
# ---------------------------------------------------------------------------

$t5Final = [string]$Files[$t5Key].Text
$xiaoFinal = [string]$Files[$xiaoKey].Text
$t5Proto = [string]$Files["firmware\t5-hub\include\plant_protocol.h"].Text
$xiaoProto = [string]$Files["firmware\xiao-soil-sensor\include\plant_protocol.h"].Text

if ($t5Proto -ne $xiaoProto) {
    throw "STOPPED: protocol headers diverged in memory. Nothing has been written."
}

foreach ($needle in @(
    "struct __attribute__((packed)) IdentityPacket",
    "struct __attribute__((packed)) IdentityAckPacket",
    "sizeof(ReadingPacket) == 30",
    "sizeof(IdentityPacket) == 18"
)) {
    if (-not $t5Proto.Contains($needle)) {
        throw "STOPPED: protocol safety check missing '$needle'. Nothing has been written."
    }
}

foreach ($needle in @(
    "Identity ACK via ESP-NOW",
    "Identity ACK via UDP",
    "slotNumberForSensor",
    "Identify Sensor #",
    "sendIdentityEspNow",
    "sendIdentityUdp"
)) {
    if (-not $t5Final.Contains($needle)) {
        throw "STOPPED: T5 safety check missing '$needle'. Nothing has been written."
    }
}

foreach ($needle in @(
    "T5 authority confirmed: Sensor #",
    "identity_confirmed_this_wake",
    "servicePendingEspNowIdentity",
    "handleIdentityUdpPacket",
    "long=10 short=1",
    "xQueueCreate"
)) {
    if (-not $xiaoFinal.Contains($needle)) {
        throw "STOPPED: XIAO safety check missing '$needle'. Nothing has been written."
    }
}

foreach ($packetName in @(
    "ReadingPacket",
    "AckPacket",
    "ProvisionPacket",
    "ProvisionAckPacket",
    "LocatePacket",
    "IdentityPacket",
    "IdentityAckPacket"
)) {
    $marker = "struct __attribute__((packed)) $packetName"
    $count = ([regex]::Matches(
        $t5Proto,
        [regex]::Escape($marker))).Count

    if ($count -ne 1) {
        throw "STOPPED: protocol contains $count declarations of $packetName. Nothing has been written."
    }
}

# ---------------------------------------------------------------------------
# 5) All guards passed. Now write all four files, preserving newline style.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "All edit guards passed. Writing four files..."

foreach ($relative in $Expected.Keys) {
    $entry = $Files[$relative]
    $text = [string]$entry.Text
    $output = if ($entry.HadCrLf) {
        $text.Replace("`n", "`r`n")
    } else {
        $text
    }

    [System.IO.File]::WriteAllText(
        [string]$entry.Path,
        $output,
        $utf8NoBom
    )

    Write-Host "  WROTE $relative"
}

Write-Host ""
Write-Host "SUCCESS."
Write-Host ""
Write-Host "This chunk now provides:"
Write-Host "  - additive protocol-v3 IdentityPacket + IdentityAckPacket"
Write-Host "  - compile-time locks on all packet sizes"
Write-Host "  - T5 persistent array slot = authoritative Sensor #1..#16"
Write-Host "  - T5 sends ASSIGN #n before UDP ACK, provisioning, and Locate"
Write-Host "  - XIAO persists the slot separately from Wi-Fi and ACKs after applying it"
Write-Host "  - button wake: confirmed number pattern once, then solid green"
Write-Host "  - Identify: repeat numbered green pattern; long=10, short=1"
Write-Host "  - stale/unconfirmed number is never used for numbered Locate"
Write-Host "  - setup page and normal T5 display show the authoritative slot number"
Write-Host ""
Write-Host "NOT in this chunk:"
Write-Host "  - Replace/Assign-to-specific-slot web UI"
Write-Host "  - removal/tombstone lifecycle for an old replaced sensor"
Write-Host ""
Write-Host "Next: build BOTH firmware targets before flashing."
