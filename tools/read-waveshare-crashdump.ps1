param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Environment = "waveshare_s3_touch_lcd_7"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$PioHome = Join-Path $env:USERPROFILE ".platformio"
$Python = Join-Path $PioHome "penv\Scripts\python.exe"
$Esptool = Join-Path $PioHome "packages\tool-esptoolpy\esptool.py"
$Elf = Join-Path $RepoRoot "firmware\waveshare-hub\.pio\build\$Environment\firmware.elf"
$DumpDir = Join-Path $RepoRoot "crashdumps"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Dump = Join-Path $DumpDir "waveshare-coredump-$Stamp.bin"
$Report = Join-Path $DumpDir "waveshare-coredump-$Stamp.txt"

if (-not (Test-Path $Python)) {
    throw "PlatformIO Python was not found: $Python"
}
if (-not (Test-Path $Elf)) {
    throw "Firmware ELF was not found: $Elf`nBuild $Environment first and DO NOT clean .pio before decoding the crash."
}

New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null

Write-Host "Reading ESP PLANTS Waveshare coredump partition..."
if (Test-Path $Esptool) {
    & $Python $Esptool --chip esp32s3 --port $Port --baud 460800 read_flash 0xff0000 0x10000 $Dump
} else {
    & $Python -m esptool --chip esp32s3 --port $Port --baud 460800 read_flash 0xff0000 0x10000 $Dump
}
if ($LASTEXITCODE -ne 0) {
    throw "esptool failed while reading the coredump partition."
}

Write-Host "Raw coredump saved: $Dump"

$Decoder = $null
foreach ($Name in @("esp-coredump.exe", "esp-coredump", "espcoredump.py")) {
    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        $Decoder = $Command.Source
        break
    }
}

if (-not $Decoder) {
    $Candidates = Get-ChildItem -Path $PioHome -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in @("esp-coredump.exe", "espcoredump.py") } |
        Select-Object -First 1
    if ($Candidates) {
        $Decoder = $Candidates.FullName
    }
}

if ($Decoder) {
    Write-Host "Decoding against: $Elf"
    if ($Decoder.EndsWith(".py")) {
        & $Python $Decoder --chip esp32s3 info_corefile --core $Dump --core-format raw $Elf 2>&1 |
            Tee-Object -FilePath $Report
    } else {
        & $Decoder --chip esp32s3 info_corefile --core $Dump --core-format raw $Elf 2>&1 |
            Tee-Object -FilePath $Report
    }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Decoded report saved: $Report"
        exit 0
    }
    Write-Warning "The raw dump was read successfully, but automatic decoding failed. Keep both the .bin and firmware.elf files."
} else {
    Write-Warning "esp-coredump was not found in PlatformIO. The raw dump is still saved successfully."
}

Write-Host "ELF:      $Elf"
Write-Host "Coredump: $Dump"
Write-Host "Send/upload both files together with the serial log for analysis."
