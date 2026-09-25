@echo off
setlocal
cd /d "%~dp0\.."
echo ============================================================
echo  ESP PLANTS - PHASE 2 UPDATE ALL RELEASE BUILD
echo ============================================================
set "RELEASE_VERSION="
set "H2_VERSION="
for /f "tokens=3" %%V in ('findstr /b /c:"#define ESP_PLANTS_WAVESHARE_VERSION " firmware\waveshare-hub\include\build_version.h') do set "RELEASE_VERSION=%%~V"
for /f "tokens=3" %%V in ('findstr /b /c:"#define ESP_PLANTS_H2_VERSION " firmware\m5-h2-zigbee\include\build_version.h') do set "H2_VERSION=%%~V"
if not defined RELEASE_VERSION goto config_failed
if not defined H2_VERSION goto config_failed
>VERSION echo %RELEASE_VERSION%
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if exist "%PIO%" goto build
where pio >nul 2>&1 && set "PIO=pio" && goto build
where platformio >nul 2>&1 && set "PIO=platformio" && goto build
goto pio_missing
:build
echo Building H2 distribution image v%H2_VERSION% first...
"%PIO%" run -d firmware\m5-h2-zigbee -e m5_gateway_h2_release
if errorlevel 1 goto build_failed
echo Building Waveshare distribution package v%RELEASE_VERSION% and combined manifest...
"%PIO%" run -d firmware\waveshare-hub -e waveshare_s3_touch_lcd_7_release
if errorlevel 1 goto build_failed
echo.
echo Waveshare release v%RELEASE_VERSION% complete with H2 firmware v%H2_VERSION%.
echo Upload ALL generated files from release\:
echo   esp-plants-waveshare-%RELEASE_VERSION%.plantsota
echo   esp-plants-waveshare.manifest.json
echo   esp-plants-h2-%H2_VERSION%.bin
echo   esp-plants-h2-%H2_VERSION%.bin.sha256
pause
exit /b 0
:build_failed
echo RELEASE BUILD FAILED. Nothing was published.
pause
exit /b 1
:pio_missing
echo ERROR: PlatformIO was not found.
pause
exit /b 1
:config_failed
echo ERROR: Could not read Waveshare or H2 firmware version.
pause
exit /b 1
