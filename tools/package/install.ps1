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
    [switch]$Quiet,

    # Proveri tabelu izlaznih kodova i izadji. Ne dodiruje racunar.
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$inf  = Join-Path $here 'g2710.inf'
$cer  = Join-Path $here 'g2710-dev.cer'

function Say {
    param([string]$Text, [string]$Color = 'Gray')
    if (-not $Quiet) { Write-Host $Text -ForegroundColor $Color }
}

# Sta pnputil zapravo kaze svojim izlaznim kodom.
#
# Izdvojeno u funkciju da se moze PROVERITI bez pokretanja instalacije - vidi
# -SelfTest. Ova tabela je logika koja se moze pogresiti, a njen otkaz se ne
# vidi ovde nego na tudjoj masini, usred instalacije koja pukne bez objasnjenja.
function Get-PnputilOutcome {
    param([Parameter(Mandatory)][int]$ExitCode)

    switch ($ExitCode) {
        0 {
            return [pscustomobject]@{
                Ok = $true; Reboot = $false; Text = 'drajver instaliran'
            }
        }
        259 {
            # ERROR_NO_MORE_ITEMS: paket je dodat, ali nijedan prikljucen
            # uredjaj mu ne odgovara. Kada skener nije prikljucen to je
            # OCEKIVAN ishod, ne greska.
            return [pscustomobject]@{
                Ok = $true; Reboot = $false
                Text = 'drajver je u DriverStore-u, ali skener nije prikljucen'
            }
        }
        3010 {
            # ERROR_SUCCESS_REBOOT_REQUIRED. Instalacija je USPELA; Windows
            # samo trazi restart da bi je dovrsio.
            #
            # Prva verzija je ovo tretirala kao gresku i vracala 3010 dalje.
            # MSI custom action ima Return="check", pa bi cela instalacija
            # pukla - na svakoj masini na kojoj Windows zatrazi restart, i ni
            # na jednoj ovde.
            return [pscustomobject]@{
                Ok = $true; Reboot = $true
                Text = 'drajver instaliran; Windows trazi restart da ga dovrsi'
            }
        }
        default {
            return [pscustomobject]@{
                Ok = $false; Reboot = $false
                Text = "pnputil je vratio $ExitCode"
            }
        }
    }
}

# Stanje Secure Boot-a, iz registra pa tek onda iz cmdlet-a.
#
# Prva verzija je zvala samo Confirm-SecureBootUEFI i svaki neuspeh tumacila
# kao "legacy BIOS". Izmereno pri stvarnoj MSI instalaciji: pod LocalSystem-om
# cmdlet padne i na masini koja JESTE UEFI - pa je install-state.json tvrdio
# "nema (legacy BIOS)" za racunar sa UEFI-jem i iskljucenim Secure Boot-om.
#
# Ta niska ide u izvestaj po kome se ocenjuje H1-A. Netacan zapis je gori od
# praznog: prazan se vidi, netacan se ne vidi.
#
# Kljuc SecureBoot\State postoji SAMO na UEFI masinama, pa njegovo odsustvo
# jeste odgovor - a ne nagadjanje. Isti kljuc cita i collect-diagnostics.ps1.
function Get-SecureBootState {
    try {
        $state = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' `
                                  -Name UEFISecureBootEnabled -ErrorAction Stop
        return $(if ($state.UEFISecureBootEnabled -eq 1) { 'ukljucen' } else { 'iskljucen' })
    } catch {
        # Kljuca nema. To je ili legacy BIOS ili nedostupan registar; cmdlet
        # ume da razluci kada uopste moze da se izvrsi.
    }

    try {
        return $(if (Confirm-SecureBootUEFI) { 'ukljucen' } else { 'iskljucen' })
    } catch [System.PlatformNotSupportedException] {
        return 'nema (legacy BIOS)'
    } catch {
        return 'nepoznato'
    }
}

