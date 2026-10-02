$ErrorActionPreference = "Stop"

$bashScript = Join-Path $PSScriptRoot "build_7b_vsync_sdk.sh"
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Write-Error "WSL is required for Espressif's supported Arduino lib-builder workflow. Install/enable WSL, then rerun this task."
    exit 2
}

$wslPath = (& wsl.exe wslpath -a $bashScript).Trim()
if (-not $wslPath) {
    Write-Error "Could not translate the SDK builder path into WSL."
    exit 2
}

Write-Host "Building pinned ESP32-S3 3.0.7-h VSYNC SDK overlay in WSL..."
& wsl.exe bash $wslPath
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Error "7B VSYNC SDK build failed with exit code $code."
    exit $code
}

Write-Host "7B VSYNC SDK build complete. Use 'ESP PLANTS: Upload Waveshare 7B DIAG' next."
