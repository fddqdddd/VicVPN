@echo off
chcp 65001 >nul
setlocal
set "DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DIR%Install-VicVPN.ps1" -Silent -DesktopIcon %*
exit /b %ERRORLEVEL%
