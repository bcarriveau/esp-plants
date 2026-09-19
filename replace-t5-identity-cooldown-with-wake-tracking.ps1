Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Relative = "firmware\t5-hub\src\main.cpp"
$Path = Join-Path (Get-Location) $Relative

if (-not (Test-Path -LiteralPath $Path)) {
    throw "Could not find $Relative from the current folder."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$raw = [System.IO.File]::ReadAllText($Path, $utf8)
$hadCrLf = $raw.Contains("`r`n")
$text = $raw.Replace("`r`n", "`n")

# This replaces the previously tested 3-minute automatic identity throttle.
if (-not $text.Contains("AUTO_IDENTITY_COOLDOWN_MS")) {
    throw "STOPPED: current 3-minute identity throttle was not found. Nothing has been written."
}

$statePattern = '(?ms)// Automatic identity confirmation is useful once when a sensor becomes active,.*?(?=uint16_t allocateIdentityRequestId\(\) \{)'
$stateMatches = [regex]::Matches($text, $statePattern)

if ($stateMatches.Count -ne 1) {
    throw "STOPPED: expected exactly one automatic identity throttle block, found $($stateMatches.Count). Nothing has been written."
}

$newState = @'
// Automatic ASSIGN is scoped to an actual sensor wake, not to an arbitrary
// wall-clock cooldown. ReadingPacket.awake_ms comes from the XIAO's millis(),
// so it restarts on each deep-sleep/reset wake. A long receive gap also marks a
// new wake, which covers short scheduled wakes whose first awake_ms could be
// slightly larger than the previous wake's final value.
//
// The 30-second gap is comfortably longer than the normal 5-second service
// cadence, the 8-second Identify/Locate flash, and ordinary bounded UDP retries.
constexpr uint32_t AUTO_IDENTITY_NEW_WAKE_GAP_MS = 30000;

struct AutoIdentityWakeState {
  uint32_t sensor_id = 0;
  uint32_t last_awake_ms = 0;
  uint32_t last_t5_seen_ms = 0;
  bool have_awake = false;
  bool confirmed_current_wake = false;
};

AutoIdentityWakeState
    auto_identity_wake[MAX_PLANTS];

AutoIdentityWakeState* automaticIdentityState(
    uint32_t sensor_id) {
  AutoIdentityWakeState* empty = nullptr;

  for (size_t i = 0;
       i < MAX_PLANTS;
       ++i) {
    AutoIdentityWakeState& entry =
        auto_identity_wake[i];

    if (entry.sensor_id == sensor_id) {
      return &entry;
    }

    if (entry.sensor_id == 0 &&
        empty == nullptr) {
      empty = &entry;
    }
  }

  if (empty != nullptr) {
    empty->sensor_id = sensor_id;
  }

  return empty;
}

bool automaticIdentityDue(
    uint32_t sensor_id,
    uint32_t awake_ms) {
  AutoIdentityWakeState* state =
      automaticIdentityState(sensor_id);

  if (state == nullptr) {
    // Database capacity is already bounded to MAX_PLANTS. If this ever happens,
    // prefer an extra ASSIGN over withholding current authority.
    return true;
  }

  const uint32_t now = millis();

  const bool long_receive_gap =
      state->last_t5_seen_ms != 0 &&
      (now - state->last_t5_seen_ms) >=
          AUTO_IDENTITY_NEW_WAKE_GAP_MS;

  const bool awake_counter_restarted =
      state->have_awake &&
      awake_ms < state->last_awake_ms;

  const bool new_wake =
      !state->have_awake ||
      long_receive_gap ||
      awake_counter_restarted;

  if (new_wake) {
    state->confirmed_current_wake = false;
  }

  state->have_awake = true;
  state->last_awake_ms = awake_ms;
  state->last_t5_seen_ms = now;

  return !state->confirmed_current_wake;
}

void noteAutomaticIdentitySent(
    uint32_t sensor_id) {
  AutoIdentityWakeState* state =
      automaticIdentityState(sensor_id);

  if (state != nullptr) {
    state->confirmed_current_wake = true;
  }
}

'@

$newText = [regex]::Replace(
    $text,
    $statePattern,
    [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $newState },
    1
)

$udpOld = @'
      automaticIdentityDue(
          packet.sensor_id)) {
'@
$udpNew = @'
      automaticIdentityDue(
          packet.sensor_id,
          packet.awake_ms)) {
'@

$udpCount = ([regex]::Matches($newText, [regex]::Escape($udpOld))).Count
if ($udpCount -ne 1) {
    throw "STOPPED: expected one UDP automaticIdentityDue call, found $udpCount. Nothing has been written."
}
$newText = $newText.Replace($udpOld, $udpNew)

$espOld = @'
      automaticIdentityDue(
          event.packet.sensor_id)) {
'@
$espNew = @'
      automaticIdentityDue(
          event.packet.sensor_id,
          event.packet.awake_ms)) {
'@

$espCount = ([regex]::Matches($newText, [regex]::Escape($espOld))).Count
if ($espCount -ne 1) {
    throw "STOPPED: expected one ESP-NOW automaticIdentityDue call, found $espCount. Nothing has been written."
}
$newText = $newText.Replace($espOld, $espNew)

# Focused final checks only.
foreach ($needle in @(
    "AUTO_IDENTITY_NEW_WAKE_GAP_MS = 30000",
    "awake_ms < state->last_awake_ms",
    "packet.awake_ms",
    "event.packet.awake_ms",
    "confirmed_current_wake"
)) {
    if (-not $newText.Contains($needle)) {
        throw "STOPPED: focused validation missing '$needle'. Nothing has been written."
    }
}

if ($newText.Contains("AUTO_IDENTITY_COOLDOWN_MS")) {
    throw "STOPPED: old 3-minute cooldown still remains. Nothing has been written."
}

$out = if ($hadCrLf) { $newText.Replace("`n", "`r`n") } else { $newText }
[System.IO.File]::WriteAllText($Path, $out, $utf8)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "T5 automatic identity is now wake-scoped:"
Write-Host "  - first reading/beacon of a new XIAO wake can ASSIGN"
Write-Host "  - 5-second samples in the same wake do not repeat ASSIGN"
Write-Host "  - a restarted XIAO awake_ms immediately opens a new identity session"
Write-Host "  - >30 sec without a packet also opens a new identity session"
Write-Host "  - explicit Identify/provisioning behavior is unchanged"
Write-Host ""
Write-Host "Only $Relative was changed."
