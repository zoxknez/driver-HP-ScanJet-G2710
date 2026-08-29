<#
.SYNOPSIS
    Instalira G2710 drajver na racunar na kome se skener testira.

.DESCRIPTION
    Radi tacno tri stvari, i nista vise:

        1. ubaci razvojni sertifikat u Root i TrustedPublisher
        2. ubaci g2710.inf u DriverStore (pnputil)
        3. kaze sta se desilo, na engleskom ili srpskom (-Language)

    ZAPIS I PORUKA NISU ISTA STVAR. install-state.json je uvek na engleskom,
    jer ga cita onaj kome se izvestaj salje, a ne onaj ko pokrece skript. Na
    ekranu stoji jezik koji je korisnik izabrao.

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

.PARAMETER Language
    Jezik poruka na ekranu: en (podrazumevano) ili sr. Ne utice na
    install-state.json - taj zapis je uvek na engleskom.

.EXAMPLE
    Desni klik na PowerShell -> "Pokreni kao administrator", pa:
    powershell -ExecutionPolicy Bypass -File install.ps1
#>
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$Quiet,

    [ValidateSet('en', 'sr')]
    [string]$Language,

    # Proveri tabelu izlaznih kodova i izadji. Ne dodiruje racunar.
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$inf  = Join-Path $here 'g2710.inf'
$cer  = Join-Path $here 'g2710-dev.cer'

# Jezik: sto je receno prekidacem, pa sto pise u language.txt pored skripta
# (kvalifikacioni ZIP ga upisuje), pa engleski. Isti redosled kao u programu.
if (-not $Language) {
    $languageFile = Join-Path $here 'language.txt'
    $fromFile = if (Test-Path $languageFile) {
        (Get-Content -LiteralPath $languageFile -TotalCount 1 | Select-Object -First 1)
    } else { $null }
    $Language = if ($fromFile -and $fromFile.Trim() -in @('en', 'sr')) { $fromFile.Trim() } else { 'en' }
}

# Poruke stoje na jednom mestu, u oba jezika.
#
# Kljuc kome nedostaje prevod ispisuje se kao [kljuc] umesto da obori skript:
# instalacija drajvera ne sme pasti zbog jedne niske.
$Messages = @{
    CertExists   = @{ en = 'already there';        sr = 'vec postoji' }
    CertAdded    = @{ en = 'added';                sr = 'dodat' }
    CertMissing  = @{ en = '      the certificate is not in the package - skipping';
                      sr = '      sertifikat nije u paketu - preskacem' }
    CertRemoved  = @{ en = 'removed';              sr = 'uklonjen' }
    CertNotThere = @{ en = 'was not installed';    sr = 'nije bio instaliran' }
    NeedAdmin    = @{ en = 'This script has to be run as an administrator.';
                      sr = 'Ovaj skript mora da se pokrene kao administrator.' }
    HowTo        = @{ en = 'How:';                 sr = 'Kako:' }
    HowTo1       = @{ en = '  1. Press the Windows key and type: powershell';
                      sr = '  1. Pritisnite Windows dugme i ukucajte: powershell' }
    HowTo2       = @{ en = '  2. Right-click "Windows PowerShell"';
                      sr = '  2. Desni klik na "Windows PowerShell"' }
    HowTo3       = @{ en = '  3. Choose "Run as administrator"';
                      sr = '  3. Izaberite "Pokreni kao administrator"' }
    HowTo4       = @{ en = '  4. Type:';           sr = '  4. Ukucajte:' }
    Title        = @{ en = 'HP ScanJet G2710 - driver installation';
                      sr = 'HP ScanJet G2710 - instalacija drajvera' }
    RemovingDrv  = @{ en = '[1/2] Removing the driver from the DriverStore';
                      sr = '[1/2] Uklanjam drajver iz DriverStore-a' }
    RemovedOem   = @{ en = 'removed';              sr = 'uklonjen' }
    PnputilSaid  = @{ en = '      pnputil returned {0}';
                      sr = '      pnputil je vratio {0}' }
    NotInstalled = @{ en = '      the driver was not installed';
                      sr = '      drajver nije bio instaliran' }
    RemovingCert = @{ en = '[2/2] Removing the certificate';
                      sr = '[2/2] Uklanjam sertifikat' }
    Restored     = @{ en = 'Done. The computer is back as it was before the installation.';
                      sr = 'Gotovo. Racunar je vracen u stanje pre instalacije.' }
    NoInf        = @{ en = 'There is no g2710.inf next to the script ({0})';
                      sr = 'Nema g2710.inf pored skripta ({0})' }
    Untouched    = @{ en = '(we do not touch it)';  sr = '(ne diramo ga)' }
    AddingCert   = @{ en = '[1/3] Adding the certificate';
                      sr = '[1/3] Ubacujem sertifikat' }
    Thumbprint   = @{ en = '      thumbprint {0}';  sr = '      otisak {0}' }
    ProdCatalog  = @{ en = '[1/3] Production catalogue - no local development certificate';
                      sr = '[1/3] Produkcioni katalog - nema lokalnog razvojnog sertifikata' }
    AddingDrv    = @{ en = '[2/3] Adding the driver to the DriverStore';
                      sr = '[2/3] Ubacujem drajver u DriverStore' }
    Looking      = @{ en = '[3/3] Looking for the scanner';
                      sr = '[3/3] Trazim skener' }
    RebootWanted = @{ en = 'Windows wants a restart to finish the driver.';
                      sr = 'Windows trazi restart da bi drajver bio dovrsen.' }
    RebootFirst  = @{ en = 'Restart the computer before you run the check.';
                      sr = 'Restartujte racunar pre nego sto pokrenete proveru.' }
    Found        = @{ en = 'The scanner was found: {0}';
                      sr = 'Skener je pronadjen: {0}' }
    State        = @{ en = 'State: {0}';           sr = 'Stanje: {0}' }
    NextStep     = @{ en = 'Next step: run G2710.Qualification.exe';
                      sr = 'Sledeci korak: pokrenite G2710.Qualification.exe' }
    NotConnected = @{ en = 'The scanner is NOT connected right now.';
                      sr = 'Skener trenutno NIJE prikljucen.' }
    DriverReady  = @{ en = 'The driver is ready - connect the scanner and switch it on,';
                      sr = 'Drajver je spreman - prikljucite skener i ukljucite ga,' }
    ThenRun      = @{ en = 'then run G2710.Qualification.exe';
                      sr = 'pa pokrenite G2710.Qualification.exe' }
    DrvInstalled = @{ en = 'the driver was installed';
                      sr = 'drajver instaliran' }
    DrvNoDevice  = @{ en = 'the driver is in the DriverStore, but the scanner is not connected';
                      sr = 'drajver je u DriverStore-u, ali skener nije prikljucen' }
    DrvReboot    = @{ en = 'the driver was installed; Windows wants a restart to finish it';
                      sr = 'drajver instaliran; Windows trazi restart da ga dovrsi' }
    DrvFailed    = @{ en = 'pnputil returned {0}';  sr = 'pnputil je vratio {0}' }
    SelfTestOk   = @{ en = 'The exit-code table is correct.';
                      sr = 'Tabela izlaznih kodova je ispravna.' }
    SelfTestBad  = @{ en = '{0} checks did not pass';
                      sr = '{0} provera nije proslo' }
}

