Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Relative = "firmware\xiao-soil-sensor\src\main.cpp"
$Path = Join-Path (Get-Location) $Relative

if (-not (Test-Path -LiteralPath $Path)) {
    throw "Could not find $Relative from the current folder."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$raw = [System.IO.File]::ReadAllText($Path, $utf8)
$hadCrLf = $raw.Contains("`r`n")
$text = $raw.Replace("`r`n", "`n")

$old = @'
  rememberObservation(measurement);

  const plant::ReadingPacket reading =
      makeReadingPacket(measurement);

  sendEspNowReadingBeacon(
      reading,
      "Service");

  const bool acknowledged =
      sendReadingUdp(
'@

$new = @'
  rememberObservation(measurement);

  const plant::ReadingPacket reading =
      makeReadingPacket(measurement);

  // The initial service-entry reading already advertises this awake sensor
  // over ESP-NOW. Do not transmit another ESP-NOW beacon before every 5-second
  // service sample; keep the steady-state service path to UDP + application ACK.
  const bool acknowledged =
      sendReadingUdp(
'@

$first = $text.IndexOf($old, [System.StringComparison]::Ordinal)

if ($first -lt 0) {
    throw "STOPPED: exact sendFreshServiceReading beacon block was not found. Nothing has been written."
}

$second = $text.IndexOf(
    $old,
    $first + $old.Length,
    [System.StringComparison]::Ordinal
)

if ($second -ge 0) {
    throw "STOPPED: exact sendFreshServiceReading beacon block appeared more than once. Nothing has been written."
}

$initialMarker = @'
    sendEspNowReadingBeacon(
        first_reading,
        "Service");
'@

if (-not $text.Contains($initialMarker)) {
    throw "STOPPED: initial service-entry ESP-NOW beacon marker is missing. Nothing has been written."
}

$newText =
    $text.Substring(0, $first) +
    $new +
    $text.Substring($first + $old.Length)

if (-not $newText.Contains($initialMarker)) {
    throw "STOPPED: initial service-entry beacon would be lost. Nothing has been written."
}

if (-not $newText.Contains("Do not transmit another ESP-NOW beacon before every 5-second")) {
    throw "STOPPED: replacement validation failed. Nothing has been written."
}

$out = if ($hadCrLf) { $newText.Replace("`n", "`r`n") } else { $newText }

[System.IO.File]::WriteAllText($Path, $out, $utf8)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "XIAO service ESP-NOW behavior:"
Write-Host "  - service entry: one ESP-NOW reading beacon remains"
Write-Host "  - 5-second auto-samples: UDP only"
Write-Host "  - manual fresh readings: UDP only"
Write-Host "  - ESP-NOW remains initialized for Locate/identity/provisioning"
Write-Host ""
Write-Host "Only $Relative was changed."
Write-Host ""
git diff -- $Relative
