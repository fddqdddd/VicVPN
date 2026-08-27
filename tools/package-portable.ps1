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

$finalDlls = Get-ChildItem $out -Filter "*.dll" -Recurse | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB)}}
Write-Host "DLLs in package:"
$finalDlls | Format-Table -AutoSize

$systemDlls = @(
    "KERNEL32.dll","USER32.dll","GDI32.dll","ADVAPI32.dll","SHELL32.dll","WS2_32.dll",
    "IPHLPAPI.DLL","ole32.dll","oleaut32.dll","comctl32.dll","comdlg32.dll","shlwapi.dll",
    "version.dll","winmm.dll","dwmapi.dll","uxtheme.dll","ntdll.dll","AUTHZ.dll","MPR.dll",
    "NETAPI32.dll","USERENV.dll","d3d11.dll","d3d12.dll","DWrite.dll","dxgi.dll","DNSAPI.dll",
    "Secur32.dll","WINHTTP.dll","bcrypt.dll","CRYPT32.dll","ncrypt.dll","IMM32.dll",
    "SETUPAPI.dll","SHCORE.dll","WTSAPI32.dll","d3d9.dll","MSWSock.dll","WinTypes.dll",
    "WindowsCodecs.dll","HID.dll","UIAnimation.dll","dcomp.dll","dwmapi.dll",
    "propsys.dll","acemapi.dll","cfgmgr32.dll","clbcatq.dll","comsvcs.dll",
    "devobj.dll","dui70.dll","duser.dll","explorerframe.dll","gdiplus.dll",
    "globpath.dll","icm32.dll","imm32.dll","mfc42u.dll","mpr.dll","msacm32.dll",
    "msasn1.dll","mscms.dll","mscomctl.ocx","msimg32.dll","mspaint.exe",
    "msvcr100.dll","msvcr110.dll","msvcr120.dll","msvcr80.dll","msvcr90.dll",
    "netutils.dll","npmproxy.dll","profapi.dll","psapi.dll","rasapi32.dll",
    "rasman.dll","rpcrt4.dll","rtutils.dll","samcli.dll","samlib.dll",
    "schannel.dll","secur32.dll","sensapi.dll","setupapi.dll","shell32.dll",
    "shlwapi.dll","sndvolsso.dll","Stdlib.dll","sxs.dll","tpmvsc.dll",
    "tzres.dll","ucrtbase.dll","urlmon.dll","userenv.dll","usp10.dll",
    "virtdisk.dll","wintrust.dll","wkscli.dll","wldap32.dll","wtsapi32.dll",
    "xmllite.dll"
)

Write-Host "`nResolving transitive DLL dependencies..."
$msysBin = Join-Path $qtRoot "bin"
$maxDepth = 4

function Get-NonSystemDeps([string]$FilePath) {
    $output = objdump -x $FilePath 2>&1
    return @($output | Select-String "DLL Name" | ForEach-Object { ($_ -split "DLL Name:\s+")[1].Trim() } | Where-Object {
        ($_ -notin $systemDlls) -and ($_ -notmatch "^api-ms-win-")
    })
}

# Include the main exe as a seed so its direct deps are resolved first
$seeds = @((Join-Path $out "VicVPN.exe"))

for ($depth = 0; $depth -lt $maxDepth; $depth++) {
    $dlls = Get-ChildItem $out -Filter "*.dll" -Recurse
    $roots = @($seeds) + @($dlls | ForEach-Object { $_.FullName })
    $seeds = @()
    $added = 0
    foreach ($file in $roots) {
        $deps = Get-NonSystemDeps $file
        foreach ($dep in $deps) {
            $target = Join-Path $out $dep
            if (Test-Path $target) { continue }
            $src = Join-Path $msysBin $dep
            if (Test-Path $src) {
                Copy-Item $src $out -Force
                Write-Host "  [depth=$depth] $dep"
                $added++
                $seeds += $target
            } else {
                $winSys = Join-Path $env:SystemRoot\System32 $dep
                if (-not (Test-Path $winSys)) {
                    Write-Warning "  $dep not found in MSYS2 or System32"
                }
            }
        }
    }
    if ($added -eq 0) { break }
    Write-Host "  depth ${depth}: ${added} DLLs added"
}

$finalDlls2 = Get-ChildItem $out -Filter "*.dll" -Recurse | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB)}}
Write-Host "`nFinal DLLs in package:"
$finalDlls2 | Format-Table -AutoSize

# Guarantee the sqlite runtime DLL is present
$exeDeps = Get-NonSystemDeps (Join-Path $out "VicVPN.exe")
$exeDeps | ForEach-Object {
    if (-not (Test-Path (Join-Path $out $_))) {
        $src = Join-Path $msysBin $_
        if (Test-Path $src) {
            Copy-Item $src $out -Force
            Write-Host "[OK] final: $_ copied"
        } else {
            throw "Required dependency of VicVPN.exe missing: $_"
        }
    }
}

Copy-Item (Join-Path $projectRoot "LICENSE") $out -Force
Copy-Item (Join-Path $projectRoot "docs\USER.ru.md") (Join-Path $out "README.txt") -Force -ErrorAction SilentlyContinue

$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $projectRoot
$zip = Join-Path $projectRoot "dist\VicVPN-windows-$version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip -Force

Write-Host "[OK] Portable: $out"
Write-Host "[OK] ZIP: $zip"
Get-ChildItem $out | Format-Table Name, Length
