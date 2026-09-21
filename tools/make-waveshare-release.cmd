@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

echo ============================================================
echo  ESP PLANTS - BUILD WAVESHARE GITHUB RELEASE
echo ============================================================
echo.

set "PIO_EXE="

rem Double-clicked CMD windows often do not inherit the same PATH as the
rem PlatformIO terminal inside VS Code. Try PlatformIO's normal Windows venv
rem first, then PATH aliases.
if exist "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" (
  set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
) else (
  where pio >nul 2>&1
  if not errorlevel 1 set "PIO_EXE=pio"
)

if not defined PIO_EXE (
  where platformio >nul 2>&1
  if not errorlevel 1 set "PIO_EXE=platformio"
)

if not defined PIO_EXE (
  echo ERROR: PlatformIO was not found.
  echo.
  echo Expected one of these:
  echo   %USERPROFILE%\.platformio\penv\Scripts\platformio.exe
  echo   pio on PATH
  echo   platformio on PATH
  echo.
  echo Open this project in VS Code with PlatformIO installed, or run the
  echo release build from a PlatformIO terminal.
  goto :failed
)

echo Using PlatformIO:
echo   %PIO_EXE%
echo.
echo Building the PUBLIC/DISTRIBUTION Waveshare firmware...
echo.

"%PIO_EXE%" run -d firmware\waveshare-hub -e waveshare_s3_touch_lcd_7_release
set "BUILD_RC=%ERRORLEVEL%"
if not "%BUILD_RC%"=="0" goto :build_failed

echo.
echo ============================================================
echo  RELEASE BUILD COMPLETE
echo ============================================================
echo.
echo Release assets are in:
echo   release\
echo.
echo Upload BOTH:
echo   1. the versioned .plantsota file
echo   2. esp-plants-waveshare.manifest.json
echo.
echo The GitHub Release tag must match VERSION.
echo.
pause
exit /b 0

:build_failed
echo.
echo ============================================================
echo  RELEASE BUILD FAILED - exit code %BUILD_RC%
echo ============================================================
echo.
echo The error above is the real PlatformIO/build error. Nothing was published.
echo.
pause
exit /b %BUILD_RC%

:failed
echo.
echo RELEASE BUILD DID NOT START.
echo.
pause
exit /b 1
