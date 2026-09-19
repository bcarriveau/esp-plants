Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Relative = "firmware\xiao-soil-sensor\src\main.cpp"

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
    $probe = Join-Path $root $Relative
    if (Test-Path -LiteralPath $probe) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw "Could not find firmware\xiao-soil-sensor\src\main.cpp."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Path = Join-Path $RepoRoot $Relative
$Raw = [System.IO.File]::ReadAllText($Path, $utf8NoBom)
$HadCrLf = $Raw.Contains("`r`n")
$Text = $Raw.Replace("`r`n", "`n")

Write-Host ""
Write-Host "ESP PLANTS - service LED continuity + repeat sensor ID"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# Require the already-tested red-number identity build.
foreach ($needle in @(
    'IDENTITY_SHORT_PULSE_MS = 300',
    'IDENTITY_LONG_PULSE_MS = 1000',
    'IDENTITY_TO_SERVICE_PAUSE_MS = 600',
    'void pulseIdentityRed(',
    'Button wake confirmed by T5 as Sensor #%u; showing RED identity.',
    'Late T5 identity confirmation: Sensor #%u; showing RED identity.',
    'identity_confirmed_this_wake'
)) {
    if (-not $Text.Contains($needle)) {
        throw "STOPPED: expected current red-ID source marker missing: '$needle'. Nothing has been written."
    }
}

Write-Host "Current red-ID source markers found."
Write-Host "Applying two exact service-mode edits in memory..."

# 1) Keep GREEN visible while Wi-Fi/T5 identity confirmation is happening.
$Text = Replace-ExactlyOnce $Text @'
  // The initial green light proved the button wake. Turn it off while the T5
  // confirms identity; once confirmed the number pattern plays before solid.
  allStatusLedsOff();
  identity_confirmed_this_wake = false;

  service_mode_active = true;
'@ @'
  // Keep GREEN visible while Wi-Fi and the T5 identity handshake happen.
  // Once the T5 confirms the slot, the deliberate RED number pattern briefly
  // takes over, then service returns to solid GREEN.
  serviceLedOn();
  identity_confirmed_this_wake = false;

  service_mode_active = true;
'@ "Keep GREEN on during Wi-Fi/T5 confirmation"

# 2) After the existing multi-click window proves this was not a calibration
# triple-click, replay the current confirmed sensor number before the fresh send.
$Text = Replace-ExactlyOnce $Text @'
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(
            "%u short press%s: sending fresh reading.\n",
            completed_clicks,
            completed_clicks == 1
                ? ""
                : "es");

        sendFreshServiceReading(
            t5_requested_wake_seconds,
            initial_measurement);

        last_service_sample_ms =
            millis();
      } else {
'@ @'
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
'@ "Replay confirmed RED sensor number after ordinary short press"

# Final validation before write.
foreach ($needle in @(
    'Keep GREEN visible while Wi-Fi and the T5 identity handshake happen.',
    'service timer restarted; identifying then sending fresh reading.',
    'Never show an unconfirmed cached number.',
    'flashSensorNumberOnce(',
    'IDENTITY_TO_SERVICE_PAUSE_MS'
)) {
    if (-not $Text.Contains($needle)) {
        throw "STOPPED: final validation missing '$needle'. Nothing has been written."
    }
}

# Ensure the old intentional-dark startup behavior is gone.
if ($Text.Contains('Turn it off while the T5')) {
    throw "STOPPED: old service-start LED-off behavior still exists. Nothing has been written."
}

$Output = if ($HadCrLf) {
    $Text.Replace("`n", "`r`n")
} else {
    $Text
}

[System.IO.File]::WriteAllText(
    $Path,
    $Output,
    $utf8NoBom
)

Write-Host ""
Write-Host "SUCCESS."
Write-Host ""
Write-Host "New service behavior:"
Write-Host "  - button wake -> GREEN stays on during Wi-Fi/T5 connection"
Write-Host "  - T5 confirms slot -> RED number -> 600 ms dark pause -> GREEN"
Write-Host "  - ordinary short press restarts the 2-minute timer"
Write-Host "  - after the click window, confirmed RED number plays again"
Write-Host "  - then GREEN returns and a fresh reading is sent"
Write-Host "  - triple-click calibration remains separate and does NOT replay the number"
Write-Host ""
Write-Host "Only XIAO main.cpp was changed."
