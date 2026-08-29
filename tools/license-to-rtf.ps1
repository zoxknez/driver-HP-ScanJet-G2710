<#
.SYNOPSIS
    Pretvori LICENSE u RTF koji WixUI ume da prikaze.

.DESCRIPTION
    WixUI_Minimal trazi RTF, a licenca u repozitorijumu je obican tekst. Da su
    dva fajla, razisli bi se: instalater bi pokazivao jednu licencu, a paket
    isporucivao drugu. GPL-2.0 zahteva da uz binarni oblik ide TACNO ona licenca
    pod kojom se izvor daje, pa se RTF pravi iz LICENSE pri svakom build-u i
    nikada se ne uredjuje rukom.

    Izlaz nije u repozitorijumu; nastaje u build/ i tamo ostaje.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source)) { throw "Nema licence: $Source" }
$text = Get-Content -LiteralPath $Source -Raw

# GPL-2.0 je ceo u ASCII-ju. Ako se to promeni, radije se stane nego da se
# instalateru posalje tekst koji ce prikazati kao smece.
$offending = [regex]::Match($text, '[^\x09\x0A\x0D\x20-\x7E]')
if ($offending.Success) {
    $code = [int][char]$offending.Value
    throw "Licenca sadrzi znak van ASCII-ja (U+{0:X4}) na poziciji {1}." -f $code, $offending.Index
}

# Redosled je bitan: obrnuta kosa crta prva, inace bi se escape-ovale i one
# koje sami dodajemo.
$body = $text -replace '\\', '\\\\'
$body = $body -replace '\{', '\{'
$body = $body -replace '\}', '\}'
$body = $body -replace "`r`n", "`n"
$body = $body -replace "`n", "\par`r`n"

$rtf = "{\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fnil\fcharset0 Consolas;}}`r`n" +
       "\viewkind4\uc1\pard\f0\fs16 " + $body + "`r`n}`r`n"

$directory = Split-Path -Parent $Destination
if ($directory -and -not (Test-Path -LiteralPath $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
[System.IO.File]::WriteAllText($Destination, $rtf, [System.Text.Encoding]::ASCII)
Write-Verbose "RTF licenca: $Destination"
