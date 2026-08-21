<#
.SYNOPSIS
    Sakuplja sve sto treba za analizu i pakuje u jedan ZIP.

.DESCRIPTION
    Prijatelj pokrene ovo, dobije jedan fajl, posalje ga nazad. Sve odluke o
    ispravkama donose se iz tog ZIP-a, jer skener nije kod nas.

    Sadrzaj:
        system-info.json     Windows verzija, Secure Boot, HVCI, TESTSIGNING
        device.json          da li je 03F0:2805 prisutan i u kom je stanju
        driverstore.txt      da li je nas INF u DriverStore-u
        g2710ctl-info.txt    ugradjeni profil i plafon build-a
        capabilities.json    tabela mogucnosti iz samog binarnog fajla
        test-results.json    izvestaj kvalifikacije, ako je wizard vec radio
        setupapi.dev.log     izvod: samo redovi o nasem uredjaju

    Nista se ne salje na mrezu. ZIP ostaje na radnoj povrsini dok ga covek
    sam ne posalje.

.PARAMETER OutputDirectory
    Gde ZIP zavrsava. Podrazumevano radna povrsina.

.PARAMETER ReportPath
    Izvestaj koji treba ubaciti u ZIP. Prosledjuje ga wizard, koji tacno zna
    koji je fajl upravo napisao. Bez ovoga se trazi najnoviji na radnoj
    povrsini - sto radi, ali pokupi i tudji izvestaj od proslog puta.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory = [Environment]::GetFolderPath('Desktop'),
    [string]$ReportPath
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$tool = Join-Path $here 'g2710ctl.exe'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path ([IO.Path]::GetTempPath()) "G2710-diag-$stamp"
New-Item -ItemType Directory -Path $work -Force | Out-Null

function Save-Json {
    param([string]$Name, $Value)
    $Value | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $work $Name) -Encoding UTF8
    Write-Host "      $Name"
}

function Save-Text {
    param([string]$Name, $Value)
    ($Value | Out-String) | Set-Content (Join-Path $work $Name) -Encoding UTF8
    Write-Host "      $Name"
}

Write-Host ''
Write-Host 'HP ScanJet G2710 - sakupljanje podataka' -ForegroundColor Cyan
Write-Host ''

# --- 1. sistem -----------------------------------------------------------------
#
# Sve tri bezbednosne postavke se BELEZE, ne menjaju. H1-A je eksperiment sa
# UKLJUCENIM Secure Boot-om; bez zapisa o stvarnom stanju masine, ishod tog
# eksperimenta ne znaci nista.

