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
    [switch]$RequireDevelopmentCertificate
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
    Write-Host 'MSI struktura i sadrzaj su provereni.' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
