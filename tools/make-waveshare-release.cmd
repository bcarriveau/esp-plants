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
set "PYTHON=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if exist "%PIO%" goto build
where pio >nul 2>&1 && set "PIO=pio" && goto build
where platformio >nul 2>&1 && set "PIO=platformio" && goto build
goto pio_missing

:build
echo Building H2 distribution image v%H2_VERSION% first...
"%PIO%" run -d firmware\m5-h2-zigbee -e m5_gateway_h2_release
if errorlevel 1 goto build_failed

echo Building Waveshare distribution image v%RELEASE_VERSION%...
"%PIO%" run -d firmware\waveshare-hub -e waveshare_s3_touch_lcd_7_release
if errorlevel 1 goto build_failed

echo Packaging Waveshare + H2 release assets explicitly...
if not exist "firmware\m5-h2-zigbee\.pio\build\m5_gateway_h2_release\firmware.bin" goto package_missing_binary
if not exist "firmware\waveshare-hub\.pio\build\waveshare_s3_touch_lcd_7_release\firmware.bin" goto package_missing_binary
if exist "%PYTHON%" goto run_packager
where python >nul 2>&1 && set "PYTHON=python" && goto run_packager
goto python_missing

:run_packager
if not exist "release" mkdir "release"
del /q "release\esp-plants-waveshare-%RELEASE_VERSION%.plantsota" >nul 2>&1
del /q "release\esp-plants-waveshare.manifest.json" >nul 2>&1
del /q "release\esp-plants-h2-%H2_VERSION%.bin" >nul 2>&1
del /q "release\esp-plants-h2-%H2_VERSION%.bin.sha256" >nul 2>&1

"%PYTHON%" "firmware\waveshare-hub\scripts\build_plants_ota.py" ^
  "firmware\waveshare-hub\.pio\build\waveshare_s3_touch_lcd_7_release\firmware.bin" ^
  "firmware\waveshare-hub\include\build_version.h" ^
  "release"
if errorlevel 1 goto package_failed

if not exist "release\esp-plants-waveshare-%RELEASE_VERSION%.plantsota" goto package_failed
if not exist "release\esp-plants-waveshare.manifest.json" goto package_failed
if not exist "release\esp-plants-h2-%H2_VERSION%.bin" goto package_failed
if not exist "release\esp-plants-h2-%H2_VERSION%.bin.sha256" goto package_failed

echo.
echo Waveshare release v%RELEASE_VERSION% complete with H2 firmware v%H2_VERSION%.
echo Verified generated files in release\:
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

:package_missing_binary
echo RELEASE PACKAGING FAILED: expected release firmware.bin was not produced.
pause
exit /b 1

:package_failed
echo RELEASE PACKAGING FAILED. Expected release files were not produced.
pause
exit /b 1

:python_missing
echo ERROR: Python was not found for the release packaging step.
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