Write-Host '[1/6] Sistem'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$elevated = (New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole(
                [Security.Principal.WindowsBuiltInRole]::Administrator)

# Secure Boot se cita iz registra, a NE preko Confirm-SecureBootUEFI.
# Confirm-SecureBootUEFI trazi administratorska prava, a ovaj skript se
# pokrece duplim klikom - pa bi stanje uvek ispalo "nepoznato" bas kod onoga
# ko treba da ga vidi. Ovaj kljuc cita i obican korisnik.
$secureBoot = try {
    $state = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' `
                              -Name UEFISecureBootEnabled -ErrorAction Stop
    if ($state.UEFISecureBootEnabled -eq 1) { 'ukljucen' } else { 'iskljucen' }
} catch {
    # Kljuc ne postoji na masinama sa starim BIOS-om - to nije greska nego
    # odgovor: Secure Boot tu ni ne postoji.
    'nema (legacy BIOS ili kljuc nedostupan)'
}

$memoryIntegrity = try {
    $guard = Get-CimInstance -ClassName Win32_DeviceGuard `
                             -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction Stop
    if ($guard.SecurityServicesRunning -contains 2) { 'ukljucen' } else { 'iskljucen' }
} catch { 'nepoznato' }

# bcdedit BEZ administratorskih prava ne pada tiho - ispise gresku i vrati
# nenulti kod. Bez provere koda, regex nad tom greskom ne bi nasao
# "testsigning Yes" i stanje bi se prijavilo kao "iskljucen". To je gora vest
# od "nepoznato", jer izgleda kao izmeren podatak.
$testSigning = 'nepoznato'
$bcd = & bcdedit /enum '{current}' 2>&1 | Out-String
if ($LASTEXITCODE -eq 0) {
    $testSigning = if ($bcd -match '(?im)^\s*testsigning\s+Yes') { 'ukljucen' } else { 'iskljucen' }
} elseif (-not $elevated) {
    $testSigning = 'nepoznato (skript nije pokrenut kao administrator)'
}

$os = Get-CimInstance Win32_OperatingSystem

$systemInfo = [ordered]@{
    collectedAt       = (Get-Date).ToString('o')
    osCaption         = $os.Caption
    osVersion         = $os.Version
    osBuild           = $os.BuildNumber
    architecture      = $env:PROCESSOR_ARCHITECTURE
    computerName      = $env:COMPUTERNAME
    elevated          = $elevated
    secureBoot        = $secureBoot
    memoryIntegrity   = $memoryIntegrity
    testSigning       = $testSigning
    powerShellVersion = $PSVersionTable.PSVersion.ToString()
}

# install.ps1 se pokrece kao administrator i zapisuje ono sto samo administrator
# moze da vidi. Ako je taj zapis tu, on je merodavniji od onoga sto ovaj
# neelevirani skript uspe da procita - i, sto je vaznije, on nosi stanje masine
# u TRENUTKU instalacije, a bas to test H1-A i meri.
$installState = Join-Path $here 'install-state.json'
if (Test-Path $installState) {
    $systemInfo['atInstallTime'] = (Get-Content $installState -Raw | ConvertFrom-Json)
}

Save-Json 'system-info.json' $systemInfo

# --- 2. uredjaj ------------------------------------------------------------------

Write-Host '[2/6] Uredjaj'

$devices = @(Get-PnpDevice -ErrorAction SilentlyContinue |
             Where-Object { $_.InstanceId -like '*VID_03F0&PID_2805*' } |
             ForEach-Object {
                 [ordered]@{
                     instanceId   = $_.InstanceId
                     friendlyName = $_.FriendlyName
                     status       = "$($_.Status)"
                     problem      = "$($_.Problem)"
                     class        = $_.Class
                     service      = $_.Service
                     present      = [bool]$_.Present
                 }
             })

$note = if ($devices.Count -eq 0) {
    'Skener nije prikljucen ili ga Windows ne prepoznaje.'
} else {
    'Uredjaj je prisutan.'
}

Save-Json 'device.json' ([ordered]@{
    matchedDevices = $devices
    deviceCount    = $devices.Count
    note           = $note
})

# --- 3. DriverStore ---------------------------------------------------------------

Write-Host '[3/6] DriverStore'
Save-Text 'driverstore.txt' (& pnputil /enum-drivers 2>&1)

# --- 4. g2710ctl ------------------------------------------------------------------
#
# `info` i `capabilities` ne diraju uredjaj - staticki su racun iz ugradjenog
# profila, pa rade i kada skenera nema. Zato uvek imaju smisla u izvestaju:
# iz njih se vidi TACNO koji je binarni fajl covek pokrenuo.

Write-Host '[4/6] g2710ctl'
if (Test-Path $tool) {
    Save-Text 'g2710ctl-info.txt' (& $tool info 2>&1)
    Save-Text 'capabilities.json' (& $tool capabilities --json 2>&1)
} else {
    Save-Text 'g2710ctl-info.txt' "g2710ctl.exe nije nadjen pored skripta ($here)"
}

# --- 5. izvestaj wizarda ------------------------------------------------------------

Write-Host '[5/6] Izvestaj kvalifikacije'

$chosen = $null
if ($ReportPath -and (Test-Path $ReportPath)) {
    $chosen = Get-Item $ReportPath
} else {
    $reports = @()
    $reports += @(Get-ChildItem ([Environment]::GetFolderPath('Desktop')) `
                                -Filter 'G2710-HardwareReport-*.json' -ErrorAction SilentlyContinue)
    $reports += @(Get-ChildItem $here -Filter 'test-results.json' -ErrorAction SilentlyContinue)
    $chosen = $reports | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

if ($chosen) {
    Copy-Item $chosen.FullName (Join-Path $work 'test-results.json')
    Write-Host "      test-results.json  (iz $($chosen.Name))"
} else {
    # Prazna rubrika je losa vest samo ako se ne vidi da je prazna. Zato ovde
    # stoji uputstvo, a ne fajl koji nedostaje bez objasnjenja.
    Save-Text 'test-results-NEMA.txt' @'
Wizard jos nije sacuvao izvestaj.

Pokrenite G2710.Qualification.exe, prodjite kroz proveru i kliknite
"Sacuvaj izvestaj", pa ponovo pokrenite ovaj skript.
'@
}

# --- 6. setupapi log ---------------------------------------------------------------
#
# Ceo setupapi.dev.log ume da bude desetine megabajta i pun tudjih uredjaja.
# Uzima se samo ono sto se tice nas, sa po dva reda konteksta oko nalaza.

Write-Host '[6/6] setupapi (izvod)'
$setupapi = Join-Path $env:WINDIR 'inf\setupapi.dev.log'
if (Test-Path $setupapi) {
    try {
        $hits = Select-String -Path $setupapi -Pattern 'VID_03F0&PID_2805|g2710|usbscan' `
                              -Context 2, 2 -ErrorAction Stop
        if ($hits) {
            Save-Text 'setupapi.dev.log' $hits
        } else {
            Save-Text 'setupapi.dev.log' 'Nema nijednog reda o ovom uredjaju.'
        }
    } catch {
        Save-Text 'setupapi.dev.log' "Log se ne moze procitati: $($_.Exception.Message)"
    }
} else {
    Save-Text 'setupapi.dev.log' 'Log ne postoji na ovom racunaru.'
}

# --- pakovanje -----------------------------------------------------------------------

$zip = Join-Path $OutputDirectory "G2710-HardwareReport-$stamp.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip
Remove-Item $work -Recurse -Force

Write-Host ''
Write-Host "Gotovo: $zip" -ForegroundColor Green
Write-Host 'Posaljite taj jedan fajl nazad. Nista drugo ne treba.'
Write-Host ''
