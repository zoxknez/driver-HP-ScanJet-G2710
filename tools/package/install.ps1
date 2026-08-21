<#
.SYNOPSIS
    Instalira G2710 drajver na racunar na kome se skener testira.

.DESCRIPTION
    Radi tacno tri stvari, i nista vise:

        1. ubaci razvojni sertifikat u Root i TrustedPublisher
        2. ubaci g2710.inf u DriverStore (pnputil)
        3. kaze sta se desilo, na srpskom

    Sto NE radi:
      - ne gasi Secure Boot
      - ne pali TESTSIGNING
      - ne dira nijednu drugu postavku sistema

    To je namerno. Plan zove ovaj slucaj H1-A: prvo se proba sa UKLJUCENIM
    Secure Boot-om i HVCI-jem, jer ako tako prodje, ta zastita se nikada ne
    dira. Tek ako H1-A padne, prelazi se na H1-B, i to je zaseban, svesan
    korak - ne nesto sto skript uradi sam.

.PARAMETER Uninstall
    Uklanja drajver iz DriverStore-a i sertifikat iz oba skladista.

.EXAMPLE
    Desni klik na PowerShell -> "Pokreni kao administrator", pa:
    powershell -ExecutionPolicy Bypass -File install.ps1
#>
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$inf  = Join-Path $here 'g2710.inf'
$cer  = Join-Path $here 'g2710-dev.cer'

