<#
.SYNOPSIS
    Elevated acceptance proverava instalaciju i cist uninstall G2710 MSI-ja.

.DESCRIPTION
    Ovo je namerno odvojeno od build-a: MSI je per-machine i test menja
    DriverStore/certificate store. Pokrece se samo u administrator PowerShell
    sesiji, nad development paketom, i na kraju vraca racunar u prvobitno
    stanje. Ne sme se pokretati ako je G2710 vec instaliran.
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$MsiPath)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'verify-msi-install.ps1 mora da radi kao administrator.'
}
$msi = (Resolve-Path -LiteralPath $MsiPath).Path
$app = 'C:\Program Files\HP ScanJet G2710'
$twain64 = 'C:\Windows\twain_64\G2710.Twain.ds'
$twain32 = 'C:\Windows\twain_32\G2710.Twain.ds'
function Get-G2710Certificates {
    @(Get-ChildItem Cert:\LocalMachine\Root,Cert:\LocalMachine\TrustedPublisher |
        Where-Object { $_.Subject -like '*G2710 Development Signing*' })
}
if ((Test-Path -LiteralPath $app) -or (Test-Path -LiteralPath $twain64) -or
    (Test-Path -LiteralPath $twain32) -or (Get-G2710Certificates).Count -gt 0) {
    throw 'G2710 je vec prisutan; acceptance test odbija da dira postojecu instalaciju.'
}

$install = Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn" -Wait -PassThru
if ($install.ExitCode -ne 0) { throw "MSI instalacija je vratila $($install.ExitCode)." }
try {
    foreach ($path in "$app\G2710.App.exe", "$app\G2710.Native.dll", "$app\driver\G2710.Wia.dll", $twain64, $twain32) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Instalacija nema obavezan fajl: $path" }
    }
    if ((Get-G2710Certificates).Count -ne 2) { throw 'Development sertifikat nije u oba trazena store-a.' }
} finally {
    $uninstall = Start-Process msiexec.exe -ArgumentList "/x `"$msi`" /qn" -Wait -PassThru
    if ($uninstall.ExitCode -ne 0) { throw "MSI deinstalacija je vratila $($uninstall.ExitCode)." }
}
foreach ($path in $app, $twain64, $twain32) {
    if (Test-Path -LiteralPath $path) { throw "Cist uninstall je ostavio: $path" }
}
if ((Get-G2710Certificates).Count -ne 0) { throw 'Cist uninstall je ostavio razvojni sertifikat.' }
if (& pnputil /enum-drivers | Select-String -Quiet -Pattern 'g2710\.inf') {
    throw 'Cist uninstall je ostavio G2710 INF u DriverStore-u.'
}
Write-Host 'MSI install/uninstall acceptance je prosao; sistem je ociscen.' -ForegroundColor Green
