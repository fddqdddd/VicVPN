param(
    [switch]$Force,
    [string]$Root = ""
)

$ErrorActionPreference = "Stop"
if (-not $Root) { $Root = Split-Path $PSScriptRoot -Parent }

function Get-LatestReleaseAsset($repo, $pattern) {
    $headers = @{ "User-Agent" = "VicVPN" }
    if ($env:GITHUB_TOKEN) {
        $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)"
    }
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" `
        -Headers $headers
    $asset = $rel.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $asset) { throw "Asset not found: $repo / $pattern" }
    return $asset
}

function Download-File($url, $dest) {
    $dir = Split-Path $dest -Parent
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Write-Host "  -> $dest"

    $headers = @{ "User-Agent" = "VicVPN/0.1.0-beta" }
    $urls = @($url)
    if ($url -match '^https://github\.com/') {
        $urls += "https://ghfast.top/$url"
        $urls += "https://mirror.ghproxy.com/$url"
    }

    $max = 3
    foreach ($tryUrl in $urls) {
        for ($i = 1; $i -le $max; $i++) {
            try {
                [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                Invoke-WebRequest -Uri $tryUrl -OutFile $dest -Headers $headers -UseBasicParsing -TimeoutSec 180
                if ((Test-Path $dest) -and (Get-Item $dest).Length -ge 1024) { return }
            } catch {
                Write-Host "  mirror attempt $i failed"
            }
            try {
                curl.exe -fsSL --retry 2 --retry-delay 2 -A "VicVPN" -o $dest $tryUrl
                if ((Test-Path $dest) -and (Get-Item $dest).Length -ge 1024) { return }
            } catch { }
            Start-Sleep -Seconds (2 * $i)
        }
    }
    throw "Download failed: $url"
}

function Ensure-Dir($path) {
    New-Item -ItemType Directory -Force -Path $path | Out-Null
}

function Extract-Zip($zip, $dest) {
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $dest -Force
}

function Copy-IfExists($src, $dest) {
    if (Test-Path $src) {
        Copy-Item $src $dest -Force
        return $true
    }
    return $false
}

function Fetch-WindowsCores($outDir) {
    Write-Host "`n=== Windows cores -> $outDir ==="
    Ensure-Dir $outDir

    $jobs = @(
        @{ Name = "sing-box.exe"; Repo = "SagerNet/sing-box"; Pattern = "windows-amd64\.zip$"; Zip = $true; Bin = "sing-box*.exe" },
        @{ Name = "xray.exe"; Repo = "XTLS/Xray-core"; Pattern = "windows-64\.zip$"; Zip = $true; Bin = "xray.exe" },
        @{ Name = "hysteria.exe"; Repo = "apernet/hysteria"; Pattern = "hysteria-windows-amd64\.exe$"; Zip = $false },
        @{ Name = "tun2socks.exe"; Repo = "xjasonlyu/tun2socks"; Pattern = "tun2socks-windows-amd64"; Zip = $true; Bin = "tun2socks*.exe" }
    )

    foreach ($j in $jobs) {
        $dest = Join-Path $outDir $j.Name
        if ((Test-Path $dest) -and -not $Force) {
            Write-Host "[skip] $($j.Name)"
            continue
        }
        $asset = Get-LatestReleaseAsset $j.Repo $j.Pattern
        $tmp = Join-Path $env:TEMP ("vicvpn-" + $asset.name)
        Download-File $asset.browser_download_url $tmp
        if ($j.Zip) {
            $extract = Join-Path $env:TEMP "vicvpn-extract-$($j.Name)"
            Extract-Zip $tmp $extract
            $bin = Get-ChildItem $extract -Filter $j.Bin -Recurse | Select-Object -First 1
            if (-not $bin) { throw "Binary not found in $($j.Name) archive" }
            Copy-Item $bin.FullName $dest -Force
        } else {
            Copy-Item $tmp $dest -Force
        }
        Write-Host "[OK] $($j.Name)"
    }

    # wintun.dll from Xray zip
    $wintun = Join-Path $outDir "wintun.dll"
    if (-not (Test-Path $wintun) -or $Force) {
        $asset = Get-LatestReleaseAsset "XTLS/Xray-core" "windows-64\.zip$"
        $zip = Join-Path $env:TEMP "vicvpn-xray-wintun.zip"
        Download-File $asset.browser_download_url $zip
        $extract = Join-Path $env:TEMP "vicvpn-xray-wintun"
        Extract-Zip $zip $extract
        $src = Join-Path $extract "wintun.dll"
        if (-not (Test-Path $src)) { throw "wintun.dll not in Xray zip" }
        Copy-Item $src $wintun -Force
        $bundle = Join-Path $Root "third_party\wintun"
        Ensure-Dir $bundle
        Copy-Item $src (Join-Path $bundle "wintun.dll") -Force
        $lic = Join-Path $extract "LICENSE-wintun.txt"
        if (Test-Path $lic) {
            Copy-Item $lic (Join-Path $outDir "LICENSE-wintun.txt") -Force
            Copy-Item $lic (Join-Path $bundle "LICENSE-wintun.txt") -Force
        }
        Write-Host "[OK] wintun.dll"
    } else {
        Write-Host "[skip] wintun.dll"
    }

    foreach ($geo in @("geoip.dat", "geosite.dat")) {
        $dest = Join-Path $outDir $geo
        if ((Test-Path $dest) -and -not $Force) { Write-Host "[skip] $geo"; continue }
        Download-File "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/$geo" $dest
        Write-Host "[OK] $geo"
    }
}

function Fetch-LinuxCores($outDir) {
    Write-Host "`n=== Linux cores -> $outDir ==="
    Ensure-Dir $outDir

    $jobs = @(
        @{ Name = "sing-box"; Repo = "SagerNet/sing-box"; Pattern = "linux-amd64\.tar\.gz$"; Gz = $true },
        @{ Name = "xray"; Repo = "XTLS/Xray-core"; Pattern = "linux-64\.zip$"; Zip = $true; Bin = "xray" },
        @{ Name = "hysteria"; Repo = "apernet/hysteria"; Pattern = "hysteria-linux-amd64$"; Raw = $true },
        @{ Name = "tun2socks"; Repo = "xjasonlyu/tun2socks"; Pattern = "tun2socks-linux-amd64"; Zip = $true; Bin = "tun2socks*" }
    )

    foreach ($j in $jobs) {
        $dest = Join-Path $outDir $j.Name
        if ((Test-Path $dest) -and -not $Force) {
            Write-Host "[skip] $($j.Name)"
            continue
        }
        $asset = Get-LatestReleaseAsset $j.Repo $j.Pattern
        $tmp = Join-Path $env:TEMP ("vicvpn-linux-" + $asset.name)
        Download-File $asset.browser_download_url $tmp

        if ($j.Gz) {
            $extract = Join-Path $env:TEMP "vicvpn-gz-$($j.Name)"
            if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
            Ensure-Dir $extract
            tar -xzf $tmp -C $extract
            $bin = Get-ChildItem $extract -Filter "sing-box*" -File -Recurse | Select-Object -First 1
            if (-not $bin) { throw "sing-box not found in tar.gz" }
            Copy-Item $bin.FullName $dest -Force
        } elseif ($j.Zip) {
            $extract = Join-Path $env:TEMP "vicvpn-zip-$($j.Name)"
            Extract-Zip $tmp $extract
            $bin = if ($j.Bin) {
                Get-ChildItem $extract -Filter $j.Bin -Recurse | Select-Object -First 1
            } else {
                Get-ChildItem $extract -File -Recurse | Select-Object -First 1
            }
            if (-not $bin) { throw "$($j.Name) not found in archive" }
            Copy-Item $bin.FullName $dest -Force
        } else {
            Copy-Item $tmp $dest -Force
        }
        Write-Host "[OK] $($j.Name)"
    }

    foreach ($geo in @("geoip.dat", "geosite.dat")) {
        $dest = Join-Path $outDir $geo
        if ((Test-Path $dest) -and -not $Force) { Write-Host "[skip] $geo"; continue }
        Download-File "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/$geo" $dest
        Write-Host "[OK] $geo"
    }
}

function Sync-To($src, $dst) {
    if (-not (Test-Path $src)) { return }
    Ensure-Dir $dst
    robocopy $src $dst /MIR /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed $src -> $dst" }
    Write-Host "[sync] $dst"
}

Write-Host "VicVPN: download all cores and libraries"
$thirdWin = Join-Path $Root "third_party\cores\windows-amd64"
$thirdLin = Join-Path $Root "third_party\cores\linux-amd64"

# Seed Windows bundle from existing build if present
$existingWin = Join-Path $Root "build-mingw\core"
if ((Test-Path $existingWin) -and -not (Test-Path (Join-Path $thirdWin "sing-box.exe"))) {
    Write-Host "Seeding Windows cores from build-mingw\core..."
    Ensure-Dir $thirdWin
    robocopy $existingWin $thirdWin /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
}

Fetch-WindowsCores $thirdWin

try {
    Fetch-LinuxCores $thirdLin
} catch {
    Write-Warning "Linux cores download failed (GitHub may be blocked). CI will fetch on Ubuntu."
    Write-Warning $_.Exception.Message
}

Sync-To $thirdWin (Join-Path $Root "build-mingw\core")
Sync-To $thirdWin (Join-Path $Root "dist\VicVPN-portable\core")

Write-Host "`n[OK] All dependencies downloaded."
Write-Host "Windows: $thirdWin"
Write-Host "Linux:   $thirdLin"
Get-ChildItem $thirdWin | Format-Table Name, @{N='MB';E={[math]::Round($_.Length/1MB,1)}}
Get-ChildItem $thirdLin | Format-Table Name, @{N='MB';E={[math]::Round($_.Length/1MB,1)}}
