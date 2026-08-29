<#
.SYNOPSIS
    Napravi banner i pozadinu za WixUI dijaloge.

.DESCRIPTION
    Bez ovoga instalater nosi Wix-ov podrazumevani crtez - crveni znak zabrane -
    i to je prvo sto covek vidi kada pokrene paket. Slika koja kaze "zabranjeno"
    na prvom ekranu instalacije drajvera je poruka koju niko nije hteo da posalje.

    Boje su iste one iz Theme/Palette.xaml, da instalater i program ne izgledaju
    kao dva razlicita proizvoda. Slike nastaju u build/ i nisu u repozitorijumu -
    binarni fajl koji se ne moze procitati u diff-u ne ide uz izvor.

    Dimenzije su one koje WixUI trazi: banner 493x58, pozadina 493x312.
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$surface = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
$text    = [System.Drawing.Color]::FromArgb(255, 0x14, 0x18, 0x1D)
$muted   = [System.Drawing.Color]::FromArgb(255, 0x5B, 0x66, 0x72)
$accent  = [System.Drawing.Color]::FromArgb(255, 0x1B, 0x62, 0xC4)
$onDark  = [System.Drawing.Color]::FromArgb(255, 0xF4, 0xF6, 0xF8)

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    , @($bmp, $g)
}

# Skener u par poteza: telo, staklo, i traka svetla ispod poklopca. Prepoznaje
# se i na 40 piksela, sto logo sa detaljima ne bi.
function Draw-Scanner($g, [single]$x, [single]$y, [single]$size, $body, $glass, $lamp) {
    $unit = $size / 40.0
    $bodyRect = New-Object System.Drawing.RectangleF ($x), ($y + 10 * $unit), ($size), ($size * 0.62)
    $bodyBrush = New-Object System.Drawing.SolidBrush $body
    $g.FillRectangle($bodyBrush, $bodyRect)
    $glassRect = New-Object System.Drawing.RectangleF ($x + 4 * $unit), ($y + 14 * $unit), ($size - 8 * $unit), ($size * 0.34)
    $glassBrush = New-Object System.Drawing.SolidBrush $glass
    $g.FillRectangle($glassBrush, $glassRect)
    $lampBrush = New-Object System.Drawing.SolidBrush $lamp
    $lampRect = New-Object System.Drawing.RectangleF ($x + 7 * $unit), ($y + 20 * $unit), ($size - 14 * $unit), (3 * $unit)
    $g.FillRectangle($lampBrush, $lampRect)
    $lidBrush = New-Object System.Drawing.SolidBrush $body
    $lidRect = New-Object System.Drawing.RectangleF ($x + 2 * $unit), ($y + 3 * $unit), ($size - 4 * $unit), (5 * $unit)
    $g.FillRectangle($lidBrush, $lidRect)
    $bodyBrush.Dispose(); $glassBrush.Dispose(); $lampBrush.Dispose(); $lidBrush.Dispose()
}

# --- banner: 493x58, naslov dijaloga stoji levo pa znak ide desno ------------
$pair = New-Canvas 493 58
$bmp = $pair[0]; $g = $pair[1]
$g.Clear($surface)
Draw-Scanner $g 430 6 46 $accent $surface ([System.Drawing.Color]::FromArgb(255, 0xFF, 0xD1, 0x66))
$line = New-Object System.Drawing.SolidBrush $accent
$g.FillRectangle($line, 0, 56, 493, 2)
$line.Dispose()
$g.Dispose()
$bmp.Save((Join-Path $OutputDirectory 'banner.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()

# --- pozadina prvog ekrana: 493x312, levo traka, desno prazno za tekst -------
$pair = New-Canvas 493 312
$bmp = $pair[0]; $g = $pair[1]
$g.Clear($surface)
$panel = New-Object System.Drawing.Rectangle 0, 0, 164, 312
$from = [System.Drawing.Color]::FromArgb(255, 0x12, 0x43, 0x82)
$gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush $panel, $from, $accent, 60.0
$g.FillRectangle($gradient, $panel)
$gradient.Dispose()

Draw-Scanner $g 46 96 72 $onDark ([System.Drawing.Color]::FromArgb(255, 0x12, 0x43, 0x82)) ([System.Drawing.Color]::FromArgb(255, 0xFF, 0xD1, 0x66))

$titleFont = New-Object System.Drawing.Font 'Segoe UI Semibold', 13.0
$smallFont = New-Object System.Drawing.Font 'Segoe UI', 8.5
$onDarkBrush = New-Object System.Drawing.SolidBrush $onDark
$mutedOnDark = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x94, 0xA1, 0xAE))
$format = New-Object System.Drawing.StringFormat
$format.Alignment = [System.Drawing.StringAlignment]::Center
$g.DrawString('HP ScanJet', $titleFont, $onDarkBrush, (New-Object System.Drawing.RectangleF 0, 186, 164, 22), $format)
$g.DrawString('G2710', $titleFont, $onDarkBrush, (New-Object System.Drawing.RectangleF 0, 206, 164, 22), $format)
$g.DrawString('GPL-2.0', $smallFont, $mutedOnDark, (New-Object System.Drawing.RectangleF 0, 236, 164, 18), $format)
$titleFont.Dispose(); $smallFont.Dispose(); $onDarkBrush.Dispose(); $mutedOnDark.Dispose(); $format.Dispose()
$g.Dispose()
$bmp.Save((Join-Path $OutputDirectory 'dialog.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()

foreach ($name in 'banner.bmp', 'dialog.bmp') {
    $path = Join-Path $OutputDirectory $name
    if (-not (Test-Path -LiteralPath $path)) { throw "Slika nije nastala: $path" }
}
Write-Verbose "Slike instalatera: $OutputDirectory"
