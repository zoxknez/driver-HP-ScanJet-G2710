<#
.SYNOPSIS
    Sastavlja ZIP koji ide prijatelju na testiranje.

.DESCRIPTION
    Paket sadrzi tacno ono sto treba i nista vise:

        README.txt                uputstvo na engleskom, bez zargona
        PROCITAJ-ME.txt           isto to na srpskom
        language.txt              jezik wizarda; ZIP se ne instalira, pa
                                  izbor ne moze stajati u registru
        install.ps1               sertifikat + INF, nista drugo
        collect-diagnostics.ps1   sve nazad u jedan ZIP
        G2710.Qualification.exe   wizard
        g2710ctl.exe              alat koji wizard pokrece
        g2710.inf  g2710.cat  G2710.Wia.dll  g2710-dev.cer

    PLAFON BEZBEDNOSTI SE UGRADJUJE U BINARNI FAJL. Paket napravljen sa
    -SafetyCeiling 1 ne moze se na prijateljevom racunaru "otkljucati" -
    motorni kod u njemu nije ni preveden. Zato ovaj skript i postoji: da se
    plafon bira pri PAKOVANJU, a ne pri pokretanju.

.PARAMETER SafetyCeiling
    1..5. Prvi paket ide sa 1 ili 2; visi tek kada nizi prodje.

.PARAMETER Language
    Jezik paketa: en (podrazumevano) ili sr. Odredjuje na kom jeziku govori
    wizard i koje uputstvo stoji prvo. Oba uputstva idu u ZIP u svakom slucaju -
    onaj ko otvori paket cita ono koje razume, bez obzira sta je izabrano.

.PARAMETER SkipDriver
    Preskace potpisivanje drajvera. Paket tada nosi samo wizard i g2710ctl -
    korisno dok se radi na wizardu, beskorisno za slanje.

.EXAMPLE
    powershell -File tools/build-qualification-package.ps1 -SafetyCeiling 2
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 5)]
    [int]$SafetyCeiling = 1,

    [string]$OutputDirectory,

    [ValidateSet('en', 'sr')]
    [string]$Language = 'en',

    [switch]$SkipTests,
    [switch]$SkipDriver
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

$buildDir = Join-Path $repo ".build-package-ceiling$SafetyCeiling"
$stageDir = Join-Path $OutputDirectory "stage-ceiling$SafetyCeiling"

Write-Host "== Paket sa plafonom $SafetyCeiling ==" -ForegroundColor Cyan

# --- 1. native, sa ugradjenim plafonom ---------------------------------------

Write-Host "`n[1/6] Gradim g2710ctl sa BuildSafetyCeiling=$SafetyCeiling"
cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A x64 `
      "-DG2710_BUILD_SAFETY_CEILING=$SafetyCeiling" -DG2710_BUILD_TESTS=OFF | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cmake konfiguracija nije uspela' }

cmake --build $buildDir --config Release --target g2710ctl | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'g2710ctl se nije izgradio' }

# WIA minidriver deli ISTI plafon. Da se gradi zasebno, paket bi mogao da nosi
# alat sa plafonom 1 i drajver sa plafonom 5 - a INF je taj koji Windows
# ucitava sam, bez wizarda.
if (-not $SkipDriver) {
    cmake --build $buildDir --config Release --target g2710_wia | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'G2710.Wia.dll se nije izgradio' }
}

$tool = Join-Path $buildDir 'native\cli\Release\g2710ctl.exe'
if (-not (Test-Path $tool)) { throw "nema $tool" }

# --- 2. provera da je plafon ZAISTA ugradjen ---------------------------------
#
# Bez ove provere bi se paket mogao poslati sa pogresnim plafonom, a to se ne
# bi videlo dok se ne pomeri tudji motor. Cita se izlaz samog binarnog fajla,
# ne promenljiva iz ovog skripta - jedino to nesto dokazuje.

Write-Host '[2/6] Proveravam ugradjeni plafon'
$info = & $tool info
if ($LASTEXITCODE -ne 0) { throw 'g2710ctl info je pao' }

$ceilingLine = $info | Where-Object { $_ -match '^\s*BuildSafetyCeiling\s' }
if (-not $ceilingLine) { throw 'g2710ctl info ne prijavljuje plafon' }
if ($ceilingLine -notmatch "^\s*BuildSafetyCeiling\s+$SafetyCeiling\b") {
    throw "binarni fajl prijavljuje drugi plafon: $($ceilingLine.Trim())"
}
Write-Host "      $($ceilingLine.Trim())"

# Ispod nivoa 3 motorni put ne sme ni da postoji u binarnom fajlu.
$motorLine = $info | Where-Object { $_ -match '^\s*Motor path\s' }
if ($SafetyCeiling -lt 3) {
    if ($motorLine -notmatch 'NOT compiled') {
        throw "motorni kod je preveden uprkos plafonu $SafetyCeiling"
    }
} elseif ($motorLine -match 'NOT compiled') {
    throw "motorni kod nedostaje iako ga plafon $SafetyCeiling dozvoljava"
}
Write-Host "      $($motorLine.Trim())"

# --- 3. wizard ----------------------------------------------------------------

Write-Host '[3/6] Gradim wizard'
if (-not $SkipTests) {
    dotnet test (Join-Path $repo 'managed\G2710.Qualification.Tests\G2710.Qualification.Tests.csproj') `
        -c Release --nologo | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'testovi wizarda ne prolaze - paket se ne pravi' }
}

