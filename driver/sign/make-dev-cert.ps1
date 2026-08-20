<#
.SYNOPSIS
    Pravi self-signed code-signing sertifikat za razvoj.

.DESCRIPTION
    SAMO ZA RAZVOJ. Produkcija ide preko Partner Center-a sa EV sertifikatom;
    isti INF/CAT paket, samo drugi potpis (SIGNING_MODE=Release).

    Za instalaciju driver paketa na Windows 10/11 nije dovoljno da paket bude
    potpisan - sertifikat mora biti u Trusted Root I Trusted Publishers. To je
    isti pristup koji koriste libwdi/Zadig i radi sa ukljucenim Secure Boot-om
    bez test-signing moda.

    HIPOTEZA, ne cinjenica: da ce PnP prihvatiti self-signed katalog zato sto
    paket ne isporucuje nijedan novi kernel binarni fajl (usbscan.sys je vec
    Microsoft-potpisan). Proverava se u H1-A. Vidi docs/SIGNING.md.

.PARAMETER MachineStore
    Instalira u LocalMachine umesto CurrentUser. Trazi admin prava. Ovo je
    ono sto radi installer kod prijatelja; za lokalni razvoj nije potrebno.
#>
[CmdletBinding()]
param(
    [string]$OutputDir = (Join-Path $PSScriptRoot 'out'),
    [string]$SubjectName = 'G2710 Development Signing',
    [switch]$MachineStore,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$storeLocation = if ($MachineStore) { 'LocalMachine' } else { 'CurrentUser' }

Write-Host "Pravim self-signed code-signing sertifikat u ${storeLocation}\My"

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=$SubjectName" `
    -KeyUsage DigitalSignature `
    -KeyExportPolicy Exportable `
    -KeyLength 3072 `
    -HashAlgorithm SHA256 `
    -CertStoreLocation "Cert:\$storeLocation\My" `
    -NotAfter (Get-Date).AddYears(3) `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')

Write-Host "  Thumbprint: $($cert.Thumbprint)"

$cerPath = Join-Path $OutputDir 'g2710-dev.cer'
$pfxPath = Join-Path $OutputDir 'g2710-dev.pfx'

Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

# Lozinka je fiksna i beskorisna namerno - ovo je razvojni kljuc koji nikada
# ne napusta ovu masinu. Produkcioni kljuc ide u HSM/EV token, ne u fajl.
$password = ConvertTo-SecureString -String 'g2710-dev' -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $password -Force | Out-Null

Write-Host "  $cerPath"
Write-Host "  $pfxPath"

if ($Install) {
    # Root: da lanac bude poverljiv. TrustedPublisher: da PnP ne pita korisnika.
    #
    # Import-Certificate se ovde ne koristi: za Root stores on trazi GUI
    # potvrdu ("UI is not allowed in this operation") i pada u neinteraktivnom
    # radu - CI, installer, remote session. X509Store API upisuje direktno.
    $public = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($cerPath)
    $location = [System.Security.Cryptography.X509Certificates.StoreLocation]::$storeLocation

    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        Write-Host "Instaliram u ${storeLocation}\$storeName"
        $store = [System.Security.Cryptography.X509Certificates.X509Store]::new($storeName, $location)
        try {
            $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
            $store.Add($public)
        } finally {
            $store.Close()
        }
    }
    $public.Dispose()
}

[PSCustomObject]@{
    Thumbprint = $cert.Thumbprint
    Cer        = $cerPath
    Pfx        = $pfxPath
    Store      = $storeLocation
}
