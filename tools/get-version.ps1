param([string]$Root = (Split-Path $PSScriptRoot -Parent))

$cmake = Join-Path $Root "CMakeLists.txt"
if (-not (Test-Path $cmake)) { return "0.1.0" }

$line = Get-Content $cmake | Where-Object { $_ -match 'project\(VicVPN VERSION ' } | Select-Object -First 1
if ($line -match 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { return "$($Matches[1])-beta" }
return "0.1.0"
