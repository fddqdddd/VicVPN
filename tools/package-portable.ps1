param(
    [string]$BuildDir = "build-mingw",
    [string]$OutDir = "dist\VicVPN-portable"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root $BuildDir
$exe = Join-Path $build "VicVPN.exe"

if (-not (Test-Path $exe)) {
    throw "VicVPN.exe not found. Run build-mingw.bat first."
}

$windeployqt = Join-Path $env:MSYSTEM_PREFIX "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    $windeployqt = "C:\msys64\ucrt64\bin\windeployqt.exe"
}
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found (set MSYSTEM_PREFIX or install Qt6 tools)"
}

$out = Join-Path $root $OutDir
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

Copy-Item $exe $out -Force
Copy-Item (Join-Path $build "styles") (Join-Path $out "styles") -Recurse -Force

$coreSrc = Join-Path $build "core"
$coreDst = Join-Path $out "core"
if (-not (Test-Path (Join-Path $coreSrc "xray.exe"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\fetch-core.ps1") -OutDir $coreSrc
}
if (-not (Test-Path (Join-Path $coreSrc "wintun.dll"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\ensure-wintun.ps1") -OutDir $coreSrc
}
Copy-Item $coreSrc $coreDst -Recurse -Force

$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$null = & $windeployqt (Join-Path $out "VicVPN.exe") --no-translations --compiler-runtime 2>&1
$ErrorActionPreference = $prevEap

Copy-Item (Join-Path $root "LICENSE") $out -Force
Copy-Item (Join-Path $root "docs\USER.ru.md") (Join-Path $out "README.txt") -Force -ErrorAction SilentlyContinue

$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $root
$zip = Join-Path $root "dist\VicVPN-windows-$version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip -Force

Write-Host "[OK] Portable: $out"
Write-Host "[OK] ZIP: $zip"
Get-ChildItem $out | Format-Table Name, Length
