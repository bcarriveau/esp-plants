Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Relative = "firmware\t5-hub\src\main.cpp"

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

function Replace-RegexExactlyOnce(
    [string]$Text,
    [string]$Pattern,
    [string]$Replacement,
    [string]$Label
) {
    $matches = [regex]::Matches(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )

    if ($matches.Count -ne 1) {
        throw "STOPPED: '$Label' expected exactly 1 match, found $($matches.Count). Nothing has been written."
    }

    Write-Host "  OK  $Label"
    return [regex]::Replace(
        $Text,
        $Pattern,
        $Replacement,
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )
}

$roots = @(
    (Get-Location).Path,
    $PSScriptRoot
) | Select-Object -Unique

$RepoRoot = $null
foreach ($root in $roots) {
    if (Test-Path -LiteralPath (Join-Path $root $Relative)) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw "Could not find firmware\t5-hub\src\main.cpp."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Path = Join-Path $RepoRoot $Relative
$Raw = [System.IO.File]::ReadAllText($Path, $utf8NoBom)
$HadCrLf = $Raw.Contains("`r`n")
$Text = $Raw.Replace("`r`n", "`n")

Write-Host ""
Write-Host "ESP PLANTS - stop repeated automatic identity ASSIGN chatter"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# This script is deliberately for the exact post-cleanup local source already
# physically tested by the user. Do not try to apply it to GitHub main or to
# pre-cleanup source.
foreach ($needle in @(
    'first_udp_of_awake_window',
    '!wifiTransportRecent(',
    '!espNowTransportRecent(',
    'sendIdentityUdp(',
    'sendIdentityEspNow(',
    'uint16_t next_identity_request_id = 1;'
)) {
    if (-not $Text.Contains($needle)) {
        throw "STOPPED: expected current T5 marker missing: '$needle'. Nothing has been written."
    }
}

if ($Text.Contains('AUTO_IDENTITY_COOLDOWN_MS')) {
    throw "STOPPED: automatic identity cooldown already appears to be installed. Nothing has been written."
}

Write-Host "Current physically-tested cleanup source found."
Write-Host "Applying T5-only automatic identity throttle in memory..."

# ---------------------------------------------------------------------------
# 1) Add a tiny shared throttle table.
#    It is transport-neutral: one successful automatic UDP ASSIGN suppresses
#    automatic ESP-NOW ASSIGN too, and vice versa.
# ---------------------------------------------------------------------------

$Text = Replace-ExactlyOnce $Text @'
uint16_t next_identity_request_id = 1;
'@ @'
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
'@ "Add shared 3-minute automatic identity throttle"

# ---------------------------------------------------------------------------
# 2) Replace only the UDP "fresh route" inference block.
#    Keep the existing persistent slot lookup. Automatic identity is now
#    governed solely by the shared cooldown.
# ---------------------------------------------------------------------------

$udpPattern = @'
(?ms)
  const int existing_live_index =
      findLivePlant\(
          packet\.sensor_id\);

  const bool first_udp_of_awake_window =
      existing_live_index < 0 \|\|
      !wifiTransportRecent\(
          live_plants\[
              existing_live_index\]\);

  if \(persistent_index >= 0 &&
      first_udp_of_awake_window\) \{
    sendIdentityUdp\(
        source_ip,
        source_port,
        packet\.sensor_id,
        plant::IdentityCommand::Assign,
        static_cast<uint8_t>\(
            persistent_index \+ 1\)\);
  \}
'@

$udpReplacement = @'
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
'@

$Text = Replace-RegexExactlyOnce `
    $Text `
    $udpPattern `
    $udpReplacement `
    "Replace UDP fresh-route guess with shared cooldown"

# ---------------------------------------------------------------------------
# 3) Replace only the ESP-NOW "transport became active" inference block.
#    The same table means UDP + ESP-NOW cannot both spam the same sensor.
# ---------------------------------------------------------------------------

$espPattern = @'
(?ms)
  // ESP-NOW discovery/service beacons do not pass through serviceUdp\(\)\.
  // Confirm the current T5 slot only when this transport was stale before the
  // new beacon\. During a 5-second service burst, later beacons do not resend
  // ASSIGN\. Brand-new/channel-hopping sensors still get the first confirmation\.
  if \(!event\.via_udp &&
      mac != nullptr &&
      !espNowTransportRecent\(
          live\)\) \{
    sendIdentityEspNow\(
        mac,
        event\.packet\.sensor_id,
        plant::IdentityCommand::Assign,
        static_cast<uint8_t>\(
            persistent_index \+ 1\)\);
  \}
'@

$espReplacement = @'
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
'@

$Text = Replace-RegexExactlyOnce `
    $Text `
    $espPattern `
    $espReplacement `
    "Share automatic identity cooldown with ESP-NOW"

# ---------------------------------------------------------------------------
# Final in-memory validation. Still nothing has been written.
# ---------------------------------------------------------------------------

foreach ($needle in @(
    'AUTO_IDENTITY_COOLDOWN_MS',
    'automaticIdentityDue(',
    'noteAutomaticIdentitySent(',
    '3UL * 60UL * 1000UL',
    'Whichever transport confirms first suppresses the other'
)) {
    if (-not $Text.Contains($needle)) {
        throw "STOPPED: final validation missing '$needle'. Nothing has been written."
    }
}

# Only the identity-specific wake-boundary variable must disappear.
# wifiTransportRecent()/espNowTransportRecent() are legitimate helpers used
# elsewhere in the T5 and must NOT be banned globally.
if ($Text.Contains('first_udp_of_awake_window')) {
    throw "STOPPED: obsolete identity wake-boundary variable still remains. Nothing has been written."
}

# Make sure explicit pathways still exist; this cleanup must not remove them.
foreach ($needle in @(
    'sendIdentityEspNow(',
    'sendIdentityUdp(',
    'Identify Sensor #',
    'plant::IdentityCommand::Assign'
)) {
    if (-not $Text.Contains($needle)) {
        throw "STOPPED: explicit identity path marker disappeared: '$needle'. Nothing has been written."
    }
}

$Out = if ($HadCrLf) {
    $Text.Replace("`n", "`r`n")
} else {
    $Text
}

[System.IO.File]::WriteAllText(
    $Path,
    $Out,
    $utf8NoBom
)

Write-Host ""
Write-Host "SUCCESS."
Write-Host ""
Write-Host "T5 automatic identity behavior is now:"
Write-Host "  - first automatic ASSIGN for a sensor: allowed"
Write-Host "  - further automatic UDP or ESP-NOW ASSIGNs: blocked for 3 minutes"
Write-Host "  - normal 5-second service readings still receive normal UDP ACKs"
Write-Host "  - explicit Identify/provisioning identity sends are unchanged"
Write-Host ""
Write-Host "Only firmware\t5-hub\src\main.cpp was changed."
