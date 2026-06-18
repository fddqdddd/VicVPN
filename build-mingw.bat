@echo off
setlocal EnableExtensions
chcp 65001 >nul 2>&1

cd /d "%~dp0"

set "PATH=C:\msys64\ucrt64\bin;%PATH%"

where gcc >nul 2>&1
if errorlevel 1 (
    echo [Ошибка] gcc не найден. Добавьте в PATH: C:\msys64\ucrt64\bin
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [Ошибка] cmake не найден.
    exit /b 1
)

echo Сборка: %CD%

if not exist "resources\icons\vicvpn.ico" (
    echo Генерация иконки...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-icon.ps1
)

cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -B build-mingw
if errorlevel 1 goto :fail

cmake --build build-mingw -j
if errorlevel 1 goto :fail

if not exist "build-mingw\core\sing-box.exe" (
    echo Скачивание ядер и библиотек...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-all-deps.ps1
    if errorlevel 1 goto :fail
)

if not exist "build-mingw\core\wintun.dll" (
    echo [Ошибка] wintun.dll не найден в build-mingw\core — TUN не будет работать.
    exit /b 1
)

if exist "build-mingw\VicVPN.exe" (
    echo.
    echo [OK] build-mingw\VicVPN.exe
    echo Запуск: build-mingw\VicVPN.exe
) else (
    echo [Ошибка] exe не найден.
    exit /b 1
)
exit /b 0

:fail
echo Сборка не удалась.
exit /b 1
