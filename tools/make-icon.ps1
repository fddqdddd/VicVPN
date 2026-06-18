$ErrorActionPreference = "Stop"
$outDir = Join-Path $PSScriptRoot "..\resources\icons"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Add-Type -AssemblyName System.Drawing

function New-VicVpnBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::FromArgb(18, 20, 28))

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.Rectangle]::new(0, 0, $size, $size),
        [System.Drawing.Color]::FromArgb(88, 101, 242),
        [System.Drawing.Color]::FromArgb(56, 189, 248),
        45)
    $margin = [int]($size * 0.12)
    $g.FillEllipse($brush, $margin, $margin, $size - 2 * $margin, $size - 2 * $margin)

    $fontSize = [int]($size * 0.42)
    $font = New-Object System.Drawing.Font("Segoe UI", $fontSize, [System.Drawing.FontStyle]::Bold)
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = [System.Drawing.RectangleF]::new(0, 0, $size, $size)
    $g.DrawString("V", $font, [System.Drawing.Brushes]::White, $rect, $sf)

    $g.Dispose()
    $brush.Dispose()
    return $bmp
}

$pngPath = Join-Path $outDir "vicvpn.png"
$icoPath = Join-Path $outDir "vicvpn.ico"
$bmp256 = New-VicVpnBitmap 256
$bmp256.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)

$icon = [System.Drawing.Icon]::FromHandle((New-VicVpnBitmap 64).GetHicon())
$fs = [System.IO.File]::Create($icoPath)
$icon.Save($fs)
$fs.Close()

Write-Host "[OK] $pngPath"
Write-Host "[OK] $icoPath"
