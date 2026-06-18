@echo off
setlocal EnableExtensions
chcp 65001 >nul 2>&1
cd /d "%~dp0"

call build-mingw.bat
if errorlevel 1 exit /b 1

if not exist "resources\icons\vicvpn.ico" (
    echo Генерация иконки...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-icon.ps1
)

echo.
echo === Portable package ===
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package-portable.ps1
if errorlevel 1 exit /b 1

echo.
echo === Installer ===
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-installer.ps1

echo.
echo [OK] Релиз готов в папке dist\
dir /b dist 2>nul
exit /b 0
