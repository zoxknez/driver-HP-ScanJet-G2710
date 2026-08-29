<#
.SYNOPSIS
    Proverava gotov MSI bez instaliranja na racunar.

.DESCRIPTION
    WiX validacija meri MSI tabelu, a dekompilacija meri stvarni sadrzaj
    kabineta. Obe provere su offline i bezbedne za razvojnu masinu.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MsiPath,
    [switch]$RequireDevelopmentCertificate,

    # Verzija koju MSI mora prijaviti, i koju mora nositi i aplikacija u njemu.
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $MsiPath)) { throw "MSI ne postoji: $MsiPath" }

$wix = (Get-Command wix -ErrorAction SilentlyContinue).Source
if (-not $wix) { $wix = 'C:\Program Files\WiX Toolset v7.0\bin\wix.exe' }
if (-not (Test-Path -LiteralPath $wix)) { throw 'WiX CLI nije pronadjen.' }

& $wix msi validate $MsiPath
if ($LASTEXITCODE -ne 0) { throw 'WiX MSI validacija nije prosla.' }

$temporary = Join-Path $env:TEMP ("g2710-msi-verify-" + $PID)
$decompiled = Join-Path $temporary 'decompiled'
try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    # `wix msi decompile` sam pravi odredisni direktorijum i odbija vec
    # postojeci; zato se prosledjuje jos-nepostojeci poddirektorijum.
    & $wix msi decompile $MsiPath -o $decompiled | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'MSI dekompilacija nije uspela.' }

    $text = (Get-ChildItem -LiteralPath $decompiled -Recurse -Filter *.wxs |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    foreach ($required in 'G2710.App.exe', 'G2710.Native.dll', 'G2710.Interop.dll',
                             'G2710.Wia.dll', 'g2710.inf', 'g2710.cat', 'install.ps1', 'G2710.Twain.ds') {
        if ($text -notmatch [regex]::Escape($required)) { throw "MSI nema obavezan fajl: $required" }
    }
    if (([regex]::Matches($text, [regex]::Escape('G2710.Twain.ds'))).Count -ne 2) {
        throw 'MSI mora sadrzati tacno dva TWAIN DLL-a (x64 i x86).'
    }
    # Verzija MSI-ja i verzija aplikacije u njemu MORAJU biti ista.
    #
    # Ranije su bile razlicite: MSI je nosio 0.1.0, a aplikacija nije imala
    # verziju uopste, pa je prijavljivala 1.0.0.0. Sa tudjeg racunara se tada
    # nije moglo utvrditi koji je build tamo - isti problem kao trag koji ne
    # belezi identitet uredjaja.
    if ($ExpectedVersion) {
        if ($text -notmatch ('Version="' + [regex]::Escape($ExpectedVersion) + '"')) {
            throw "MSI ne prijavljuje verziju $ExpectedVersion."
        }
    }

    foreach ($directory in 'twain_64', 'twain_32') {
        if ($text -notmatch ('Name="' + [regex]::Escape($directory) + '"')) {
            throw "MSI ne rasporedjuje TWAIN u C:\Windows\$directory."
        }
    }
    # Ovo nije samo fajl u MSI-ju: akcije moraju stvarno da povezu instalaciju
    # i deinstalaciju sa proverenom pnputil/certificate procedurom.
    foreach ($action in 'InstallDriverPackage', 'RemoveDriverPackage') {
        if ($text -notmatch [regex]::Escape($action)) {
            throw "MSI nema obaveznu akciju za drajver: $action"
        }
    }
    if ($text -notmatch [regex]::Escape('powershell.exe') -or
        $text -notmatch [regex]::Escape('-Uninstall -Quiet')) {
        throw 'MSI nema proverljiv install/uninstall poziv za driver paket.'
    }
    if ($RequireDevelopmentCertificate -and $text -notmatch [regex]::Escape('g2710-dev.cer')) {
        throw 'Development MSI nema javni sertifikat potreban install.ps1 skriptu.'
    }
    if ($text -notmatch [regex]::Escape('G2710.Localization.resources.dll')) {
        throw 'MSI ne nosi srpski satelit; instalacija bi ponudila jezik koji ne postoji.'
    }

    # --- izbor jezika se meri na TABELAMA, ne na tekstu ---------------------
    #
    # Prva verzija ovog dijaloga se prevodila bez ijedne greske, stajala je u
    # dekompilovanom WXS-u, i NIJE SE POJAVLJIVALA. Dugme "Install" na licencnom
    # dijalogu vec nosi EndDialog na redu 2, a dodati NewDialog je dobio veci
    # red - MSI je zavrsio dijalog pre nego sto bi stigao do naseg. To se u
    # tekstu ne vidi; vidi se samo u brojevima u tabelama.
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.GetType().InvokeMember(
        'OpenDatabase', 'InvokeMethod', $null, $installer, @($MsiPath, 0))

    # Svaki red se ispisuje kao JEDAN objekat. Bez -NoEnumerate PowerShell
    # razmota niz polja u zaseban izlaz po polju, pa red prestane da bude red.
    function Read-Table([string]$sql) {
        $view = $database.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $database, @($sql))
        [void]$view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null)
        while ($true) {
            $record = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
            if (-not $record) { break }
            $count = $record.GetType().InvokeMember('FieldCount', 'GetProperty', $null, $record, $null)
            $row = @()
            for ($i = 1; $i -le $count; $i++) {
                $row += [string]$record.GetType().InvokeMember('StringData', 'GetProperty', $null, $record, @($i))
            }
            Write-Output -NoEnumerate $row
        }
    }

    $sequence = @{}
    foreach ($row in @(Read-Table 'SELECT * FROM `InstallUISequence`')) {
        $sequence[$row[0]] = [int]$row[2]
    }
    if (-not $sequence.ContainsKey('G2710LanguageDlg')) {
        throw 'MSI ne prikazuje dijalog za izbor jezika.'
    }
    if (-not $sequence.ContainsKey('WelcomeEulaDlg')) {
        throw 'MSI nema licencni dijalog.'
    }
    if ($sequence['G2710LanguageDlg'] -ge $sequence['WelcomeEulaDlg']) {
        throw ('Dijalog za jezik je na mestu {0}, a licencni na {1} - jezik se nikada ne bi pitao.' -f
               $sequence['G2710LanguageDlg'], $sequence['WelcomeEulaDlg'])
    }

    # Dugme mora ZAVRSITI dijalog povratkom u sekvencu. Bez ovoga bi izbor
    # stajao na ekranu i instalacija ne bi krenula.
    $ends = @(Read-Table "SELECT * FROM ``ControlEvent`` WHERE ``Dialog_``='G2710LanguageDlg'" |
        Where-Object { $_[1] -eq 'Next' -and $_[2] -eq 'EndDialog' -and $_[3] -eq 'Return' })
    if ($ends.Count -eq 0) { throw 'Dugme Next na dijalogu za jezik ne nastavlja instalaciju.' }

    $languages = @(Read-Table "SELECT * FROM ``RadioButton`` WHERE ``Property``='G2710LANGUAGE'" |
        ForEach-Object { $_[2] })
    foreach ($language in 'en', 'sr') {
        if ($languages -notcontains $language) { throw "Dijalog ne nudi jezik: $language" }
    }

    $default = @(Read-Table "SELECT * FROM ``Property`` WHERE ``Property``='G2710LANGUAGE'" |
        ForEach-Object { $_[1] })[0]
    if ($default -ne 'en') {
        throw "Podrazumevani jezik je '$default', a mora biti 'en'."
    }

    # Vrednost mora zavrsiti tacno tamo gde je Language.cs trazi.
    $written = @(@(Read-Table 'SELECT * FROM `Registry`') |
        Where-Object { $_[2] -eq 'SOFTWARE\G2710' -and $_[3] -eq 'Language' })
    if ($written.Count -eq 0) { throw 'MSI ne upisuje izabrani jezik u HKLM\SOFTWARE\G2710.' }
    if ($written[0][4] -ne '[G2710LANGUAGE]') {
        throw ("U registar se upisuje '{0}', a ne izbor sa dijaloga." -f $written[0][4])
    }
    Write-Host 'MSI struktura i sadrzaj su provereni.' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
