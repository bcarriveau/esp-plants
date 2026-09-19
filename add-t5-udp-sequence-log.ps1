Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Relative = "firmware\t5-hub\src\main.cpp"

$roots = @(
    (Get-Location).Path,
    $PSScriptRoot
) | Select-Object -Unique

$RepoRoot = $null
foreach ($root in $roots) {
    $candidate = Join-Path $root $Relative
    if (Test-Path -LiteralPath $candidate) {
        $RepoRoot = $root
        break
    }
}

if ($null -eq $RepoRoot) {
    throw "Could not find firmware\t5-hub\src\main.cpp."
}

$Path = Join-Path $RepoRoot $Relative
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Raw = [System.IO.File]::ReadAllText($Path, $utf8NoBom)

$HadCrLf = $Raw.Contains("`r`n")
$Text = $Raw.Replace("`r`n", "`n")

if ($Text.Contains('"Sequence: %lu\n"')) {
    throw "Sequence logging already appears to be installed."
}

$pattern = '(?ms)(Serial\.println\(\s*"--- HOME WI-FI UDP SENSOR PACKET ---"\s*\);\s*Serial\.printf\(\s*"Sensor ID: 0x%08lX\\n",\s*(?:static_cast<unsigned long>\(\s*)?packet\.sensor_id(?:\s*\))?\s*\);)'

$matches = [regex]::Matches($Text, $pattern)

if ($matches.Count -ne 1) {
    throw "STOPPED: expected exactly one HOME WI-FI UDP packet logging block, found $($matches.Count). Nothing has been written."
}

$insert = @'
$1
  Serial.printf(
      "Sequence: %lu\n",
      static_cast<unsigned long>(
          packet.sequence));
'@

$NewText = [regex]::Replace(
    $Text,
    $pattern,
    $insert,
    1
)

if (-not $NewText.Contains('"Sequence: %lu\n"')) {
    throw "STOPPED: sequence log insertion validation failed. Nothing has been written."
}

$Out = if ($HadCrLf) {
    $NewText.Replace("`n", "`r`n")
} else {
    $NewText
}

[System.IO.File]::WriteAllText(
    $Path,
    $Out,
    $utf8NoBom
)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "Added T5 UDP sequence logging only."
Write-Host "Expected serial format:"
Write-Host "  --- HOME WI-FI UDP SENSOR PACKET ---"
Write-Host "  Sensor ID: 0x........"
Write-Host "  Sequence: 27"
Write-Host ""
Write-Host "No transport behavior was changed."
