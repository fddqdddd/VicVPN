param(
    [switch]$NoBuild,
    [switch]$SkipArchive
)

# Builds, packages and stages the release artifacts with channel-correct names.
# Produces dist\VicVPN-windows-<version>-setup.zip and -portable.zip plus copies
# in Dowload_Vic_VPN named VicVPN-<version>-{setup,portable}-win64.zip.

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$publishDir = Join-Path $projectRoot "Dowload_Vic_VPN"

function Get-Version {
    return (& (Join-Path $PSScriptRoot "get-version.ps1") -Root $projectRoot).Trim()
}

$version = Get-Version
Write-Host "== VicVPN $version =="

if (-not $NoBuild) {
    $ucrt = "C:\msys64\ucrt64"
    $env:PATH = "$ucrt\bin;$env:PATH"
    Write-Host "-- configure"
    & cmake -G "MinGW Makefiles" `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_PREFIX_PATH=$($ucrt -replace '\\','/')" `
        -DVICVPN_BUILD_TESTS=OFF `
        -B build-mingw | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

    Write-Host "-- build"
    & cmake --build build-mingw -j8 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

Write-Host "-- package"
& (Join-Path $PSScriptRoot "package-portable.ps1") | Out-Host
& (Join-Path $PSScriptRoot "build-installer.ps1") | Out-Host

$setupSrc = Join-Path $projectRoot "dist\VicVPN-windows-$version-setup.zip"
$portableSrc = Join-Path $projectRoot "dist\VicVPN-windows-$version-portable.zip"
foreach ($p in @($setupSrc, $portableSrc)) {
    if (-not (Test-Path $p)) { throw "missing artifact: $p" }
}

if (-not $SkipArchive) {
    New-Item -ItemType Directory -Force -Path $publishDir | Out-Null

    # Older builds keep their names, which only confuses people about which one
    # they are running. Replace them instead of accumulating.
    Get-ChildItem $publishDir -Filter "VicVPN-*-win64.zip" -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host "   removing stale $($_.Name)"
            Remove-Item $_.FullName -Force
        }

    $setupDst = Join-Path $publishDir "VicVPN-$version-setup-win64.zip"
    $portableDst = Join-Path $publishDir "VicVPN-$version-portable-win64.zip"
    Copy-Item $setupSrc $setupDst -Force
    Copy-Item $portableSrc $portableDst -Force

    Write-Host "-- published"
    Get-ChildItem $publishDir -Filter "*.zip" |
        Select-Object Name, @{N = 'MB'; E = { [math]::Round($_.Length / 1MB, 2) }} |
        Format-Table -AutoSize | Out-Host
}

Write-Host "[OK] $version ready for a GitHub release."
Write-Host "    setup:    $setupSrc"
Write-Host "    portable: $portableSrc"