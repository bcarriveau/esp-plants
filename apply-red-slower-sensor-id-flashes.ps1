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

function Replace-ExactCount(
    [string]$Text,
    [string]$Old,
    [string]$New,
    [int]$ExpectedCount,
    [string]$Label
) {
    $count = 0
    $offset = 0

    while ($true) {
        $index = $Text.IndexOf(
            $Old,
            $offset,
            [System.StringComparison]::Ordinal
        )

        if ($index -lt 0) {
            break
        }

        $count++
        $offset = $index + $Old.Length
    }

    if ($count -ne $ExpectedCount) {
        throw "STOPPED: '$Label' expected $ExpectedCount occurrence(s), found $count. Nothing has been written."
    }

    Write-Host "  OK  $Label ($count)"
    return $Text.Replace($Old, $New)
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

Run this script from the root of your esp-plants clone, or copy this .ps1 file
into that root and run it there.
"@
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$XiaoPath = Join-Path $RepoRoot $XiaoRelative
$T5Path = Join-Path $RepoRoot $T5Relative

$XiaoRaw = [System.IO.File]::ReadAllText($XiaoPath, $utf8NoBom)
$T5Raw = [System.IO.File]::ReadAllText($T5Path, $utf8NoBom)

$XiaoCrLf = $XiaoRaw.Contains("`r`n")
$T5CrLf = $T5Raw.Contains("`r`n")

$Xiao = $XiaoRaw.Replace("`r`n", "`n")
$T5 = $T5Raw.Replace("`r`n", "`n")

Write-Host ""
Write-Host "ESP PLANTS - slower RED sensor-number identification"
Write-Host "Repository: $RepoRoot"
Write-Host ""

# Guard that this is the post-identity-handshake local source, not old main.
foreach ($needle in @(
    "identity_confirmed_this_wake",
    "T5 authority confirmed: Sensor #",
    "handleIdentityUdpPacket",
    "flashSensorNumberOnce"
)) {
    if (-not $Xiao.Contains($needle)) {
        throw "STOPPED: XIAO does not contain expected post-handshake marker '$needle'. Nothing has been written."
    }
}

foreach ($needle in @(
    "sendIdentityEspNow",
    "Identity ACK via %s",
    "Identify Sensor #"
)) {
    if (-not $T5.Contains($needle)) {
        throw "STOPPED: T5 does not contain expected post-handshake marker '$needle'. Nothing has been written."
    }
}

Write-Host "Post-handshake markers found. Applying polish edits in memory..."

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
'@ "XIAO - slow identity cadence and reserve RED for number pulses"

$Xiao = Replace-ExactlyOnce $Xiao @'
void pulseGreen(
    uint32_t on_ms) {
  allStatusLedsOff();
  digitalWrite(PIN_LED_GREEN, HIGH);
  delay(on_ms);
  digitalWrite(PIN_LED_GREEN, LOW);
}
'@ @'
void pulseIdentityRed(
    uint32_t on_ms) {
  allStatusLedsOff();
  digitalWrite(PIN_LED_RED, HIGH);
  delay(on_ms);
  digitalWrite(PIN_LED_RED, LOW);
}
'@ "XIAO - make identity pulse RED"

$Xiao = Replace-ExactCount `
    $Xiao `
    "pulseGreen(" `
    "pulseIdentityRed(" `
    2 `
    "XIAO - route long/short number elements through RED pulse helper"

$Xiao = Replace-ExactCount `
    $Xiao `
    "delay(250);" `
    "delay(IDENTITY_TO_SERVICE_PAUSE_MS);" `
    2 `
    "XIAO - add readable pause before solid GREEN service state"

$Xiao = Replace-ExactlyOnce $Xiao @'
        "IDENTIFY: Sensor #%u, long=10 short=1, repeating for %lu ms.\n",
'@ @'
        "IDENTIFY: Sensor #%u, RED long=10 / short=1, repeating for %lu ms.\n",
'@ "XIAO - describe RED numbered Identify pattern in serial"

$T5 = Replace-ExactlyOnce $T5 `
    " Watch the selected sensor repeat its numbered green identity pattern for about 8 seconds. Long = 10; short = 1.</p>" `
    " Watch the selected sensor repeat its numbered red identity pattern for about 8 seconds. Long = 10; short = 1.</p>" `
    "T5 - correct Identify page wording to RED"

# Final checks before the first write.
if ($Xiao.Contains("void pulseGreen(")) {
    throw "STOPPED: old green identity helper remains. Nothing has been written."
}

if (-not $Xiao.Contains("digitalWrite(PIN_LED_RED, HIGH);")) {
    throw "STOPPED: RED identity pulse was not installed. Nothing has been written."
}

if (-not $Xiao.Contains("IDENTITY_SHORT_PULSE_MS = 300")) {
    throw "STOPPED: slower short pulse was not installed. Nothing has been written."
}

if (-not $Xiao.Contains("IDENTITY_LONG_PULSE_MS = 1000")) {
    throw "STOPPED: slower long pulse was not installed. Nothing has been written."
}

if (-not $Xiao.Contains("IDENTITY_TO_SERVICE_PAUSE_MS = 600")) {
    throw "STOPPED: service handoff pause was not installed. Nothing has been written."
}

if (-not $T5.Contains("numbered red identity pattern")) {
    throw "STOPPED: T5 Identify wording was not updated. Nothing has been written."
}

$XiaoOut = if ($XiaoCrLf) { $Xiao.Replace("`n", "`r`n") } else { $Xiao }
$T5Out   = if ($T5CrLf)   { $T5.Replace("`n", "`r`n") } else { $T5 }

[System.IO.File]::WriteAllText($XiaoPath, $XiaoOut, $utf8NoBom)
[System.IO.File]::WriteAllText($T5Path, $T5Out, $utf8NoBom)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "Updated:"
Write-Host "  $XiaoRelative"
Write-Host "  $T5Relative"
Write-Host ""
Write-Host "Physical identity behavior:"
Write-Host "  #1  = one 300 ms RED pulse"
Write-Host "  #3  = three 300 ms RED pulses"
Write-Host "  #10 = one 1000 ms RED pulse"
Write-Host "  #13 = one 1000 ms RED pulse + three 300 ms RED pulses"
Write-Host "  pulse gap = 300 ms"
Write-Host "  after number = 600 ms pause, then solid GREEN service state"
Write-Host ""
Write-Host "Unprovisioned RED/GREEN alternation is unchanged."
Write-Host "Identity protocol and packet layouts are unchanged."
