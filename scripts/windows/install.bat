@echo off
:: Check for administrative privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Requesting administrative privileges...
    powershell -Command "Start-Process '%0' -Verb RunAs"
    exit /b
)

:: Run the PowerShell script with the Bypass flag
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_tools.ps1"

echo.
echo Setup process finished.
pause