# Bez [Parameter()] atributa: sa njim PowerShell pravi advanced funkciju, a ona
# odbija drugi pozicioni argument porukom o "pozicionom parametru" - i to na
# mestu poziva, pa greska izgleda kao da je u pozivaocu.
function T {
    param([string]$Key, [object[]]$Arguments)
    $entry = $Messages[$Key]
    if (-not $entry) { return "[$Key]" }
    $text = $entry[$Language]
    if (-not $text) { $text = $entry['en'] }
    if ($Arguments) { return ($text -f $Arguments) }
    return $text
}

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
                Ok = $true; Reboot = $false; Text = (T 'DrvInstalled')
            }
        }
        259 {
            # ERROR_NO_MORE_ITEMS: paket je dodat, ali nijedan prikljucen
            # uredjaj mu ne odgovara. Kada skener nije prikljucen to je
            # OCEKIVAN ishod, ne greska.
            return [pscustomobject]@{
                Ok = $true; Reboot = $false
                Text = (T 'DrvNoDevice')
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
                Text = (T 'DrvReboot')
            }
        }
        default {
            return [pscustomobject]@{
                Ok = $false; Reboot = $false
                Text = (T 'DrvFailed' $ExitCode)
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
        return $(if ($state.UEFISecureBootEnabled -eq 1) { 'on' } else { 'off' })
    } catch {
        # Kljuca nema. To je ili legacy BIOS ili nedostupan registar; cmdlet
        # ume da razluci kada uopste moze da se izvrsi.
    }

    try {
        return $(if (Confirm-SecureBootUEFI) { 'on' } else { 'off' })
    } catch [System.PlatformNotSupportedException] {
        return 'absent (legacy BIOS)'
    } catch {
        return 'unknown'
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

    $reboot = Get-PnputilOutcome -ExitCode 3010
    Check 'kod 3010 je uspeh'         ($reboot.Ok)
    Check '3010 trazi restart'        ($reboot.Reboot)

    foreach ($bad in 1, 5, 87, 1603) {
        $outcome = Get-PnputilOutcome -ExitCode $bad
        Check "kod $bad je greska"    (-not $outcome.Ok)
    }

    # Poruka mora postojati na OBA jezika i mora se razlikovati.
    #
    # Ranije je ovde stajalo poredjenje sa srpskom niskom. Takva provera pada
    # cim se podrazumevani jezik promeni - a ono sto se meri nije jezik nego
    # da li tekst uopste stize do korisnika. Prazan tekst ili nedostajuci kljuc
    # (koji T vraca kao [Kljuc]) prolazio bi neprimecen.
    # Promenljiva se NE sme zvati $language: imena su neosetljiva na velicinu
    # slova, pa bi promenljiva petlje zaklonila parametar $Language u lokalnom
    # opsegu - a T bi je nasla pre one koju petlja vraca. Izmereno: posle
    # petlje se $script:Language vracao ispravno, a poruka je i dalje bila na
    # srpskom.
    $before = $Language
    $seen = @{}
    foreach ($candidate in 'en', 'sr') {
        $script:Language = $candidate
        $outcome = Get-PnputilOutcome -ExitCode 259
        Check "259 ima tekst na $candidate" `
              ($outcome.Text -and -not $outcome.Text.StartsWith('['))
        $seen[$candidate] = $outcome.Text
    }
    $script:Language = $before
    Check 'oba jezika daju razlicit tekst' ($seen['en'] -ne $seen['sr'])

    if ($script:failures -eq 0) {
        Write-Host (T 'SelfTestOk') -ForegroundColor Green
        return 0
    }
    Write-Host (T 'SelfTestBad' $script:failures) -ForegroundColor Red
    return 1
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host ''
        Write-Host (T 'NeedAdmin') -ForegroundColor Red
        Write-Host ''
        Write-Host (T 'HowTo') -ForegroundColor Yellow
        Write-Host (T 'HowTo1')
        Write-Host (T 'HowTo2')
        Write-Host (T 'HowTo3')
        Write-Host (T 'HowTo4')
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
            Say ("      {0,-16} {1}" -f $store, (T 'CertExists'))
            continue
        }
        Import-Certificate -FilePath $Path -CertStoreLocation $target | Out-Null
        Say ("      {0,-16} {1}" -f $store, (T 'CertAdded'))
    }
    return $thumb
}

function Remove-DevCertificate {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        Say (T 'CertMissing')
        return
    }
    $thumb = Get-CertThumbprint -Path $Path
    foreach ($store in 'Root', 'TrustedPublisher') {
        $found = Get-ChildItem "Cert:\LocalMachine\$store" |
                 Where-Object { $_.Thumbprint -eq $thumb }
        if ($found) {
            $found | Remove-Item -Force
            Say ("      {0,-16} {1}" -f $store, (T 'CertRemoved'))
        } else {
            Say ("      {0,-16} {1}" -f $store, (T 'CertNotThere'))
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
Say (T 'Title') 'Cyan'
Say ''

if ($Uninstall) {
    Say (T 'RemovingDrv')
    $oem = Get-InstalledOemInf
    if ($oem) {
        & pnputil /delete-driver $oem /uninstall /force | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Say ("      {0} {1}" -f $oem, (T 'RemovedOem')) 'Green'
        } else {
            Say (T 'PnputilSaid' $LASTEXITCODE) 'Yellow'
        }
    } else {
        Say (T 'NotInstalled')
    }

    Say (T 'RemovingCert')
    Remove-DevCertificate -Path $cer

    Remove-Item (Join-Path $here 'install-state.json') -Force -ErrorAction SilentlyContinue

    Say ''
    Say (T 'Restored') 'Green'
    exit 0
}

if (-not (Test-Path $inf)) { throw (T 'NoInf' $here) }

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
    if ($guard.SecurityServicesRunning -contains 2) { 'on' } else { 'off' }
} catch { 'unknown' }

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
$testSigning = if ($bcdCode -ne 0) { "unknown (bcdedit returned $bcdCode)" }
               elseif ($bcd -match '(?im)^\s*testsigning\s+Yes') { 'on' }
               else { 'off' }

Say ("      Secure Boot        {0}   {1}" -f $secureBoot, (T 'Untouched'))
Say "      Memory Integrity   $memoryIntegrity"
Say "      TESTSIGNING        $testSigning"

if (Test-Path $cer) {
    Say (T 'AddingCert')
    $thumbprint = Install-DevCertificate -Path $cer
    Say (T 'Thumbprint' $thumbprint)
} else {
    # Release paket ima katalog potpisan produkcionim sertifikatom; njegov
    # javni sertifikat se NE dodaje lokalno samo da bi instalacija prosla.
    Say (T 'ProdCatalog')
    $thumbprint = $null
}

Say (T 'AddingDrv')
$output = & pnputil /add-driver $inf /install 2>&1
$code = $LASTEXITCODE
$output | ForEach-Object { Say "      $_" }

$outcome = Get-PnputilOutcome -ExitCode $code
if (-not $outcome.Ok) {
    Say "      $($outcome.Text)" 'Red'
    exit $code
}
Say "      $($outcome.Text)" $(if ($outcome.Reboot) { 'Yellow' } else { 'Green' })

Say (T 'Looking')
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
    deviceStatus     = if ($device) { "$($device.Status)" } else { 'not connected' }
} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $here 'install-state.json') -Encoding UTF8

Say ''
if ($outcome.Reboot) {
    Say (T 'RebootWanted') 'Yellow'
    Say (T 'RebootFirst')
    Say ''
}
if ($device) {
    Say (T 'Found' $device.FriendlyName) 'Green'
    Say (T 'State' $device.Status)
    Say ''
    Say (T 'NextStep')
} else {
    Say (T 'NotConnected') 'Yellow'
    Say (T 'DriverReady')
    Say (T 'ThenRun')
}
Say ''
exit 0