function Say {
    param([string]$Text, [string]$Color = 'Gray')
    if (-not $Quiet) { Write-Host $Text -ForegroundColor $Color }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host ''
        Write-Host 'Ovaj skript mora da se pokrene kao administrator.' -ForegroundColor Red
        Write-Host ''
        Write-Host 'Kako:' -ForegroundColor Yellow
        Write-Host '  1. Pritisnite Windows dugme i ukucajte: powershell'
        Write-Host '  2. Desni klik na "Windows PowerShell"'
        Write-Host '  3. Izaberite "Pokreni kao administrator"'
        Write-Host '  4. Ukucajte:'
        Write-Host ("     powershell -ExecutionPolicy Bypass -File `"{0}`"" -f $PSCommandPath)
        Write-Host ''
        exit 2
    }
}

# --- pomocne funkcije oko sertifikata ----------------------------------------
#
# Trazi se po otisku (thumbprint), a ne po imenu. Ime nije jedinstveno i moze
# se poklopiti sa tudjim sertifikatom; otisak ne moze.

function Get-CertThumbprint {
    param([string]$Path)
    $certificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2 $Path
    return $certificate.Thumbprint
}

function Install-DevCertificate {
    param([string]$Path)

    $thumb = Get-CertThumbprint -Path $Path
    foreach ($store in 'Root', 'TrustedPublisher') {
        $target = "Cert:\LocalMachine\$store"
        if (Get-ChildItem $target | Where-Object { $_.Thumbprint -eq $thumb }) {
            Say "      $store    vec postoji"
            continue
        }
        Import-Certificate -FilePath $Path -CertStoreLocation $target | Out-Null
        Say "      $store    dodat"
    }
    return $thumb
}

function Remove-DevCertificate {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        Say '      sertifikat nije u paketu - preskacem'
        return
    }
    $thumb = Get-CertThumbprint -Path $Path
    foreach ($store in 'Root', 'TrustedPublisher') {
        $found = Get-ChildItem "Cert:\LocalMachine\$store" |
                 Where-Object { $_.Thumbprint -eq $thumb }
        if ($found) {
            $found | Remove-Item -Force
            Say "      $store    uklonjen"
        } else {
            Say "      $store    nije bio instaliran"
        }
    }
}

# --- pnputil ------------------------------------------------------------------

function Get-InstalledOemInf {
    # pnputil ispisuje lokalizovano, pa se ne oslanjamo na engleske naslove:
    # trazi se blok koji sadrzi nas originalni naziv INF-a, i iz njega red
    # koji lici na oemNN.inf.
    $lines = & pnputil /enum-drivers
    $current = $null
    foreach ($line in $lines) {
        if ($line -match '(oem\d+\.inf)') { $current = $Matches[1] }
        if ($line -match 'g2710\.inf' -and $current) { return $current }
    }
    return $null
}

# --- glavni tok ---------------------------------------------------------------

Assert-Administrator

Say ''
Say 'HP ScanJet G2710 - instalacija drajvera' 'Cyan'
Say ''

if ($Uninstall) {
    Say '[1/2] Uklanjam drajver iz DriverStore-a'
    $oem = Get-InstalledOemInf
    if ($oem) {
        & pnputil /delete-driver $oem /uninstall /force | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Say "      $oem uklonjen" 'Green'
        } else {
            Say "      pnputil je vratio $LASTEXITCODE" 'Yellow'
        }
    } else {
        Say '      drajver nije bio instaliran'
    }

    Say '[2/2] Uklanjam sertifikat'
    Remove-DevCertificate -Path $cer

    Remove-Item (Join-Path $here 'install-state.json') -Force -ErrorAction SilentlyContinue

    Say ''
    Say 'Gotovo. Racunar je vracen u stanje pre instalacije.' 'Green'
    exit 0
}

if (-not (Test-Path $inf)) { throw "Nema g2710.inf pored skripta ($here)" }
if (-not (Test-Path $cer)) { throw "Nema g2710-dev.cer pored skripta ($here)" }

# Stanje sistema se BELEZI, ne menja. Ako instalacija padne, iz izvestaja se
# vidi u kakvom je stanju masina bila - a to je ceo smisao testa H1-A.
#
# Ovo je jedino mesto u paketu koje sigurno radi kao administrator, pa je i
# jedino koje moze da procita bcdedit. collect-diagnostics.ps1 se pokrece
# duplim klikom i to ne moze - zato zapis ostaje ovde, pored skripta, i on ga
# kasnije pokupi.
$secureBoot = try {
    if (Confirm-SecureBootUEFI) { 'ukljucen' } else { 'iskljucen' }
} catch { 'nema (legacy BIOS)' }

$memoryIntegrity = try {
    $guard = Get-CimInstance -ClassName Win32_DeviceGuard `
                             -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction Stop
    if ($guard.SecurityServicesRunning -contains 2) { 'ukljucen' } else { 'iskljucen' }
} catch { 'nepoznato' }

$bcd = & bcdedit /enum '{current}' 2>&1 | Out-String
$testSigning = if ($LASTEXITCODE -ne 0) { 'nepoznato' }
               elseif ($bcd -match '(?im)^\s*testsigning\s+Yes') { 'ukljucen' }
               else { 'iskljucen' }

Say "      Secure Boot        $secureBoot   (ne diramo ga)"
Say "      Memory Integrity   $memoryIntegrity"
Say "      TESTSIGNING        $testSigning"

Say '[1/3] Ubacujem sertifikat'
$thumbprint = Install-DevCertificate -Path $cer
Say "      otisak $thumbprint"

Say '[2/3] Ubacujem drajver u DriverStore'
$output = & pnputil /add-driver $inf /install 2>&1
$code = $LASTEXITCODE
$output | ForEach-Object { Say "      $_" }

# 259 = ERROR_NO_MORE_ITEMS: paket je dodat, ali nijedan prikljucen uredjaj mu
# ne odgovara. Kada skener nije prikljucen to je OCEKIVAN ishod, ne greska -
# i acceptance gate faze G2710-11 trazi bas da se tako i prijavi.
if ($code -eq 0) {
    Say '      drajver instaliran' 'Green'
} elseif ($code -eq 259) {
    Say '      drajver je u DriverStore-u, ali skener nije prikljucen' 'Yellow'
} else {
    Say "      pnputil je vratio $code" 'Red'
    exit $code
}

Say '[3/3] Trazim skener'
$device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
          Where-Object { $_.InstanceId -like '*VID_03F0&PID_2805*' }

# Zapis za collect-diagnostics.ps1. Ovo je H1-A u jednom fajlu: sta je bilo
# ukljuceno, sta je pnputil rekao, i da li se uredjaj pojavio.
[ordered]@{
    installedAt      = (Get-Date).ToString('o')
    secureBoot       = $secureBoot
    memoryIntegrity  = $memoryIntegrity
    testSigning      = $testSigning
    certThumbprint   = $thumbprint
    pnputilExitCode  = $code
    deviceFound      = [bool]$device
    deviceStatus     = if ($device) { "$($device.Status)" } else { 'nije prikljucen' }
} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $here 'install-state.json') -Encoding UTF8

Say ''
if ($device) {
    Say "Skener je pronadjen: $($device.FriendlyName)" 'Green'
    Say "Stanje: $($device.Status)"
    Say ''
    Say 'Sledeci korak: pokrenite G2710.Qualification.exe'
} else {
    Say 'Skener trenutno NIJE prikljucen.' 'Yellow'
    Say 'Drajver je spreman - prikljucite skener i ukljucite ga,'
    Say 'pa pokrenite G2710.Qualification.exe'
}
Say ''
exit 0
