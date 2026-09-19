@echo off
setlocal
set ROOT=%~dp0..
where g++ >nul 2>nul || (echo g++ not found on PATH & exit /b 1)
g++ -std=c++17 -Wall -Wextra -Werror -I "%ROOT%\\shared\\plantlink" -I "%ROOT%\\shared\\zg303z" "%ROOT%\\tests\\host\\test_shared.cpp" -o "%TEMP%\\esp_plants_shared_test.exe" || exit /b 1
"%TEMP%\\esp_plants_shared_test.exe"
