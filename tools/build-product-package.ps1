<#
.SYNOPSIS
    Pravi ZIP za isporuku punog G2710 proizvoda.

.DESCRIPTION
    Ne pravi novu varijantu binarnih fajlova: koristi provereni build-installer
    tok, pa ZIP sadrži upravo MSI čija su struktura, CAT potpis i TWAIN putanje
    već provereni. Hardverski prolaz ostaje odvojen od ovog offline paketa.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateSet('Development', 'Release')]
    [string]$SigningMode = 'Development'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Poreklo koda i higijena izvora, pre nego sto bilo sta krene u paket.
#
# Ove kapije su postojale a niko ih nije zvao; provera porekla je licencno
# pitanje - GPL-2.0 port sme da bude port, ali izvor koji ukljucuje referencu
# nije port nego kopija. Paket koji odlazi sa ove masine ne sme se napraviti
# pre nego sto se to potvrdi.
& (Join-Path $repo 'tools\verify-all.ps1') -GatesOnly
if ($LASTEXITCODE -ne 0) { throw 'kapije ne prolaze - paket se ne pravi' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
$stage = Join-Path $OutputDirectory 'G2710-product-stage'
$msiOutput = Join-Path $OutputDirectory 'msi'

& (Join-Path $repo 'tools\build-installer.ps1') -OutputDirectory $msiOutput -SigningMode $SigningMode
if ($LASTEXITCODE -ne 0) { throw 'MSI build nije uspeo.' }
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
$msi = Join-Path $msiOutput "G2710-$version-x64.msi"
if (-not (Test-Path -LiteralPath $msi)) { throw "MSI nije nastao: $msi" }

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $msi -Destination $stage
# Oba uputstva, uvek. Paket otvori i neko ko nije onaj kome je poslat, a jedini
# fajl koji objasnjava sta se dogadja ne sme biti na jeziku koji taj ne cita.
Copy-Item -LiteralPath (Join-Path $repo 'installer\README.en.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $repo 'installer\README.md') -Destination (Join-Path $stage 'PROCITAJ-ME.md')
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $msi
"$($hash.Hash)  $($hash.Path | Split-Path -Leaf)" | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ascii

$zip = Join-Path $OutputDirectory "G2710-$version-x64-$SigningMode.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $stage | Select-Object -ExpandProperty FullName) -DestinationPath $zip -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $zip)) { throw 'ZIP nije nastao.' }
Write-Host "Gotovo: $zip" -ForegroundColor Green