if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

dotnet publish (Join-Path $repo 'managed\G2710.Qualification\G2710.Qualification.csproj') `
    -c Release -r win-x64 --self-contained false -o $stageDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'wizard se nije izgradio' }

Remove-Item (Join-Path $stageDir '*.pdb') -Force -ErrorAction SilentlyContinue
Copy-Item $tool $stageDir

# Oba jezika moraju STVARNO biti u paketu.
#
# Wizard se objavljuje kao jedan fajl, pa srpski satelit ne stoji pored njega
# nego unutar njega. Zato se ne moze proveriti postojanjem foldera `sr`.
#
# Otkaz je tih: dovoljno je da neko doda <SatelliteResourceLanguages>en</...>
# ili ukloni referencu na G2710.Localization, i paket se i dalje gradi, i dalje
# radi, i dalje se pokrece - samo govori engleski onome kome je poslat na
# srpskom. Nista ne pukne, pa se to primeti tek kod prijatelja.
#
# Bundle se ne kompresuje podrazumevano, pa niske stoje u fajlu doslovno.
$wizardBytes = [System.IO.File]::ReadAllBytes((Join-Path $stageDir 'G2710.Qualification.exe'))
$wizardText = [System.Text.Encoding]::UTF8.GetString($wizardBytes)
#
# Promenljiva petlje se NE sme zvati $language: imena su neosetljiva na
# velicinu slova, pa bi to bio parametar $Language - koji nosi ValidateSet i
# baca cim mu se dodeli nesto van skupa. Skript tada pukne na proveri, a poruka
# govori o parametru koji korisnik nije ni dodirnuo.
$probes = @{
    'engleski' = 'Start the check'
    'srpski'   = [char]0x005A + 'apo' + [char]0x010D + 'ni proveru'
}
foreach ($probe in $probes.Keys) {
    if (-not $wizardText.Contains($probes[$probe])) {
        throw "wizard ne nosi $probe prevod - paket bi govorio pogresnim jezikom"
    }
}
Write-Host '      oba prevoda su u wizardu'

# --- 4. drajver ----------------------------------------------------------------
#
# INF i katalog idu u ISTI direktorijum kao install.ps1, jer pnputil trazi
# katalog pored INF-a. Potpisivanje se radi u privremenom direktorijumu da
# .cat ne bi ostajao u repozitorijumu izmedju pakovanja.

if ($SkipDriver) {
    Write-Host '[4/6] Drajver PRESKOCEN (-SkipDriver) - paket nije za slanje' -ForegroundColor Yellow
} else {
    Write-Host '[4/6] Potpisujem drajver'

    # Build vec sastavlja INF + DLL u <build>\package (vidi native/wia/
    # CMakeLists.txt), jer Inf2Cat radi nad direktorijumom a ne nad fajlom.
    $driverStage = Join-Path $buildDir 'package'
    if (-not (Test-Path (Join-Path $driverStage 'G2710.Wia.dll'))) {
        throw "nema G2710.Wia.dll u $driverStage"
    }
    Remove-Item (Join-Path $driverStage '*.cat') -Force -ErrorAction SilentlyContinue

    & (Join-Path $repo 'driver\sign\sign-package.ps1') `
        -PackageDir $driverStage -SigningMode Development | Out-Null

    $cat = Get-ChildItem $driverStage -Filter '*.cat' | Select-Object -First 1
    if (-not $cat) { throw 'katalog nije nastao' }
    Write-Host "      $($cat.Name) potpisan"

    Copy-Item (Join-Path $driverStage 'g2710.inf') $stageDir
    Copy-Item (Join-Path $driverStage 'G2710.Wia.dll') $stageDir
    Copy-Item $cat.FullName $stageDir
    Copy-Item (Join-Path $repo 'driver\sign\out\g2710-dev.cer') $stageDir
}

