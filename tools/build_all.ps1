$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"

if (-not (Test-Path $Pio)) {
    throw "PlatformIO CLI not found at $Pio. Install/repair the PlatformIO IDE extension in VS Code."
}

Write-Host "Checking shared protocol..."
python (Join-Path $Root "tools\check_protocol_sync.py")

$Projects = @(
    @{ Name = "Waveshare hub"; Path = "firmware\waveshare-hub" },
    @{ Name = "M5Stack H2 Zigbee"; Path = "firmware\m5-h2-zigbee" },
    @{ Name = "LILYGO T5 hub"; Path = "firmware\t5-hub" },
    @{ Name = "XIAO soil sensor"; Path = "firmware\xiao-soil-sensor" }
)

foreach ($Project in $Projects) {
    Write-Host ""
    Write-Host "Building $($Project.Name)..."
    & $Pio run -d (Join-Path $Root $Project.Path)
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "All ESP PLANTS PlatformIO builds completed."
