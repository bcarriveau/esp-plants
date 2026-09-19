$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ExpectedBranch = "waveshare-zigbee"

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $Utf8NoBom)
}

function Replace-Required([string]$Content, [string]$Old, [string]$New, [string]$Description) {
    if (-not $Content.Contains($Old)) {
        throw "Could not apply '$Description': expected text was not found."
    }
    return $Content.Replace($Old, $New)
}

Write-Host ""
Write-Host "ESP PLANTS - H2 APS RSSI compatibility fix"
Write-Host "Repository: $Root"
Write-Host ""

if (-not (Test-Path (Join-Path $Root "firmware\m5-h2-zigbee\src\main.cpp"))) {
    throw "This must be run from the ESP PLANTS repository root package."
}

$Git = Get-Command git -ErrorAction SilentlyContinue
if ($Git) {
    $Branch = (& git -C $Root branch --show-current 2>$null).Trim()
    if ($Branch -and $Branch -ne $ExpectedBranch) {
        throw "Current Git branch is '$Branch'. This fix is for '$ExpectedBranch'."
    }
}

# Shared PlantLink: establish one explicit sentinel for SDKs that do not expose RSSI.
$PlantLinkPath = Join-Path $Root "shared\plantlink\plantlink.h"
$PlantLink = [System.IO.File]::ReadAllText($PlantLinkPath)

if (-not $PlantLink.Contains("kRssiUnavailableDbm")) {
    $PlantLink = Replace-Required $PlantLink `
        "constexpr size_t kSensorReportPayloadBytes = 21;" `
        "constexpr size_t kSensorReportPayloadBytes = 21;`r`nconstexpr int8_t kRssiUnavailableDbm = static_cast<int8_t>(-128);" `
        "PlantLink RSSI unavailable sentinel"
}

$PlantLink = $PlantLink.Replace(
    "  int8_t rssiDbm = 0;",
    "  int8_t rssiDbm = kRssiUnavailableDbm;"
)
Write-Utf8NoBom $PlantLinkPath $PlantLink

# H2: Arduino-ESP32 3.3.7's bundled APS indication type provides LQI but
# does not provide ind.rssi. Keep RSSI explicitly unavailable instead of
# inventing a conversion from LQI.
$H2Path = Join-Path $Root "firmware\m5-h2-zigbee\src\main.cpp"
$H2 = [System.IO.File]::ReadAllText($H2Path)

$RssiDefaultCount = ([regex]::Matches($H2, [regex]::Escape("  int8_t rssi = 0;"))).Count
if ($RssiDefaultCount -gt 0) {
    $H2 = $H2.Replace(
        "  int8_t rssi = 0;",
        "  int8_t rssi = plantlink::kRssiUnavailableDbm;"
    )
}

if ($H2.Contains("  event.rssi = ind.rssi;")) {
    $H2 = $H2.Replace(
        "  event.rssi = ind.rssi;",
        "  // Arduino-ESP32 3.3.7's APS indication exposes LQI but no RSSI field.`r`n  // Do not derive fake dBm from LQI; -128 means RSSI unavailable on PlantLink.`r`n  event.rssi = plantlink::kRssiUnavailableDbm;"
    )
} elseif (-not $H2.Contains("event.rssi = plantlink::kRssiUnavailableDbm;")) {
    throw "Could not locate the H2 APS RSSI assignment to fix."
}

Write-Utf8NoBom $H2Path $H2

# Waveshare diagnostics: don't print -128 as though it were a real measurement.
$WavesharePath = Join-Path $Root "firmware\waveshare-hub\src\main.cpp"
$Waveshare = [System.IO.File]::ReadAllText($WavesharePath)
$OldLog = '  Serial.printf("[sensor] %s  lqi=%u rssi=%d\n", text, report.lqi, report.rssiDbm);'
if ($Waveshare.Contains($OldLog)) {
    $NewLog = @'
  if (report.rssiDbm == plantlink::kRssiUnavailableDbm) {
    Serial.printf("[sensor] %s  lqi=%u rssi=n/a\n", text, report.lqi);
  } else {
    Serial.printf("[sensor] %s  lqi=%u rssi=%d\n", text, report.lqi, report.rssiDbm);
  }
'@
    $Waveshare = $Waveshare.Replace($OldLog, $NewLog.TrimEnd())
    Write-Utf8NoBom $WavesharePath $Waveshare
}

# Protocol documentation: same wire layout, clarified sentinel semantics.
$ProtocolPath = Join-Path $Root "docs\PLANTLINK_PROTOCOL.md"
$Protocol = [System.IO.File]::ReadAllText($ProtocolPath)
$Sentence = "An `rssi_dbm` value of `-128` means RSSI is unavailable from the active Zigbee SDK/API; LQI remains valid."
if (-not $Protocol.Contains($Sentence)) {
    $Anchor = "Only fields whose validity bits are set in `field_flags` are authoritative."
    if ($Protocol.Contains($Anchor)) {
        $Protocol = $Protocol.Replace(
            $Anchor,
            "$Sentence`r`n`r`n$Anchor"
        )
        Write-Utf8NoBom $ProtocolPath $Protocol
    }
}

# Changelog: document the compatibility correction without claiming hardware validation.
$ChangelogPath = Join-Path $Root "CHANGELOG.md"
$Changelog = [System.IO.File]::ReadAllText($ChangelogPath)
$Bullet = "- Fixed ESP32-H2 APS capture compatibility with the Arduino-ESP32 3.3.7 Zigbee API, which exposes LQI but not an APS RSSI member; unavailable RSSI is now carried as `-128` instead of referencing a nonexistent field."
if (-not $Changelog.Contains($Bullet)) {
    $Anchor = "#### Added"
    $Index = $Changelog.IndexOf($Anchor)
    if ($Index -ge 0) {
        $InsertAt = $Index + $Anchor.Length
        $Changelog = $Changelog.Insert($InsertAt, "`r`n`r`n$Bullet")
        Write-Utf8NoBom $ChangelogPath $Changelog
    }
}

Write-Host "Source fix applied."
Write-Host ""

$Pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $Pio)) {
    throw "PlatformIO CLI not found at $Pio"
}

$Project = Join-Path $Root "firmware\m5-h2-zigbee"
Write-Host "Building H2 now..."
Write-Host "$Pio run -d `"$Project`""
Write-Host ""

& $Pio run -d $Project
$BuildCode = $LASTEXITCODE

if ($BuildCode -ne 0) {
    Write-Host ""
    Write-Host "H2 build still has an error. Paste the next compiler error back into ChatGPT."
    exit $BuildCode
}

Write-Host ""
Write-Host "H2 BUILD PASSED."
Write-Host "Review the Git diff, then commit when ready."
exit 0