# --- 5. skriptovi i uputstvo ------------------------------------------------------

Write-Host '[5/6] Skriptovi i uputstvo'

# Tabela izlaznih kodova pnputil-a je logika koja se moze pogresiti, a njen
# otkaz se ne vidi ovde nego na tudjoj masini - usred instalacije koja pukne
# bez objasnjenja. Zato se proverava PRE nego sto skript udje u paket.
& powershell -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $repo 'tools\package\install.ps1') -SelfTest | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'install.ps1 -SelfTest nije prosao; paket se ne pravi' }

Copy-Item (Join-Path $repo 'tools\package\install.ps1') $stageDir
Copy-Item (Join-Path $repo 'tools\package\collect-diagnostics.ps1') $stageDir

$motorNoteSr = if ($SafetyCeiling -lt 3) {
    'Ovaj paket NE POMERA glavu skenera - motorni deo u njemu nije ni ugradjen.'
} else {
    'Ovaj paket sme da pomera glavu skenera.'
}
$motorNoteEn = if ($SafetyCeiling -lt 3) {
    'This package DOES NOT MOVE the scanner head - the motor part is not even built into it.'
} else {
    'This package is allowed to move the scanner head.'
}

$installStepSr = if ($SkipDriver) {
    '(u ovom paketu nema drajvera - preskocite ovaj korak)'
} else {
    @'
2. Instalirajte drajver, jednom:
     - pritisnite Windows dugme, ukucajte  powershell
     - desni klik na "Windows PowerShell" -> "Pokreni kao administrator"
     - prekopirajte ovu liniju i pritisnite Enter:

       powershell -ExecutionPolicy Bypass -File "<putanja>\install.ps1"

   Ako pise da skener nije prikljucen - to je u redu, drajver je ipak ugradjen.
'@
}

$installStepEn = if ($SkipDriver) {
    '(this package carries no driver - skip this step)'
} else {
    @'
2. Install the driver, once:
     - press the Windows key, type  powershell
     - right-click "Windows PowerShell" -> "Run as administrator"
     - copy this line and press Enter:

       powershell -ExecutionPolicy Bypass -File "<path>\install.ps1"

   If it says the scanner is not connected - that is fine, the driver went in
   anyway.
'@
}

# Uputstvo ide na OBA jezika, uvek.
#
# Paket se salje jednom coveku, ali ga cesto otvori neko drugi - i jedini fajl
# koji objasnjava sta se dogadja ne sme biti na jeziku koji taj ne cita.
# -Language bira samo cime program govori, ne sta se sme procitati.
$readmeEn = @"
HP ScanJet G2710 - scanner check
================================

What this is
------------
A program that checks whether the scanner works with the new driver. It changes
no settings on the computer, switches off no protection, and takes nothing apart.

Order
-----
1. Unpack the whole ZIP into one folder. Do not run the programs from inside
   the ZIP.

$installStepEn

3. Connect the scanner with the USB cable and switch it on.
   Close the lid. Leave the glass empty.

4. Double-click  G2710.Qualification.exe

   The program checks everything by itself and asks two questions the computer
   cannot answer. It takes less than a minute.

5. At the end click "Save the report".

   The program collects the information about the computer and packs everything
   up. On the desktop you will find  G2710-HardwareReport-<date>.zip
   Send that one file back.

   (If the program says the ZIP was not created, the report was still saved.
   Then run  collect-diagnostics.ps1  - right-click, "Run with PowerShell" -
   and it makes the same ZIP.)

Windows Scan and Paint will not work yet
----------------------------------------
If you open Windows Scan, Fax and Scan, or Paint, the scanner will look "not
ready". That is on purpose, not a fault: the driver offers those programs only
what has already been confirmed on real hardware, and so far nothing has. The
check you are running is exactly what confirms it. Please do not report that as
a problem - report what the check itself says.

