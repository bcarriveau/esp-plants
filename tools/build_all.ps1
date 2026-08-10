$ErrorActionPreference = "Stop"

Write-Host "Checking protocol copies..."
python "$PSScriptRoot/check_protocol_sync.py"

Write-Host ""
Write-Host "Building T5 hub..."
pio run -d "$PSScriptRoot/../firmware/t5-hub"

Write-Host ""
Write-Host "Building XIAO sensor..."
pio run -d "$PSScriptRoot/../firmware/xiao-soil-sensor"

Write-Host ""
Write-Host "Both PlatformIO builds completed."