function Invoke-SelfTest {
    # `$script:` i pri inicijalizaciji, ne samo pri uvecavanju.
    #
    # Prva verzija je pisala `$failures = 0` - lokalno u funkciji - a Check je
    # uvecavao `$script:failures`, koja tada nije ni postojala. Poredjenje
    # `$null -eq 0` je netacno, pa je self-test prijavljivao pad iako je svih
    # deset provera proslo. Alat koji laze o sebi je gori od alata koga nema.
    $script:failures = 0
    function Check($label, $condition) {
        if ($condition) { Write-Host "  ok   $label" }
        else { Write-Host "  PAD  $label" -ForegroundColor Red; $script:failures++ }
    }

    $ok = Get-PnputilOutcome -ExitCode 0
    Check 'kod 0 je uspeh'            ($ok.Ok -and -not $ok.Reboot)

    $none = Get-PnputilOutcome -ExitCode 259
    Check 'kod 259 nije greska'       ($none.Ok -and -not $none.Reboot)
    Check '259 kaze da skenera nema'  ($none.Text -match 'nije prikljucen')

    $reboot = Get-PnputilOutcome -ExitCode 3010
    Check 'kod 3010 je uspeh'         ($reboot.Ok)
    Check '3010 trazi restart'        ($reboot.Reboot)
    Check '3010 pominje restart'      ($reboot.Text -match 'restart')

    foreach ($bad in 1, 5, 87, 1603) {
        $outcome = Get-PnputilOutcome -ExitCode $bad
        Check "kod $bad je greska"    (-not $outcome.Ok)
    }

    if ($script:failures -eq 0) {
        Write-Host 'Tabela izlaznih kodova je ispravna.' -ForegroundColor Green
        return 0
    }
    Write-Host "$script:failures provera nije proslo" -ForegroundColor Red
    return 1
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

if ($SelfTest) {
    exit (Invoke-SelfTest)
}

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

# Stanje sistema se BELEZI, ne menja. Ako instalacija padne, iz izvestaja se
# vidi u kakvom je stanju masina bila - a to je ceo smisao testa H1-A.
#
# Ovo je jedino mesto u paketu koje sigurno radi kao administrator, pa je i
# jedino koje moze da procita bcdedit. collect-diagnostics.ps1 se pokrece
# duplim klikom i to ne moze - zato zapis ostaje ovde, pored skripta, i on ga
# kasnije pokupi.
$secureBoot = Get-SecureBootState

$memoryIntegrity = try {
    $guard = Get-CimInstance -ClassName Win32_DeviceGuard `
                             -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction Stop
    if ($guard.SecurityServicesRunning -contains 2) { 'ukljucen' } else { 'iskljucen' }
} catch { 'nepoznato' }

# TESTSIGNING iz bcdedit-a, sa RAZLOGOM kada se ne moze pročitati.
#
# Izmereno: elevirano `bcdedit /enum {current}` vraća 0 i radi, ali pod
# LocalSystem-om iz MSI custom action-a vrati nenulti kod. Odgovor je tada
# pošteno "nepoznato" - ali gola reč "nepoznato" u izveštaju po kome se ocenjuje
# H1-A tera onoga ko ga čita da nagađa je li reč o grešci ili o ograničenju.
#
# Odsustvo reda `testsigning Yes` znači ISKLJUČENO; bcdedit ispisuje samo
# vrednosti koje odstupaju od podrazumevanih. Neuspeh poziva NIJE isto što i
# odsustvo tog reda, i ta dva se ne smeju stopiti.
$bcd = & bcdedit /enum '{current}' 2>&1 | Out-String
$bcdCode = $LASTEXITCODE
$testSigning = if ($bcdCode -ne 0) { "nepoznato (bcdedit je vratio $bcdCode)" }
               elseif ($bcd -match '(?im)^\s*testsigning\s+Yes') { 'ukljucen' }
               else { 'iskljucen' }

Say "      Secure Boot        $secureBoot   (ne diramo ga)"
Say "      Memory Integrity   $memoryIntegrity"
Say "      TESTSIGNING        $testSigning"

if (Test-Path $cer) {
    Say '[1/3] Ubacujem sertifikat'
    $thumbprint = Install-DevCertificate -Path $cer
    Say "      otisak $thumbprint"
} else {
    # Release paket ima katalog potpisan produkcionim sertifikatom; njegov
    # javni sertifikat se NE dodaje lokalno samo da bi instalacija prosla.
    Say '[1/3] Produkcioni katalog - nema lokalnog razvojnog sertifikata'
    $thumbprint = $null
}

Say '[2/3] Ubacujem drajver u DriverStore'
$output = & pnputil /add-driver $inf /install 2>&1
$code = $LASTEXITCODE
$output | ForEach-Object { Say "      $_" }

$outcome = Get-PnputilOutcome -ExitCode $code
if (-not $outcome.Ok) {
    Say "      $($outcome.Text)" 'Red'
    exit $code
}
Say "      $($outcome.Text)" $(if ($outcome.Reboot) { 'Yellow' } else { 'Green' })

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
    rebootRequired   = $outcome.Reboot
    deviceFound      = [bool]$device
    deviceStatus     = if ($device) { "$($device.Status)" } else { 'nije prikljucen' }
} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $here 'install-state.json') -Encoding UTF8

Say ''
if ($outcome.Reboot) {
    Say 'Windows trazi restart da bi drajver bio dovrsen.' 'Yellow'
    Say 'Restartujte racunar pre nego sto pokrenete proveru.'
    Say ''
}
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
