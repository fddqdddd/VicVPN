param(
    [string]$PortableDir = "dist\VicVPN-portable",
    [string]$OutZip = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$version = & (Join-Path $PSScriptRoot "get-version.ps1") -Root $projectRoot
if (-not $OutZip) {
    $OutZip = Join-Path $projectRoot "dist\VicVPN-windows-$version-setup.zip"
}

$portable = Join-Path $projectRoot $PortableDir
if (-not (Test-Path (Join-Path $portable "VicVPN.exe"))) {
    & (Join-Path $PSScriptRoot "package-portable.ps1")
    $portable = Join-Path $projectRoot $PortableDir
}

$staging = Join-Path $projectRoot "dist\setup-staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

robocopy $portable $staging /MIR /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }

Copy-Item (Join-Path $projectRoot "installer\Install-VicVPN.ps1") $staging -Force
Copy-Item (Join-Path $projectRoot "installer\Install-VicVPN.cmd") $staging -Force
Set-Content -Path (Join-Path $staging "VERSION.txt") -Value $version -Encoding ASCII -NoNewline

$distDir = Split-Path $OutZip -Parent
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path $OutZip) { Remove-Item $OutZip -Force }

Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $OutZip -Force

$sizeMb = [math]::Round((Get-Item $OutZip).Length / 1MB, 1)
Write-Host "[OK] Installer ZIP: $OutZip ($sizeMb MB)"

Write-Host "`nContents:"
Get-ChildItem $staging | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB)}} | Format-Table -AutoSize

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
