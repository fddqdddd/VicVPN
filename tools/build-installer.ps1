param(
    [string]$IssFile = "installer\VicVPN.iss"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$iss = Join-Path $root $IssFile
$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $root

$portable = Join-Path $root "dist\VicVPN-portable"
if (-not (Test-Path (Join-Path $portable "VicVPN.exe"))) {
    & (Join-Path $PSScriptRoot "package-portable.ps1")
}

$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($iscc) {
    Write-Host "Building Inno Setup installer..."
    Push-Location (Split-Path $iss -Parent)
    & $iscc "/DMyAppVersion=$version" (Split-Path $iss -Leaf)
    Pop-Location
    $innoOut = Join-Path $root "dist\VicVPN-windows-$version-setup.exe"
    if (Test-Path $innoOut) {
        Write-Host "[OK] Inno installer: $innoOut"
        exit 0
    }
}

Write-Host "Inno Setup not found - building setup EXE..."
& (Join-Path $PSScriptRoot "build-setup-exe.ps1")
