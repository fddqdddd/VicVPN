#Requires -Version 5.1
param(
    [switch]$Silent,
    [switch]$DesktopIcon,
    [switch]$AutoStart,
    [string]$InstallDir = "$env:ProgramFiles\VicVPN"
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Ensure-Admin {
    if (Test-IsAdmin) { return }
    $argList = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`""
    )
    if ($Silent) { $argList += "-Silent" }
    if ($DesktopIcon) { $argList += "-DesktopIcon" }
    if ($AutoStart) { $argList += "-AutoStart" }
    $argList += "-InstallDir", "`"$InstallDir`""
    $proc = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $proc.ExitCode
}

function Write-Setup([string]$Text) {
    if (-not $Silent) { Write-Host $Text }
}

Ensure-Admin

$sourceDir = Split-Path -Parent $PSCommandPath
$version = "0.1.0"
$verFile = Join-Path $sourceDir "VERSION.txt"
if (Test-Path $verFile) {
    $version = (Get-Content $verFile -Raw).Trim()
}

Write-Setup "VicVPN $version -> $InstallDir"

if (Test-Path $InstallDir) {
    Write-Setup "Updating existing installation..."
    Get-Process VicVPN -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$skip = @("Install-VicVPN.ps1", "Uninstall-VicVPN.ps1", "Setup.cmd", "VERSION.txt")
Get-ChildItem -Path $sourceDir -Force | Where-Object { $_.Name -notin $skip } | ForEach-Object {
    if ($_.PSIsContainer) {
        Copy-Item $_.FullName (Join-Path $InstallDir $_.Name) -Recurse -Force
    } else {
        Copy-Item $_.FullName (Join-Path $InstallDir $_.Name) -Force
    }
}

$uninstallScript = @'
#Requires -Version 5.1
param([switch]$Silent)
$ErrorActionPreference = "Stop"
$installDir = Split-Path -Parent $PSCommandPath
Get-Process VicVPN -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$links = @(
    "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\VicVPN.lnk",
    "$env:Public\Desktop\VicVPN.lnk"
)
foreach ($lnk in $links) {
    if (Test-Path $lnk) { Remove-Item $lnk -Force }
}
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "VicVPN" -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\VicVPN" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path $installDir -Recurse -Force
if (-not $Silent) { Write-Host "VicVPN removed." }
'@
Set-Content -Path (Join-Path $InstallDir "Uninstall-VicVPN.ps1") -Value $uninstallScript -Encoding ASCII

$wsh = New-Object -ComObject WScript.Shell
$startMenu = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
New-Item -ItemType Directory -Force -Path $startMenu | Out-Null
$shortcut = $wsh.CreateShortcut((Join-Path $startMenu "VicVPN.lnk"))
$shortcut.TargetPath = Join-Path $InstallDir "VicVPN.exe"
$shortcut.WorkingDirectory = $InstallDir
$shortcut.Description = "VicVPN"
$shortcut.Save()

if ($DesktopIcon) {
    $desk = $wsh.CreateShortcut((Join-Path $env:Public "Desktop\VicVPN.lnk"))
    $desk.TargetPath = Join-Path $InstallDir "VicVPN.exe"
    $desk.WorkingDirectory = $InstallDir
    $desk.Save()
}

if ($AutoStart) {
    Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
        -Name "VicVPN" -Value "`"$($InstallDir)\VicVPN.exe`"" -Type String
}

$uninstallKey = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\VicVPN"
New-Item -Path $uninstallKey -Force | Out-Null
Set-ItemProperty $uninstallKey -Name "DisplayName" -Value "VicVPN"
Set-ItemProperty $uninstallKey -Name "DisplayVersion" -Value $version
Set-ItemProperty $uninstallKey -Name "Publisher" -Value "VZorg"
Set-ItemProperty $uninstallKey -Name "InstallLocation" -Value $InstallDir
Set-ItemProperty $uninstallKey -Name "UninstallString" `
    -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$InstallDir\Uninstall-VicVPN.ps1`""
Set-ItemProperty $uninstallKey -Name "DisplayIcon" -Value (Join-Path $InstallDir "VicVPN.exe")
Set-ItemProperty $uninstallKey -Name "NoModify" -Value 1 -Type DWord
Set-ItemProperty $uninstallKey -Name "NoRepair" -Value 1 -Type DWord

Write-Setup "[OK] VicVPN installed."
Write-Setup "Run VicVPN as Administrator for VPN (TUN)."

if (-not $Silent) {
    $run = Read-Host "Launch VicVPN now? (Y/n)"
    if ($run -eq "" -or $run -eq "Y" -or $run -eq "y") {
        Start-Process (Join-Path $InstallDir "VicVPN.exe")
    }
}
