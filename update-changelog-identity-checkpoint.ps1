Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$path = Join-Path (Get-Location) "CHANGELOG.md"
if (-not (Test-Path -LiteralPath $path)) {
    throw "CHANGELOG.md not found in the current folder."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$raw = [System.IO.File]::ReadAllText($path, $utf8)
$hadCrLf = $raw.Contains("`r`n")
$text = $raw.Replace("`r`n", "`n")

$checkpointHeading = "### Authoritative sensor identity / service checkpoint"

if (-not $text.Contains($checkpointHeading)) {
    $unreleased = "## [Unreleased]`n"
    $idx = $text.IndexOf($unreleased, [System.StringComparison]::Ordinal)
    if ($idx -lt 0) {
        throw "Could not find the Unreleased changelog heading. Nothing written."
    }

    $insertAt = $idx + $unreleased.Length

    $block = @'

### Authoritative sensor identity / service checkpoint

#### Added

- T5-owned authoritative sensor numbering for slots `#1` through `#16`, with
  the persisted T5 array index remaining the source of the visible sensor number.
- Additive protocol-v3 identity `ASSIGN` / `CLEAR` packets and identity ACKs
  without changing the existing reading, ACK, provisioning, or Locate packet
  layouts.
- XIAO NVS cache for the currently assigned T5 slot, while keeping the physical
  sensor ID as the immutable hardware identity.
- RED numbered identity indication on the XIAO after current-wake T5
  confirmation: short RED pulse = 1, long RED pulse = 10.
- T5 setup-page and e-paper slot-number visibility plus `Identify Sensor #n`.
- T5 UDP sequence-number logging so sender retries can be compared directly
  against received packet sequences.
- ESP32-S3 USB CDC-on-boot configuration for the T5 so application `Serial`
  diagnostics appear on the same USB connection as the ROM boot output.

#### Changed

- Manual XIAO service wake keeps GREEN on while Wi-Fi and T5 identity
  confirmation complete, briefly shows the confirmed RED sensor number, then
  returns to solid GREEN service state.
- An ordinary short press during the two-minute service window restarts the
  timer, replays the confirmed RED sensor number, then sends a fresh reading.
- The 5-second service auto-sampler pauses while the firmware is waiting to
  determine whether a button action is a single/two-click action or the
  existing triple-click calibration command.
- Automatic T5 identity assignment is throttled across UDP and ESP-NOW so a
  normal two-minute service session receives one automatic assignment instead
  of another `ASSIGN` every 5 seconds.
- XIAO startup diagnostics now report the actual hardware STA MAC address and
  no longer print a literal `\n` in the cached-slot message.

#### Verified on hardware

- T5 `#1` assignment was accepted and cached by XIAO sensor
  `0xDFBBF6A6`.
- Manual wake showed the confirmed RED `#1` indication followed by GREEN
  service state.
- Re-pressing the service button restarted the two-minute timer and replayed
  the confirmed sensor number.
- Repeated automatic identity-assignment chatter was eliminated after the
  initial assignment.
- T5 application serial output was restored over native ESP32-S3 USB CDC.
- T5 received UDP reading sequences `49` through `72` consecutively with no
  gaps during the diagnostic service run.
- XIAO retry logging showed intermittent first-attempt UDP ACK timeouts while
  still completing every tested exchange within the bounded retry count.

'@

    $text = $text.Insert($insertAt, $block)
}

# Replace only the Verification status section, bounded by the next heading.
$startHeading = "### Verification status`n"
$nextHeading = "### Planned / open`n"

$start = $text.IndexOf($startHeading, [System.StringComparison]::Ordinal)
if ($start -lt 0) {
    throw "Could not find '### Verification status'. Nothing written."
}

$next = $text.IndexOf($nextHeading, $start + $startHeading.Length, [System.StringComparison]::Ordinal)
if ($next -lt 0) {
    throw "Could not find '### Planned / open' after verification status. Nothing written."
}

$replacement = @'
### Verification status

- Battery-only reset/UDP receive, BQ27220 battery reporting, last-known restore,
  OFF-screen persistence, IO48 shutdown, and PWR/QON wake have been physically
  exercised during development.
- Authoritative T5 slot assignment, XIAO identity caching, RED numbered
  identification, service-button timer restart, USB CDC diagnostics, and
  sequential UDP reception have been physically verified on the current
  development hardware.
- Full multi-sensor enrollment/replacement/unassign lifecycle and final release
  verification remain open.

'@

$text = $text.Substring(0, $start) + $replacement + $text.Substring($next)

$out = if ($hadCrLf) { $text.Replace("`n", "`r`n") } else { $text }
[System.IO.File]::WriteAllText($path, $out, $utf8)

Write-Host ""
Write-Host "SUCCESS."
Write-Host "CHANGELOG.md updated for the tested identity/service checkpoint."
Write-Host "No firmware files were changed."
