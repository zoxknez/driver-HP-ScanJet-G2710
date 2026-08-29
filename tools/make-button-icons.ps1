<#
.SYNOPSIS
    Napravi tri ikone za fizicka dugmad skenera (Scan, Copy, PDF).

.DESCRIPTION
    driver/g2710.inf pokazuje Windows-u na `G2710.Wia.dll,101/102/103` kao ikone
    za dogadjaje dugmadi. Izmereno na sastavljenom DLL-u: u njemu nije bilo
    NIJEDNE ikone. Windows u kartici "Dogadjaji" na osobinama uredjaja tada
    prikazuje prazno mesto - drajver koji obecava nesto sto ne isporucuje.

    Ikone nastaju iz skripte, u istim bojama kao program, i ne stoje u
    repozitorijumu: binarni fajl koji se ne vidi u diff-u ne ide uz izvor.

    Format je ICO sa ugradjenim PNG-om (podrzano od Viste). Pisan je rucno jer
    System.Drawing ne ume da sacuva vise velicina u jedan .ico.
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$accent = [System.Drawing.Color]::FromArgb(255, 0x1B, 0x62, 0xC4)
$lamp   = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xD1, 0x66)
$paper  = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xFF, 0xFF)

# Crtez je u koordinatama 48x48 pa se skalira; tanke linije bi na 16 px nestale.
function New-ButtonBitmap([string]$Kind, [int]$Size) {
    $bmp = New-Object System.Drawing.Bitmap $Size, $Size,
           ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($Size / 48.0, $Size / 48.0)

    $body = New-Object System.Drawing.SolidBrush $accent
    $sheet = New-Object System.Drawing.SolidBrush $paper
    $bar = New-Object System.Drawing.SolidBrush $lamp

    switch ($Kind) {
        'Scan' {
            # Telo skenera sa upaljenom trakom - isti znak kao na banneru.
            $g.FillRectangle($body, 4, 16, 40, 24)
            $g.FillRectangle($sheet, 9, 21, 30, 14)
            $g.FillRectangle($bar, 13, 26, 22, 4)
            $g.FillRectangle($body, 8, 7, 32, 5)
        }
        'Copy' {
            # Dva lista, jedan preko drugog.
            $g.FillRectangle($body, 6, 6, 26, 32)
            $g.FillRectangle($sheet, 10, 10, 18, 24)
            $g.FillRectangle($body, 16, 14, 26, 30)
            $g.FillRectangle($sheet, 20, 18, 18, 22)
        }
        'Pdf' {
            # List sa presavijenim uglom i trakom za natpis.
            $g.FillRectangle($body, 9, 5, 30, 38)
            $g.FillRectangle($sheet, 13, 9, 22, 30)
            $g.FillRectangle($body, 13, 26, 22, 9)
            $g.FillRectangle($bar, 15, 28, 18, 5)
        }
        default { throw "nepoznato dugme: $Kind" }
    }

    $body.Dispose(); $sheet.Dispose(); $bar.Dispose(); $g.Dispose()
    return $bmp
}

# ICO kontejner: zaglavlje, po jedan opis za svaku velicinu, pa slike.
#
# Slike su klasicni DIB, ne PNG. PNG u ikoni podrzava Windows od Viste, ali ga
# System.Drawing.Icon.ToBitmap ne cita - a bas to je nacin na koji se ikona
# proverava posle build-a. Ikona koja se ne moze procitati istim alatom kojim
# je pravljena je ikona o kojoj se nista ne zna.
#
# DIB u ikoni ima visinu DVOSTRUKU: prvo XOR slika, pa AND maska. Alfa nosi
# providnost, ali maska mora postojati i mora biti poravnata na 4 bajta po
# redu, inace Windows crta smece oko ivica.
function ConvertTo-IconImage($Bitmap) {
    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $maskStride = [int][Math]::Floor((($width + 31) / 32)) * 4
        $xorSize = $width * $height * 4
        $andSize = $maskStride * $height

        $writer.Write([uint32]40)                 # velicina BITMAPINFOHEADER-a
        $writer.Write([int32]$width)
        $writer.Write([int32]($height * 2))       # XOR + AND
        $writer.Write([uint16]1)                  # ravni
        $writer.Write([uint16]32)                 # bita po pikselu
        $writer.Write([uint32]0)                  # BI_RGB
        $writer.Write([uint32]($xorSize + $andSize))
        $writer.Write([int32]0); $writer.Write([int32]0)
        $writer.Write([uint32]0); $writer.Write([uint32]0)

        # DIB ide odozdo nagore.
        for ($y = $height - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $width; $x++) {
                $pixel = $Bitmap.GetPixel($x, $y)
                $writer.Write([byte]$pixel.B); $writer.Write([byte]$pixel.G)
                $writer.Write([byte]$pixel.R); $writer.Write([byte]$pixel.A)
            }
        }
        # AND maska: nule znace "koristi XOR sliku". Providnost nosi alfa.
        for ($y = 0; $y -lt $height; $y++) {
            for ($b = 0; $b -lt $maskStride; $b++) { $writer.Write([byte]0) }
        }
        $writer.Flush()
        return $stream.ToArray()
    } finally {
        $writer.Dispose(); $stream.Dispose()
    }
}

function Save-Icon([string]$Kind, [string]$Path) {
    $sizes = 16, 32, 48
    $images = @()
    foreach ($size in $sizes) {
        $bmp = New-ButtonBitmap -Kind $Kind -Size $size
        $images += , @{ Size = $size; Bytes = (ConvertTo-IconImage -Bitmap $bmp) }
        $bmp.Dispose()
    }

    $stream = [System.IO.File]::Create($Path)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $writer.Write([uint16]0)                  # rezervisano
        $writer.Write([uint16]1)                  # tip: ikona
        $writer.Write([uint16]$images.Count)

        $offset = 6 + 16 * $images.Count
        foreach ($image in $images) {
            $writer.Write([byte]$image.Size)      # sirina  (0 znaci 256)
            $writer.Write([byte]$image.Size)      # visina
            $writer.Write([byte]0)                # broj boja: 0 = pun opseg
            $writer.Write([byte]0)                # rezervisano
            $writer.Write([uint16]1)              # ravni
            $writer.Write([uint16]32)             # bita po pikselu
            $writer.Write([uint32]$image.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $image.Bytes.Length
        }
        # Tip se pise izricito: PowerShell razmota byte[] koji funkcija vrati u
        # Object[], a BinaryWriter tada veze pogresan preklop i upise JEDAN
        # bajt po slici. Mereno: ceo .ico je ispao 57 bajtova umesto 4 KB, i
        # niko se nije bunio - fajl je samo bio neupotrebljiv.
        foreach ($image in $images) { $writer.Write([byte[]]$image.Bytes) }
    } finally {
        $writer.Dispose(); $stream.Dispose()
    }
}

foreach ($kind in 'Scan', 'Copy', 'Pdf') {
    $path = Join-Path $OutputDirectory "$kind.ico"
    Save-Icon -Kind $kind -Path $path
    if (-not (Test-Path -LiteralPath $path)) { throw "ikona nije nastala: $path" }
}
Write-Verbose "Ikone dugmadi: $OutputDirectory"
