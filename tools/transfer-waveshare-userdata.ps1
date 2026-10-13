# ESP PLANTS Waveshare 7 / 7B user-data migration.
# Both boards use the same 128 KiB plantdata NVS partition at 0xC10000.
# No application, bootloader, partition table, H2 firmware or generic NVS is copied.
[CmdletBinding()]
param(
    [string]$SourcePort = 'COM13', # 7B as configured in platformio.ini
    [string]$TargetPort = 'COM11', # original 7 as configured in platformio.ini
    [string]$BackupRoot = '',
    [switch]$SkipWrite # safely test reads and backups without modifying target
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$offset = '0xC10000'
$length = '0x20000'
$expectedBytes = 131072

function Require-FileSize([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Backup missing: $path" }
    $size = (Get-Item -LiteralPath $path).Length
    if ($size -ne $expectedBytes) { throw "Unexpected backup size $size (expected $expectedBytes): $path" }
}
function Hash-File([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Run-Esptool([string[]]$arguments) {
    Write-Host ("esptool " + ($arguments -join ' '))
    if ($script:esptoolScript) {
        & $script:python $script:esptoolScript @arguments
    } else {
        & $script:python -m esptool @arguments
    }
    if ($LASTEXITCODE -ne 0) { throw "esptool returned exit code $LASTEXITCODE; transfer halted." }
}
function Read-Partition([string]$port, [string]$path) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing backup: $path" }
    Run-Esptool @('--chip','esp32s3','--port',$port,'--baud','460800','read_flash',$offset,$length,$path)
    Require-FileSize $path
    $hash = Hash-File $path
    Write-Host "Backup verified: $path`nSHA-256: $hash" -ForegroundColor Green
    return $hash
}

if ([string]::IsNullOrWhiteSpace($SourcePort) -or [string]::IsNullOrWhiteSpace($TargetPort)) {
    throw 'Specify both source and target COM ports.'
}
if ($SourcePort.Trim().ToUpperInvariant() -eq $TargetPort.Trim().ToUpperInvariant()) {
    throw 'Source and target ports are identical. Transfer halted.'
}
$pioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$pioEsptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
$script:esptoolScript = $null
if (Test-Path -LiteralPath $pioPython) {
    $script:python = $pioPython
    if (Test-Path -LiteralPath $pioEsptool) { $script:esptoolScript = $pioEsptool }
} else {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if (-not $found) { throw 'Python not found. Install PlatformIO or Python plus esptool.' }
    $script:python = $found.Source
}
if (-not $script:esptoolScript) {
    & $script:python -m esptool version
    if ($LASTEXITCODE -ne 0) { throw 'esptool not installed. Install with: python -m pip install esptool' }
}

if (-not $BackupRoot) { $BackupRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'ESP-PLANTS-Backups' }
$BackupRoot = [System.IO.Path]::GetFullPath($BackupRoot)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$session = Join-Path $BackupRoot "transfer-$timestamp"
# Never merge with an existing directory; avoids accidental overwrites.
if (Test-Path -LiteralPath $session) { throw "Session directory already exists: $session" }
New-Item -Path $session -ItemType Directory -Force | Out-Null
$sourceImage = Join-Path $session 'source-7B-plantdata.bin'
$targetImage = Join-Path $session 'target-7-original-plantdata.bin'
$verifyImage = Join-Path $session 'target-7-readback-plantdata.bin'

Write-Host "`nESP PLANTS -- Waveshare USER DATA transfer (no firmware flashing)" -ForegroundColor Cyan
Write-Host "SOURCE: $SourcePort  (7B)`nTARGET: $TargetPort  (7)`nPartition: plantdata, $offset, $length"
Write-Host 'Disconnect any serial monitor / VS Code terminal using these ports.'
Write-Host "Backups: $session"
Write-Host 'NOTE: This copies saved Wi-Fi credentials too. Keep backup files private.' -ForegroundColor Yellow
Write-Host 'NOTE: Zigbee network membership lives on the ESP32-H2 and is NOT cloned.' -ForegroundColor Yellow
Write-Host ''

# Only read the source. No modification to the source board.
Write-Host 'STEP 1: Backing up SOURCE 7B...' -ForegroundColor Cyan
$sourceHash = Read-Partition $SourcePort $sourceImage
Write-Host 'STEP 2: Backing up TARGET 7...' -ForegroundColor Cyan
$targetHash = Read-Partition $TargetPort $targetImage

@(
    "ESP PLANTS user-data migration session: $timestamp",
    "Source COM: $SourcePort; SHA256: $sourceHash",
    "Target COM: $TargetPort; original SHA256: $targetHash",
    "Partition: plantdata at $offset size $length",
    'Backups contain secrets (saved Wi-Fi passwords).',
    'Only the Waveshare NVS plantdata partition is copied; H2 Zigbee network is not.',
    'Restore original TARGET only after confirming hardware/partition map using esptool write_flash 0xC10000 target-7-original-plantdata.bin'
) | Set-Content -LiteralPath (Join-Path $session 'README-private.txt')

if ($SkipWrite) {
    Write-Host 'SkipWrite selected: both backups saved, destination untouched.' -ForegroundColor Green
    exit 0
}

Write-Host "`nSOURCE SHA256: $sourceHash"
Write-Host "TARGET original SHA256: $targetHash"
Write-Host "Backup folder: $session"
if ($sourceHash -eq $targetHash) {
    Write-Host 'Source and target partition already match; no write necessary.' -ForegroundColor Green
    exit 0
}
Write-Host "`nWARNING: Target $TargetPort existing plant names, assignments, preferences and Wi-Fi will be REPLACED." -ForegroundColor Yellow
Write-Host 'The original target backup has been saved.'
$answer = Read-Host "To copy $SourcePort -> $TargetPort, type exactly: TRANSFER"
if ($answer -cne 'TRANSFER') {
    Write-Host 'Cancelled. Backups preserved. No destination writes performed.' -ForegroundColor Yellow
    exit 0
}

Write-Host "STEP 3: Writing source plantdata to TARGET $TargetPort ..." -ForegroundColor Cyan
Run-Esptool @('--chip','esp32s3','--port',$TargetPort,'--baud','460800','write_flash',$offset,$sourceImage)
Write-Host 'STEP 4: Reading back target for SHA-256 verification...' -ForegroundColor Cyan
$verifyHash = Read-Partition $TargetPort $verifyImage
if ($verifyHash -ne $sourceHash) {
    throw "READ-BACK MISMATCH: expected $sourceHash but got $verifyHash. Target data not verified. Original target backup is $targetImage"
}
Write-Host "`nSUCCESS: target 7 plantdata matches source 7B (SHA-256 $verifyHash)." -ForegroundColor Green
Write-Host 'The 7 and 7B firmware were not copied/changed.'
Write-Host 'Move the SAME Zigbee H2 to the 7 to retain network membership; otherwise sensors may require pairing.'
Write-Host "KEEP BACKUPS PRIVATE: $session"
