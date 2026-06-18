# VicVPN TUN self-test (run PowerShell as Administrator)

param(
    [string]$BuildDir = "build-mingw"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$core = Join-Path $root "$BuildDir\core"
$xray = Join-Path $core "xray.exe"
$config = Join-Path $env:APPDATA "VZorg\VicVPN\runtime\xray.json"

Write-Host "=== VicVPN TUN verify ==="
Write-Host "Xray:   $xray"
Write-Host "Config: $config"

if (-not (Test-Path $xray)) { throw "xray.exe not found. Run build-mingw.bat first." }
if (-not (Test-Path (Join-Path $core "wintun.dll"))) { throw "wintun.dll missing in core/" }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Host "Admin:  $isAdmin"
if (-not $isAdmin) {
    Write-Warning "TUN routes require administrator. Re-run PowerShell as admin for full test."
}

if (-not (Test-Path $config)) {
    Write-Warning "No runtime xray.json yet. Import a server and connect once in VicVPN."
    $config = Join-Path $core "test-xray.json"
    if (-not (Test-Path $config)) { throw "No config to test." }
}

Get-Process xray -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$logOut = Join-Path $env:TEMP "vicvpn-verify-xray-out.log"
$logErr = Join-Path $env:TEMP "vicvpn-verify-xray-err.log"
Remove-Item $logOut,$logErr -Force -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $xray -ArgumentList @("run", "-c", $config) `
    -WorkingDirectory $core -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $logOut -RedirectStandardError $logErr

Start-Sleep -Seconds 4

$adapter = Get-NetAdapter -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^(vicvpn0|xray0)$' -or $_.InterfaceDescription -match 'Xray Tunnel|Wintun' } |
    Select-Object -First 1

$routes = route print 0.0.0.0 2>$null | Out-String
$log = @()
if (Test-Path $logOut) { $log += Get-Content $logOut -Raw }
if (Test-Path $logErr) { $log += Get-Content $logErr -Raw }
$log = ($log -join "`n")

Write-Host ""
Write-Host "--- Adapter ---"
if ($adapter) {
    Write-Host "[OK] $($adapter.Name) ($($adapter.InterfaceDescription)) ifIndex=$($adapter.ifIndex)"
} else {
    Write-Host "[FAIL] TUN adapter not found"
}

Write-Host ""
Write-Host "--- Routes via TUN (metric 1 should appear) ---"
(netsh interface ipv4 show route | Select-String "0.0.0.0") | ForEach-Object { Write-Host $_.Line.Trim() }

Write-Host ""
Write-Host "--- Xray log (tail) ---"
if ($log) {
    ($log -split "`n" | Select-Object -Last 8) | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "(empty)"
}

$procAlive = -not $p.HasExited
Write-Host ""
Write-Host "Xray running: $procAlive"

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue

if (-not $adapter) { exit 1 }
if (-not $isAdmin) {
    Write-Warning "Adapter exists but routes may not apply without admin."
}
Write-Host "[OK] Basic TUN check passed"
