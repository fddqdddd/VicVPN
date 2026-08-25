param(
    [string]$BuildDir = "build-mingw",
    [string]$OutDir = "dist\VicVPN-portable"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$build = Join-Path $projectRoot $BuildDir
$exe = Join-Path $build "VicVPN.exe"

if (-not (Test-Path $exe)) {
    throw "VicVPN.exe not found. Run build-mingw.bat first."
}

$windeployqt = $null
$qtRoot = $null
foreach ($prefix in @($env:MSYSTEM_PREFIX, "C:\msys64\ucrt64", "$env:RUNNER_TEMP\msys64\ucrt64")) {
    if (-not $prefix) { continue }
    $candidateRoot = $prefix
    if ($candidateRoot.StartsWith("/")) {
        $candidateRoot = "C:\msys64" + ($candidateRoot -replace "/", "\")
    }
    $candidate = Join-Path $candidateRoot "bin\windeployqt.exe"
    if (Test-Path $candidate) {
        $windeployqt = $candidate
        $qtRoot = $candidateRoot
        break
    }
}
if (-not $windeployqt) {
    $found = (& where.exe windeployqt 2>$null | Select-Object -First 1)
    if ($found) {
        $windeployqt = $found.Trim()
        $qtRoot = Split-Path (Split-Path $windeployqt -Parent) -Parent
    }
}
if (-not $windeployqt -or -not (Test-Path $windeployqt)) {
    throw "windeployqt not found (install mingw-w64-ucrt-x86_64-qt6-tools)"
}

Write-Host "windeployqt: $windeployqt"
Write-Host "Qt root: $qtRoot"

$out = Join-Path $projectRoot $OutDir
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

Copy-Item $exe $out -Force
Copy-Item (Join-Path $build "styles") (Join-Path $out "styles") -Recurse -Force

$coreSrc = Join-Path $build "core"
$coreDst = Join-Path $out "core"
if (-not (Test-Path (Join-Path $coreSrc "xray.exe"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $projectRoot "tools\fetch-core.ps1") -OutDir $coreSrc
}
if (-not (Test-Path (Join-Path $coreSrc "wintun.dll"))) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $projectRoot "tools\ensure-wintun.ps1") -OutDir $coreSrc
}
Copy-Item $coreSrc $coreDst -Recurse -Force

Write-Host "Running windeployqt..."
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$deployOutput = & $windeployqt (Join-Path $out "VicVPN.exe") --no-translations --compiler-runtime 2>&1
$ErrorActionPreference = $prevEap
Write-Host ($deployOutput | Out-String)

$requiredDlls = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Svg.dll")
$missing = @()
foreach ($dll in $requiredDlls) {
    if (-not (Test-Path (Join-Path $out $dll))) {
        $missing += $dll
    }
}

if ($missing.Count -gt 0) {
    Write-Warning "windeployqt did not copy: $($missing -join ', ')"
    Write-Host "Attempting manual Qt DLL copy from $qtRoot..."

    $qtDllDir = Join-Path $qtRoot "bin"
    foreach ($dll in $missing) {
        $src = Join-Path $qtDllDir $dll
        if (Test-Path $src) {
            Copy-Item $src $out -Force
            Write-Host "  [fixed] $dll"
        } else {
            Write-Warning "  $dll not found in $qtDllDir either"
        }
    }

    $platformDir = Join-Path $out "platforms"
    if (-not (Test-Path (Join-Path $platformDir "qwindows.dll"))) {
        $platSrc = Join-Path $qtRoot "plugins\platforms\qwindows.dll"
        if (Test-Path $platSrc) {
            New-Item -ItemType Directory -Force -Path $platformDir | Out-Null
            Copy-Item $platSrc $platformDir -Force
            Write-Host "  [fixed] platforms\qwindows.dll"
        }
    }

    foreach ($dll in $requiredDlls) {
        if (-not (Test-Path (Join-Path $out $dll))) {
            throw "Required Qt DLL missing: $dll"
        }
    }
}

$finalDlls = Get-ChildItem $out -Filter "*.dll" | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB)}}
Write-Host "DLLs in package:"
$finalDlls | Format-Table -AutoSize

Copy-Item (Join-Path $projectRoot "LICENSE") $out -Force
Copy-Item (Join-Path $projectRoot "docs\USER.ru.md") (Join-Path $out "README.txt") -Force -ErrorAction SilentlyContinue

$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $projectRoot
$zip = Join-Path $projectRoot "dist\VicVPN-windows-$version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip -Force

Write-Host "[OK] Portable: $out"
Write-Host "[OK] ZIP: $zip"
Get-ChildItem $out | Format-Table Name, Length
