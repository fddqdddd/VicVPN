# Creates a GitHub release for the current build and uploads the installer assets.
# Auth is taken from the git credential helper, same as `git push`.

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$repo = "fddqdddd/VicVPN"

$token = (("protocol=https`nhost=github.com`n`n" |
        & (Join-Path $projectRoot "tools\MinGit\cmd\git.exe") credential fill 2>$null) |
        Where-Object { $_ -like "password=*" }) -replace "^password=", ""
if (-not $token) { throw "no GitHub credential found" }

$auth = @{ "Authorization" = "Bearer $token"; "X-GitHub-Api-Version" = "2022-11-28"; "User-Agent" = "VicVPN" }

$version = (& (Join-Path $PSScriptRoot "get-version.ps1") -Root $projectRoot).Trim()
$tag = "v$version"
$sha = (& (Join-Path $projectRoot "tools\MinGit\cmd\git.exe") -C $projectRoot rev-parse HEAD).Trim()

$assets = @(
    "dist\VicVPN-windows-$version-setup.zip",
    "dist\VicVPN-windows-$version-portable.zip"
)
foreach ($a in $assets) {
    if (-not (Test-Path (Join-Path $projectRoot $a))) { throw "missing asset: $a" }
}

$body = @{
    tag_name         = $tag
    target_commitish = "main"
    name             = "VicVPN $version"
    prerelease       = ($version -match "-")
    draft            = $false
    body             = @"
VicVPN $version

Build ``$sha``. Verify **Help -> About** shows the same build id before you install.

### Files
| Package | Use |
| --- | --- |
| ``VicVPN-$version-setup-win64.zip`` | Installer, **Auto-updates from GitHub Releases** |
| ``VicVPN-$version-portable-win64.zip`` | Portable, runs next to the unpacked folder |

### Installing
1. Unzip the package.
2. Run ``Install-VicVPN.cmd`` (setup) or ``VicVPN.exe`` (portable).
3. Windows may ask for permission — allow it.

### Self-update
The app checks GitHub Releases shortly after start, and manually from **Help ->
About**. An update is downloaded to ``runtime/updates/`` and applied by a
detached script; the application relaunches itself when it is done.
"@
} | ConvertTo-Json -Depth 5

# Sending bytes keeps the API from seeing a mis-encoded body.
$rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/releases" `
    -Headers $auth -Body ([Text.Encoding]::UTF8.GetBytes($body)) -ContentType "application/json" -TimeoutSec 120
# upload_url arrives as ".../assets{?name,label}"; strip the template.
$uploadBase = ($rel.upload_url -replace '\{\?.*\}$', '') + "?name="
Write-Host "[OK] release id=$($rel.id) tag=$($rel.tag_name) url=$($rel.html_url)"

foreach ($a in $assets) {
    $name = Split-Path $a -Leaf
    $size = (Get-Item (Join-Path $projectRoot $a)).Length
    Write-Host "   uploading $name ($([math]::Round($size/1MB,2)) MB) ..."

    # The API wants the raw multipart body, not a PS upload form.
    $boundary = [guid]::NewGuid().ToString()
    $fileBytes = [IO.File]::ReadAllBytes((Join-Path $projectRoot $a))
    $head = "--$boundary`r`nContent-Disposition: form-data; name=`"asset`"; filename=`"$name`"`r`nContent-Type: application/zip`r`n`r`n"
    $tail = "`r`n--$boundary--`r`n"
    $payload = [byte[]]::new($head.Length + $fileBytes.Length + $tail.Length)
    [Buffer]::BlockCopy([Text.Encoding]::UTF8.GetBytes($head), 0, $payload, 0, $head.Length)
    [Buffer]::BlockCopy($fileBytes, 0, $payload, $head.Length, $fileBytes.Length)
    [Buffer]::BlockCopy([Text.Encoding]::UTF8.GetBytes($tail), 0, $payload, $head.Length + $fileBytes.Length, $tail.Length)

    $asset = Invoke-RestMethod -Method Post -Uri "$uploadBase$name" -Headers $auth `
        -Body $payload -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 1800
    Write-Host "   [OK] $($asset.name) id=$($asset.id)"
}

Write-Host "[OK] release ready: $($rel.html_url)"