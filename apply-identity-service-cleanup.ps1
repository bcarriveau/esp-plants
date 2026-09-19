Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$XiaoRelative = "firmware\xiao-soil-sensor\src\main.cpp"
$T5Relative   = "firmware\t5-hub\src\main.cpp"

function Replace-ExactlyOnce(
    [string]$Text,
    [string]$Old,
    [string]$New,
    [string]$Label
) {
    $first = $Text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "STOPPED: '$Label' not found. Nothing has been written."
    }

    $second = $Text.IndexOf(
        $Old,
        $first + $Old.Length,
        [System.StringComparison]::Ordinal
    )

    if ($second -ge 0) {
        throw "STOPPED: '$Label' found more than once. Nothing has been written."
    }

    Write-Host "  OK  $Label"
    return $Text.Substring(0, $first) +
           $New +
           $Text.Substring($first + $Old.Length)
}

$roots = @(
    (Get-Location).Path,
    $PSScriptRoot
) | Select-Object -Unique

$RepoRoot = $null

foreach ($root in $roots) {
    if ((Test-Path -LiteralPath (Join-Path $root $XiaoRelative)) -and
        (Test-Path -LiteralPath (Join-Path $root $T5Relative))) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw "Could not find the ESP PLANTS repository root."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$XiaoPath = Join-Path $RepoRoot $XiaoRelative
$T5Path   = Join-Path $RepoRoot $T5Relative

$XiaoRaw = [System.IO.File]::ReadAllText($XiaoPath, $utf8NoBom)
$T5Raw   = [System.IO.File]::ReadAllText($T5Path, $utf8NoBom)

$XiaoCrLf = $XiaoRaw.Contains("`r`n")
$T5CrLf   = $T5Raw.Contains("`r`n")

$Xiao = $XiaoRaw.Replace("`r`n", "`n")
$T5   = $T5Raw.Replace("`r`n", "`n")

Write-Host ""
Write-Host "ESP PLANTS - identity/service cleanup"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# Require the exact feature state reached during physical testing.
foreach ($needle in @(
    'IDENTITY_SHORT_PULSE_MS = 300',
    'IDENTITY_TO_SERVICE_PAUSE_MS = 600',
    'service timer restarted; identifying then sending fresh reading.',
    'T5 authority confirmed: Sensor #'
)) {
    if (-not $Xiao.Contains($needle)) {
        throw "STOPPED: XIAO expected tested marker missing: '$needle'. Nothing has been written."
    }
}

foreach ($needle in @(
    'IDENTITY_CONFIRM_INTERVAL_MS = 5000',
    'last_identity_sent_ms',
    'sendIdentityUdp(',
    'sendIdentityEspNow('
)) {
    if (-not $T5.Contains($needle)) {
        throw "STOPPED: T5 expected identity marker missing: '$needle'. Nothing has been written."
    }
}

Write-Host "Tested source markers found. Applying exact cleanup edits in memory..."

# ===========================================================================
# XIAO CLEANUP
# ===========================================================================

# Real STA MAC without bringing Wi-Fi up early.
$Xiao = Replace-ExactlyOnce $Xiao @'
#include <esp_wifi.h>
#include <esp_sleep.h>

#include <atomic>
'@ @'
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_mac.h>

#include <atomic>
'@ "XIAO include esp_mac.h"

# Fix the one remaining literal backslash-n diagnostic.
$Xiao = Replace-ExactlyOnce $Xiao @'
  Serial.printf(
      "Cached T5 slot: #%u (awaiting T5 authority/confirmation).\\n",
      cached_identity_slot);
'@ @'
  Serial.printf(
      "Cached T5 slot: #%u (awaiting T5 authority/confirmation).\n",
      cached_identity_slot);
'@ "XIAO cached-slot serial newline"

# Replace the early WiFi.macAddress() diagnostic with the hardware STA MAC.
$Xiao = Replace-ExactlyOnce $Xiao @'
  Serial.print("XIAO MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.printf(
      "Sensor ID: 0x%08lX\n",
'@ @'
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
'@ "XIAO print real hardware STA MAC"

# Do not let the 5-second auto sampler fire while waiting to distinguish a
# single/two-click action from the existing triple-click calibration command.
$Xiao = Replace-ExactlyOnce $Xiao @'
    if (wifi_connected &&
        (millis() - last_service_sample_ms) >=
            SERVICE_SAMPLE_INTERVAL_MS) {
'@ @'
    if (wifi_connected &&
        short_click_count == 0 &&
        (millis() - last_service_sample_ms) >=
            SERVICE_SAMPLE_INTERVAL_MS) {
'@ "XIAO pause auto-sample during multi-click decision window"

# ===========================================================================
# T5 CLEANUP
# ===========================================================================

# The time-based identity throttle matched the 5-second service sample cadence
# and therefore sent ASSIGN on almost every report. Wake-boundary freshness is
# a better fit, so remove the obsolete interval/field.
$T5 = Replace-ExactlyOnce $T5 @'
constexpr uint32_t WIFI_SENSOR_RECENT_MS = 12000;
constexpr uint32_t ESPNOW_SENSOR_RECENT_MS = 8000;
constexpr uint32_t IDENTITY_CONFIRM_INTERVAL_MS = 5000;
'@ @'
constexpr uint32_t WIFI_SENSOR_RECENT_MS = 12000;
constexpr uint32_t ESPNOW_SENSOR_RECENT_MS = 8000;
'@ "T5 remove 5-second identity resend interval"

$T5 = Replace-ExactlyOnce $T5 @'
  uint32_t last_seen_ms;
  uint32_t last_udp_seen_ms;
  uint32_t last_espnow_seen_ms;
  uint32_t last_identity_sent_ms;
  IPAddress source_ip;
};
'@ @'
  uint32_t last_seen_ms;
  uint32_t last_udp_seen_ms;
  uint32_t last_espnow_seen_ms;
  IPAddress source_ip;
};
'@ "T5 remove obsolete identity resend timestamp"

# UDP: confirm identity only on the first packet after the Wi-Fi route has gone
# stale (i.e. the start of a new awake window), not every 5-second service read.
$T5 = Replace-ExactlyOnce $T5 @'
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
'@ @'
  const int persistent_index =
      ensurePersistedPlant(
          packet.sensor_id,
          nullptr);

  const int existing_live_index =
      findLivePlant(
          packet.sensor_id);

  const bool first_udp_of_awake_window =
      existing_live_index < 0 ||
      !wifiTransportRecent(
          live_plants[
              existing_live_index]);

  if (persistent_index >= 0 &&
      first_udp_of_awake_window) {
    sendIdentityUdp(
        source_ip,
        source_port,
        packet.sensor_id,
        plant::IdentityCommand::Assign,
        static_cast<uint8_t>(
            persistent_index + 1));
  }

  queueReceivedReading(
'@ "T5 send UDP identity once per awake-window route"

# ESP-NOW fallback: same rule. At this point last_espnow_seen_ms still contains
# the previous beacon timestamp; it is updated later in processReceivedEvent().
$T5 = Replace-ExactlyOnce $T5 @'
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
'@ @'
  // ESP-NOW discovery/service beacons do not pass through serviceUdp().
  // Confirm the current T5 slot only when this transport was stale before the
  // new beacon. During a 5-second service burst, later beacons do not resend
  // ASSIGN. Brand-new/channel-hopping sensors still get the first confirmation.
  if (!event.via_udp &&
      mac != nullptr &&
      !espNowTransportRecent(
          live)) {
    sendIdentityEspNow(
        mac,
        event.packet.sensor_id,
        plant::IdentityCommand::Assign,
        static_cast<uint8_t>(
            persistent_index + 1));
  }
'@ "T5 send ESP-NOW identity once when transport becomes active"

# ===========================================================================
# FINAL VALIDATION - still no files written at this point.
# ===========================================================================

foreach ($needle in @(
    '#include <esp_mac.h>',
    'XIAO STA MAC: %02X:%02X:%02X:%02X:%02X:%02X',
    'short_click_count == 0',
    'awaiting T5 authority/confirmation).\n"'
)) {
    if (-not $Xiao.Contains($needle)) {
        throw "STOPPED: final XIAO validation missing '$needle'. Nothing has been written."
    }
}

if ($Xiao.Contains('awaiting T5 authority/confirmation).\\n"')) {
    throw "STOPPED: literal cached-slot backslash-n remains. Nothing has been written."
}

foreach ($needle in @(
    'first_udp_of_awake_window',
    '!wifiTransportRecent(',
    '!espNowTransportRecent('
)) {
    if (-not $T5.Contains($needle)) {
        throw "STOPPED: final T5 validation missing '$needle'. Nothing has been written."
    }
}

if ($T5.Contains('IDENTITY_CONFIRM_INTERVAL_MS') -or
    $T5.Contains('last_identity_sent_ms')) {
    throw "STOPPED: obsolete T5 identity resend throttle remains. Nothing has been written."
}

# Only now write both files.
$XiaoOut = if ($XiaoCrLf) {
    $Xiao.Replace("`n", "`r`n")
} else {
    $Xiao
}

$T5Out = if ($T5CrLf) {
    $T5.Replace("`n", "`r`n")
} else {
    $T5
}

[System.IO.File]::WriteAllText(
    $XiaoPath,
    $XiaoOut,
    $utf8NoBom
)

[System.IO.File]::WriteAllText(
    $T5Path,
    $T5Out,
    $utf8NoBom
)

Write-Host ""
Write-Host "SUCCESS."
Write-Host ""
Write-Host "Cleanup applied:"
Write-Host "  - T5 no longer sends ASSIGN on every 5-second service reading"
Write-Host "  - first fresh UDP/ESP-NOW route of an awake window still confirms identity"
Write-Host "  - explicit Identify still sends ASSIGN immediately before Locate"
Write-Host "  - XIAO cached-slot diagnostic newline fixed"
Write-Host "  - XIAO prints the real hardware STA MAC"
Write-Host "  - service auto-sampling pauses while single/triple-click is undecided"
Write-Host ""
Write-Host "No protocol layouts, calibration logic, sensor excitation, sleep policy,"
Write-Host "or T5 PMU/shutdown code were changed."