If something does not pass
--------------------------
That is all right, and it is the point. The report exists precisely so that it
is visible WHAT does not pass. The scanner will not be damaged by it.

How everything goes back as it was
----------------------------------
In the same PowerShell window, as administrator:

  powershell -ExecutionPolicy Bypass -File "<path>\install.ps1" -Uninstall

That removes both the driver and the certificate. Nothing else was added.

About this package
------------------
Safety ceiling: $SafetyCeiling out of 5.
$motorNoteEn

Checks above the ceiling are marked as skipped in the report. That is intent,
not a fault.

Language: the program speaks $Language. To change it, open  language.txt
next to the program and write  en  or  sr  in it.
"@

$readmeSr = @"
HP ScanJet G2710 - provera skenera
==================================

Sta je ovo
----------
Program koji proverava da li skener radi sa novim drajverom. Ne menja
postavke racunara, ne gasi nikakvu zastitu i ne rastavlja skener.

Redosled
--------
1. Raspakujte ceo ZIP u jedan folder. Ne pokrecite programe iz ZIP-a.

$installStepSr

3. Prikljucite skener USB kablom i ukljucite ga.
   Zatvorite poklopac. Staklo ostavite prazno.

4. Dva puta kliknite na  G2710.Qualification.exe

   Program ce sam sve proveriti i postaviti dva pitanja na koja racunar ne
   moze da odgovori. Traje manje od minuta.

5. Na kraju kliknite "Sacuvaj izvestaj".

   Program ce sam sakupiti i podatke o racunaru i sve spakovati. Na radnoj
   povrsini ce se pojaviti  G2710-HardwareReport-<datum>.zip
   Posaljite samo taj jedan fajl.

   (Ako program javi da ZIP nije napravljen, izvestaj je ipak sacuvan.
   Tada pokrenite  collect-diagnostics.ps1  - desni klik, "Run with
   PowerShell" - i on ce napraviti isti ZIP.)

Windows Scan i Paint jos nece raditi
------------------------------------
Ako otvorite Windows Scan, Fax and Scan ili Paint, skener ce izgledati kao da
"nije spreman". To je namerno, a ne kvar: drajver tim programima nudi samo ono
sto je vec potvrdjeno na pravom hardveru, a do sada nije potvrdjeno nista.
Provera koju pokrecete je upravo ono cime se to potvrdjuje. Nemojte to
prijavljivati kao problem - prijavite ono sto sama provera kaze.

Ako nesto ne prodje
-------------------
To je u redu i tako treba. Izvestaj je i napravljen zato da se vidi STA ne
prodje. Skener zbog toga nece biti ostecen.

Kako se sve vraca kako je bilo
------------------------------
U istom PowerShell prozoru kao administrator:

  powershell -ExecutionPolicy Bypass -File "<putanja>\install.ps1" -Uninstall

To uklanja i drajver i sertifikat. Nista drugo nije ni dodato.

Napomena o ovom paketu
----------------------
Plafon bezbednosti: $SafetyCeiling od 5.
$motorNoteSr

Provere iznad plafona bice u izvestaju oznacene kao preskocene. To nije
greska nego namera.

Jezik: program govori $Language. Menja se tako sto se otvori  language.txt
pored programa i u njega upise  en  ili  sr.
"@

$readmeEn | Set-Content (Join-Path $stageDir 'README.txt') -Encoding UTF8
$readmeSr | Set-Content (Join-Path $stageDir 'PROCITAJ-ME.txt') -Encoding UTF8

# Wizard ovo cita pri pokretanju. Bez fajla bi paket poslat na srpskom
# progovorio engleski cim bi ga neko otvorio na engleskom Windows-u.
$Language | Set-Content (Join-Path $stageDir 'language.txt') -Encoding ASCII

# --- 6. ZIP -----------------------------------------------------------------------

Write-Host '[6/6] Pakujem'
$stamp = Get-Date -Format 'yyyyMMdd'
$zip = Join-Path $OutputDirectory "G2710-HardwareQualification-$stamp-ceiling$SafetyCeiling-$Language.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zip
Remove-Item $stageDir -Recurse -Force

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ''
Write-Host "Gotovo: $zip ($size MB)" -ForegroundColor Green
