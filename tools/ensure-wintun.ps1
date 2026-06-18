param(
    [string]$OutDir = "build-mingw\core"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if ([System.IO.Path]::IsPathRooted($OutDir)) {
    $out = $OutDir
} else {
    $out = Join-Path $root $OutDir
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

$wintun = Join-Path $out "wintun.dll"
if (Test-Path $wintun) {
    Write-Host "[OK] wintun.dll already exists"
    exit 0
}

function Copy-WintunFromExtract($extractDir) {
    $src = Join-Path $extractDir "wintun.dll"
    if (-not (Test-Path $src)) { return $false }
    Copy-Item $src $wintun -Force
    $license = Join-Path $extractDir "LICENSE-wintun.txt"
    if (Test-Path $license) {
        Copy-Item $license (Join-Path $out "LICENSE-wintun.txt") -Force
    }
    return $true
}

# 1) Offline bundle in repo
$bundled = Join-Path $root "third_party\wintun\wintun.dll"
if (Test-Path $bundled) {
    Copy-Item $bundled $wintun -Force
    Write-Host "[OK] wintun.dll copied from third_party"
    exit 0
}

# 2) Cached Xray zip / extract dir
$extract = Join-Path $env:TEMP "xray_extract"
if (Copy-WintunFromExtract $extract) {
    Write-Host "[OK] wintun.dll copied from TEMP\xray_extract"
    exit 0
}

$xrayZip = Join-Path $env:TEMP "xray.zip"
if (Test-Path $xrayZip) {
    if (-not (Test-Path $extract)) {
        Expand-Archive -Path $xrayZip -DestinationPath $extract -Force
    }
    if (Copy-WintunFromExtract $extract) {
        Write-Host "[OK] wintun.dll extracted from cached xray.zip"
        exit 0
    }
}

# 3) Download Xray zip from GitHub (includes wintun.dll)
Write-Host "Downloading Xray zip for wintun.dll..."
$headers = @{ "User-Agent" = "VicVPN" }
if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)" }
$rel = Invoke-RestMethod -Uri "https://api.github.com/repos/XTLS/Xray-core/releases/latest" -Headers $headers
$asset = $rel.assets | Where-Object { $_.name -match "windows-64.zip" } | Select-Object -First 1
if (-not $asset) { throw "Xray windows-64.zip not found on GitHub" }

Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $xrayZip -Headers @{ "User-Agent" = "VicVPN" }
if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $xrayZip -DestinationPath $extract -Force

if (-not (Copy-WintunFromExtract $extract)) {
    throw "wintun.dll not found inside Xray zip"
}

# Save offline copy for next time
$bundleDir = Join-Path $root "third_party\wintun"
New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
Copy-Item $wintun (Join-Path $bundleDir "wintun.dll") -Force

Write-Host "[OK] wintun.dll installed to $out"
