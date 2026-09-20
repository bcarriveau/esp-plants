@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\apply_home_selector_fix.ps1"
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo Patch failed. Helper files were left in place.
    pause
    exit /b %RC%
)

echo.
echo Patch applied. No build, clean, upload, or monitor task was run.
echo.
echo Commit message:
echo   Keep plant selection on the home dashboard
echo.
echo Removing temporary helper files...
rmdir /s /q "%~dp0tools" >nul 2>&1
del /f /q "%~f0" >nul 2>&1
