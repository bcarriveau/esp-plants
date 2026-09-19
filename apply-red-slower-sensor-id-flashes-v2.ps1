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
    if ((Test-Path -LiteralPath (Join-Path $root $XiaoRelative)) -and
        (Test-Path -LiteralPath (Join-Path $root $T5Relative))) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw @"
Could not find the ESP PLANTS repository root.

Run this script from the root of the esp-plants clone.
"@
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$XiaoPath = Join-Path $RepoRoot $XiaoRelative
$T5Path = Join-Path $RepoRoot $T5Relative

$XiaoRaw = [System.IO.File]::ReadAllText($XiaoPath, $utf8NoBom)
$T5Raw   = [System.IO.File]::ReadAllText($T5Path, $utf8NoBom)

$XiaoCrLf = $XiaoRaw.Contains("`r`n")
$T5CrLf   = $T5Raw.Contains("`r`n")

$Xiao = $XiaoRaw.Replace("`r`n", "`n")
$T5   = $T5Raw.Replace("`r`n", "`n")

Write-Host ""
Write-Host "ESP PLANTS - slower RED sensor-number identification v2"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# Require the already-proven identity-handshake source first.
foreach ($needle in @(
    'T5 authority confirmed: Sensor #',
    'identity_confirmed_this_wake',
    'handleIdentityUdpPacket',
    'Button wake confirmed by T5 as Sensor #',
    'Late T5 identity confirmation: Sensor #'
)) {
    if (-not $Xiao.Contains($needle)) {
        throw "STOPPED: XIAO is not the expected post-handshake source ('$needle' missing). Nothing has been written."
    }
}

if (-not $T5.Contains('Identify Sensor #')) {
    throw "STOPPED: T5 is not the expected post-handshake source. Nothing has been written."
}

Write-Host "Post-handshake source markers found."
Write-Host "Applying only exact identity-specific edits in memory..."

# 1. Identity timing constants only.
$Xiao = Replace-ExactlyOnce $Xiao @'
// Sensor-number visual language. One long green pulse means ten; each short
// green pulse means one. Example: #13 = one long + three short.
constexpr uint32_t IDENTITY_SHORT_PULSE_MS = 180;
constexpr uint32_t IDENTITY_LONG_PULSE_MS = 800;
constexpr uint32_t IDENTITY_ELEMENT_GAP_MS = 180;
'@ @'
// Sensor-number visual language. RED is reserved for the deliberate number
// pattern so it is visually distinct from the solid GREEN service-ready state.
// One long red pulse means ten; each short red pulse means one.
// Example: #13 = one long + three short.
constexpr uint32_t IDENTITY_SHORT_PULSE_MS = 300;
constexpr uint32_t IDENTITY_LONG_PULSE_MS = 1000;
constexpr uint32_t IDENTITY_ELEMENT_GAP_MS = 300;
constexpr uint32_t IDENTITY_TO_SERVICE_PAUSE_MS = 600;
'@ "XIAO identity timing/constants"

# 2. Replace the complete helper so no generic call-count replacement is used.
$Xiao = Replace-ExactlyOnce $Xiao @'
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
'@ @'
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
'@ "XIAO complete number-flasher function -> RED"

# 3. Provisioning-success handoff: exact named block only.
$Xiao = Replace-ExactlyOnce $Xiao @'
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
'@ @'
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
'@ "XIAO provision-success number -> pause -> GREEN"

# 4. Initial manual service wake: exact named block only.
$Xiao = Replace-ExactlyOnce $Xiao @'
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
'@ @'
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
'@ "XIAO initial service-wake number -> pause -> GREEN"

# 5. Late confirmation: exact named block only.
$Xiao = Replace-ExactlyOnce $Xiao @'
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
'@ @'
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
'@ "XIAO late-confirm number -> pause -> GREEN"

# 6. Identify serial wording only.
$Xiao = Replace-ExactlyOnce $Xiao @'
        "IDENTIFY: Sensor #%u, long=10 short=1, repeating for %lu ms.\n",
'@ @'
        "IDENTIFY: Sensor #%u, RED long=10 / short=1, repeating for %lu ms.\n",
'@ "XIAO Identify serial wording"

# 7. Give the last repeated Identify pattern a readable gap before returning
# to the service GREEN LED. This is anchored to the numbered branch only.
$Xiao = Replace-ExactlyOnce $Xiao @'
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
'@ @'
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
'@ "XIAO final Identify pattern pause before GREEN restore"

# 8. T5 wording only; no T5 transport/protocol behavior is touched.
$T5 = Replace-ExactlyOnce $T5 `
    ' Watch the selected sensor repeat its numbered green identity pattern for about 8 seconds. Long = 10; short = 1.</p>' `
    ' Watch the selected sensor repeat its numbered red identity pattern for about 8 seconds. Long = 10; short = 1.</p>' `
    "T5 Identify web wording GREEN -> RED"

# Final in-memory validation before either file is written.
$mustHaveXiao = @(
    'IDENTITY_SHORT_PULSE_MS = 300',
    'IDENTITY_LONG_PULSE_MS = 1000',
    'IDENTITY_ELEMENT_GAP_MS = 300',
    'IDENTITY_TO_SERVICE_PAUSE_MS = 600',
    'void pulseIdentityRed(',
    'digitalWrite(PIN_LED_RED, HIGH);',
    'RED long=10 / short=1',
    'Button wake confirmed by T5 as Sensor #%u; showing RED identity.',
    'Late T5 identity confirmation: Sensor #%u; showing RED identity.'
)

foreach ($needle in $mustHaveXiao) {
    if (-not $Xiao.Contains($needle)) {
        throw "STOPPED: final XIAO validation missing '$needle'. Nothing has been written."
    }
}

if ($Xiao.Contains('void pulseGreen(')) {
    throw "STOPPED: old identity pulseGreen helper still exists. Nothing has been written."
}

# Protect unrelated delays: this edit must leave the overall six original
# delay(250) sites alone except the two identity-specific ones above.
# We do NOT use that count as a locator; this is only a post-edit sanity note.
$remainingDelay250 = ([regex]::Matches(
    $Xiao,
    [regex]::Escape('delay(250);'))).Count

Write-Host "  INFO remaining unrelated delay(250) calls: $remainingDelay250"

if (-not $T5.Contains('numbered red identity pattern')) {
    throw "STOPPED: final T5 wording validation failed. Nothing has been written."
}

# Only now write.
$XiaoOut = if ($XiaoCrLf) { $Xiao.Replace("`n", "`r`n") } else { $Xiao }
$T5Out   = if ($T5CrLf)   { $T5.Replace("`n", "`r`n") } else { $T5 }

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
Write-Host "Changed only:"
Write-Host "  - XIAO sensor-number LED color/timing/handoff"
Write-Host "  - T5 Identify page wording (green -> red)"
Write-Host ""
Write-Host "New physical language:"
Write-Host "  #1  = one 300 ms RED pulse"
Write-Host "  #3  = three 300 ms RED pulses, 300 ms gaps"
Write-Host "  #10 = one 1000 ms RED pulse"
Write-Host "  #13 = one 1000 ms RED + three 300 ms RED pulses"
Write-Host "  after number = 600 ms dark pause, then solid GREEN service LED"
Write-Host ""
Write-Host "No protocol, Wi-Fi, UDP, ESP-NOW, sensor sampling, sleep, or PMU code changed."
