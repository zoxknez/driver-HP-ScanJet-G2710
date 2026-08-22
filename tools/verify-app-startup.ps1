<#
.SYNOPSIS
    Proverava da objavljeni WPF EXE moze da se podigne iz sopstvenog foldera.

.DESCRIPTION
    Build/publish moze proci i kada XAML staticki resurs ili binding obori
    aplikaciju tek pri stvaranju prvog prozora. Ova kratka provera namerno ne
    klikce kroz UI niti otvara uredjaj: meri samo da paket poseduje sve sto mu
    treba za bezbedan startup. Proces se uredno prekida posle provere.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AppPath,
    [int]$StartupSeconds = 3
)

$ErrorActionPreference = 'Stop'
if ($StartupSeconds -lt 1 -or $StartupSeconds -gt 30) { throw 'StartupSeconds mora biti 1-30.' }
$resolved = (Resolve-Path -LiteralPath $AppPath).Path
$process = Start-Process -FilePath $resolved -WorkingDirectory (Split-Path -Parent $resolved) `
    -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds $StartupSeconds
    if ($process.HasExited) { throw "Aplikacija se ugasila pri startup-u (exit code $($process.ExitCode))." }
    Write-Host 'Objavljena aplikacija se uspesno pokrenula iz sopstvenog foldera.' -ForegroundColor Green
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}
