@echo off
title VicVPN Installer
echo VicVPN Installer
echo ====================
echo.
echo Requesting administrator privileges...
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-VicVPN.ps1" -DesktopIcon
if %ERRORLEVEL% neq 0 (
    echo.
    echo Installation failed with code %ERRORLEVEL%.
    pause
)
