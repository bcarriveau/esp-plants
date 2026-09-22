@echo off
setlocal
cd /d "%~dp0\.."

echo ============================================================
echo  ESP PLANTS - WAVESHARE RELEASE BUILD
echo ============================================================
echo.
echo VERSION SOURCE:
echo   firmware\waveshare-hub\include\build_version.h
echo.
echo Change ONLY this line before a release:
echo   ESP_PLANTS_WAVESHARE_VERSION
echo.
echo Build ID, package name, manifest version and GitHub tag are generated
echo from that version automatically.
echo.

set "RELEASE_VERSION="
for /f "tokens=3" %%V in ('findstr /b /c:"#define ESP_PLANTS_WAVESHARE_VERSION " firmware\waveshare-hub\include\build_version.h') do set "RELEASE_VERSION=%%~V"
if not defined RELEASE_VERSION goto config_failed
>VERSION echo %RELEASE_VERSION%
echo Release version: %RELEASE_VERSION%
echo GitHub tag:      v%RELEASE_VERSION%
echo.

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if exist "%PIO%" goto build
where pio >nul 2>&1
if not errorlevel 1 (
  set "PIO=pio"
  goto build
)
where platformio >nul 2>&1
if not errorlevel 1 (
  set "PIO=platformio"
  goto build
)
goto pio_missing

:build
echo Using: %PIO%
echo.
"%PIO%" run -d firmware\waveshare-hub -e waveshare_s3_touch_lcd_7_release
if errorlevel 1 goto build_failed

echo.
echo ============================================================
echo  RELEASE BUILD COMPLETE
echo ============================================================
echo.
echo Upload the TWO generated files from release\ to GitHub Release:
echo   v%RELEASE_VERSION%
echo.
pause
exit /b 0

:build_failed
echo.
echo ============================================================
echo  RELEASE BUILD FAILED
echo ============================================================
echo.
echo Nothing was published. Read the PlatformIO error above.
echo.
pause
exit /b 1

:pio_missing
echo.
echo ERROR: PlatformIO was not found.
echo Expected:
echo   %USERPROFILE%\.platformio\penv\Scripts\platformio.exe
echo.
pause
exit /b 1

:config_failed
echo.
echo ERROR: Could not read ESP_PLANTS_WAVESHARE_VERSION from:
echo   firmware\waveshare-hub\include\build_version.h
echo.
pause
exit /b 1
