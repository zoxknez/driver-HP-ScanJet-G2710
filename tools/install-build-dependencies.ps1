<#
.SYNOPSIS
    Instalira WiX i preuzima zvanični TWAIN ugovor za build.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install-build-dependencies.ps1 -AcceptTwainTerms

.NOTES
    - WiX se instalira preko winget-a.
    - TWAIN header se preuzima samo kada korisnik eksplicitno prihvati uslove
      izvornog TWAIN Working Group repozitorijuma.
#>
[CmdletBinding()]
param(
    [switch]$AcceptTwainTerms,
    [switch]$SkipWiX
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not $SkipWiX) {
    if (-not (Get-Command wix -ErrorAction SilentlyContinue) -and
        -not (Test-Path 'C:\Program Files\WiX Toolset v7.0\bin\wix.exe')) {
        winget install --id WiXToolset.WiXCLI --exact --silent `
            --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { throw 'WiX instalacija nije uspela.' }
    }
}

if (-not $AcceptTwainTerms) {
    throw 'TWAIN se ne preuzima bez -AcceptTwainTerms. Pregledajte https://github.com/twain/twain-dsm pre pokretanja.'
}

$twainDir = Join-Path $repo 'third_party\twain'
New-Item -ItemType Directory -Force -Path $twainDir | Out-Null
$header = Join-Path $twainDir 'twain.h'
$source = 'https://raw.githubusercontent.com/twain/twain-dsm/master/TWAIN_DSM/src/twain.h'
Invoke-WebRequest -Uri $source -OutFile $header
if ((Get-Item $header).Length -lt 50000) { throw 'TWAIN header je nepotpun.' }

@"
TWAIN header: $source
Preuzet: $(Get-Date -Format o)
Licencu i copyright pogledajte u zaglavlju twain.h i izvornom repozitorijumu.
"@ | Set-Content (Join-Path $twainDir 'SOURCE.txt') -Encoding utf8

Write-Host 'WiX i TWAIN build preduslovi su spremni.' -ForegroundColor Green
