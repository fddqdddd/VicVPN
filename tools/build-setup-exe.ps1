param(
    [string]$PortableDir = "dist\VicVPN-portable",
    [string]$OutExe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $root
if (-not $OutExe) {
    $OutExe = Join-Path $root "dist\VicVPN-windows-$version-setup.exe"
}

$portable = Join-Path $root $PortableDir
if (-not (Test-Path (Join-Path $portable "VicVPN.exe"))) {
    & (Join-Path $PSScriptRoot "package-portable.ps1")
    $portable = Join-Path $root $PortableDir
}

$staging = Join-Path $root "dist\setup-staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

robocopy $portable $staging /MIR /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }

Copy-Item (Join-Path $root "installer\Install-VicVPN.ps1") $staging -Force
Set-Content -Path (Join-Path $staging "VERSION.txt") -Value $version -Encoding ASCII -NoNewline

$payloadZip = Join-Path $root "dist\setup-payload.zip"
if (Test-Path $payloadZip) { Remove-Item $payloadZip -Force }
Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $payloadZip -Force

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "csc.exe not found" }

$distDir = Split-Path $OutExe -Parent
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path $OutExe) { Remove-Item $OutExe -Force }

$cs = Join-Path $root "installer\SetupBootstrap.cs"
$manifest = Join-Path $root "installer\admin.manifest"
Write-Host "Compiling setup EXE..."
& $csc /nologo /target:winexe /optimize+ `
    /r:System.IO.Compression.dll `
    /r:System.IO.Compression.FileSystem.dll `
    /r:System.Windows.Forms.dll `
    "/win32manifest:$manifest" `
    "/out:$OutExe" `
    "/resource:$payloadZip,payload" `
    $cs
if ($LASTEXITCODE -ne 0) { throw "csc failed: $LASTEXITCODE" }
if (-not (Test-Path $OutExe)) { throw "Setup EXE was not created" }

$sizeMb = [math]::Round((Get-Item $OutExe).Length / 1MB, 1)
Write-Host "[OK] Setup EXE: $OutExe ($sizeMb MB)"

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $payloadZip -Force -ErrorAction SilentlyContinue
