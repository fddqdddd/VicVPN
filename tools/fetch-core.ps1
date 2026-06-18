param(
    [string]$OutDir = "build-mingw\core"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $root $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$bundled = Join-Path $root "third_party\cores\windows-amd64"
if (Test-Path (Join-Path $bundled "sing-box.exe")) {
    Write-Host "[OK] Using bundled cores from third_party\cores\windows-amd64"
    robocopy $bundled $OutDir /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
    if ($LASTEXITCODE -lt 8) {
        Get-ChildItem $OutDir | Format-Table Name, Length
        exit 0
    }
}

# Fallback: download individually (legacy script)

function Get-LatestReleaseAsset($repo, $pattern) {
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers @{ "User-Agent" = "VicVPN" }
    $asset = $rel.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $asset) { throw "Asset not found: $repo $pattern" }
    return $asset
}

function Download-Asset($asset, $dest) {
    Write-Host "Downloading $($asset.name)..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $dest -Headers @{ "User-Agent" = "VicVPN" }
}

# sing-box — primary TUN engine (Outline-style)
$singboxExe = Join-Path $OutDir "sing-box.exe"
if (-not (Test-Path $singboxExe)) {
    $sbAsset = Get-LatestReleaseAsset "SagerNet/sing-box" "windows-amd64"
    $sbZip = Join-Path $env:TEMP "sing-box.zip"
    Download-Asset $sbAsset $sbZip
    $sbExtract = Join-Path $env:TEMP "singbox_extract"
    if (Test-Path $sbExtract) { Remove-Item $sbExtract -Recurse -Force }
    Expand-Archive -Path $sbZip -DestinationPath $sbExtract -Force
    $bin = Get-ChildItem -Path $sbExtract -Filter "sing-box*.exe" -Recurse | Select-Object -First 1
    if (-not $bin) { throw "sing-box binary not found in archive" }
    Copy-Item $bin.FullName $singboxExe -Force
}

# Xray-core (legacy / passthrough)
$xrayExe = Join-Path $OutDir "xray.exe"
if (-not (Test-Path $xrayExe)) {
    $xrayAsset = Get-LatestReleaseAsset "XTLS/Xray-core" "windows-64.zip"
    $xrayZip = Join-Path $env:TEMP "xray.zip"
    Download-Asset $xrayAsset $xrayZip
    $extract = Join-Path $env:TEMP "xray_extract"
    if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    Expand-Archive -Path $xrayZip -DestinationPath $extract -Force
    Copy-Item (Join-Path $extract "xray.exe") $xrayExe -Force
}

# wintun.dll — always ensure (TUN requires it even if xray.exe already exists)
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot "ensure-wintun.ps1") -OutDir $OutDir

# geo files
$geoip = Join-Path $OutDir "geoip.dat"
$geosite = Join-Path $OutDir "geosite.dat"
if (-not (Test-Path $geoip)) {
    Invoke-WebRequest -Uri "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geoip.dat" -OutFile $geoip
}
if (-not (Test-Path $geosite)) {
    Invoke-WebRequest -Uri "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geosite.dat" -OutFile $geosite
}

# Hysteria2 (standalone exe, not zip)
if (-not (Test-Path (Join-Path $OutDir "hysteria.exe"))) {
    $hyAsset = Get-LatestReleaseAsset "apernet/hysteria" "hysteria-windows-amd64\.exe$"
    $hyDest = Join-Path $OutDir "hysteria.exe"
    Download-Asset $hyAsset $hyDest
}

# tun2socks — Outline / Uncle Vanya style TUN (faster than Xray built-in TUN on Windows)
$t2sExe = Join-Path $OutDir "tun2socks.exe"
if (-not (Test-Path $t2sExe)) {
    $t2sAsset = Get-LatestReleaseAsset "xjasonlyu/tun2socks" "tun2socks-windows-amd64"
    $t2sZip = Join-Path $env:TEMP "tun2socks.zip"
    Download-Asset $t2sAsset $t2sZip
    $t2sExtract = Join-Path $env:TEMP "tun2socks_extract"
    if (Test-Path $t2sExtract) { Remove-Item $t2sExtract -Recurse -Force }
    Expand-Archive -Path $t2sZip -DestinationPath $t2sExtract -Force
    $bin = Get-ChildItem -Path $t2sExtract -Filter "tun2socks*.exe" -Recurse | Select-Object -First 1
    if (-not $bin) { throw "tun2socks binary not found in archive" }
    Copy-Item $bin.FullName $t2sExe -Force
}

Write-Host "[OK] Cores installed to $OutDir"
Get-ChildItem $OutDir | Format-Table Name, Length
