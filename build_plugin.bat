@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_plugin.ps1" %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Soundpad build failed!
    pause
    exit /b %ERRORLEVEL%
)
echo.
echo [OK] Soundpad built successfully.
pause
