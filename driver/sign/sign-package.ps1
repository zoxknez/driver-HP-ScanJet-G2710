<#
.SYNOPSIS
    Gradi i potpisuje G2710 driver paket.

.DESCRIPTION
    Ceo lanac, u redosledu koji Microsoft propisuje za INF-installed PnP pakete:

        InfVerif  -> strukturna provera INF-a
        Inf2Cat   -> .cat za ciljne OS verzije
        signtool  -> potpis kataloga
        signtool  -> verifikacija potpisa

    MakeCat se NE koristi - on je za rucno pravljene kataloge, ne za INF
    pakete.

.PARAMETER SigningMode
    Development  self-signed sertifikat sa ove masine
    Release      produkcioni potpis (Partner Center / EV). Paket je ISTI -
                 menja se samo potpis, ne drajver.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDir,

    [ValidateSet('Development', 'Release')]
    [string]$SigningMode = 'Development',

    [string]$PfxPath = (Join-Path $PSScriptRoot 'out\g2710-dev.pfx'),
    [string]$PfxPassword = 'g2710-dev',

    # Windows 10 2004-22H2 - Windows 11 22H2 / 24H2 / 25H2, sve x64.
    # Windows Server nije cilj projekta, pa Server* tokeni ne ulaze.
    [string]$OsTargets = '10_VB_X64,10_NI_X64,10_GE_X64,10_25H2_X64',

    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

function Find-KitTool {
    param([string]$Name, [string[]]$Roots)
    foreach ($root in $Roots) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem -Path $root -Filter $Name -Recurse -ErrorAction SilentlyContinue |
                 Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "$Name nije nadjen. Instaliraj WDK za Visual Studio 2022."
}

$kitRoot = 'C:\Program Files (x86)\Windows Kits\10'

# InfVerif je u Tools\, a NE u bin\ - lako se promasi.
$infVerif = Find-KitTool -Name 'infverif.exe' -Roots @("$kitRoot\Tools")
# Inf2Cat postoji samo kao x86.
$inf2Cat  = Find-KitTool -Name 'Inf2Cat.exe'  -Roots @("$kitRoot\bin")
$signTool = Find-KitTool -Name 'signtool.exe' -Roots @("$kitRoot\bin")

if (-not (Test-Path $PackageDir)) {
    throw "Paket ne postoji: $PackageDir"
}
$PackageDir = (Resolve-Path $PackageDir).Path
$inf = Get-ChildItem -Path $PackageDir -Filter '*.inf' | Select-Object -First 1
if (-not $inf) {
    throw "Nema .inf fajla u $PackageDir"
}

# Ikone koje INF obecava moraju stvarno biti u DLL-u.
#
# Ovo se ne moze proveriti ni iz izvora ni iz INF-a - jedino iz sastavljenog
# fajla. Izmereno je jednom da ih nema nijedne, dok resursne skripte nije ni
# bilo; a i kada su dodate, INF ih je trazio rednim brojem umesto ID-om, pa ih
# Windows opet nije nalazio. Nijedan od ta dva kvara ne prijavljuje gresku:
# INF se overava, paket se potpisuje, drajver se instalira. Samo je slika u
# kartici "Dogadjaji" prazna.
#
# Zato se ovde meri isto ono sto radi i Windows: ExtractIconEx nad DLL-om, sa
# brojem prepisanim iz INF-a.
$wiaDll = Get-ChildItem -Path $PackageDir -Filter 'G2710.Wia.dll' | Select-Object -First 1
if ($wiaDll) {
    if (-not ('IconProbe' -as [type])) {
        Add-Type -Namespace '' -Name 'IconProbe' -MemberDefinition @'
[DllImport("shell32.dll", CharSet = CharSet.Unicode)]
public static extern uint ExtractIconEx(string file, int index, IntPtr[] large, IntPtr[] small, uint count);
[DllImport("user32.dll")]
public static extern bool DestroyIcon(IntPtr icon);
'@
    }

    $infText = Get-Content -LiteralPath $inf.FullName -Raw
    $iconReferences = [regex]::Matches($infText, 'Events\\(\w+),Icon,,"[^"]*G2710\.Wia\.dll,(-?\d+)"')
    if ($iconReferences.Count -eq 0) {
        throw 'INF ne pominje nijednu ikonu dugmadi; Events podkljuc je nepotpun.'
    }
    foreach ($reference in $iconReferences) {
        $button = $reference.Groups[1].Value
        $index = [int]$reference.Groups[2].Value
        $handles = New-Object IntPtr[] 1
        $found = [IconProbe]::ExtractIconEx($wiaDll.FullName, $index, $handles, $null, 1)
        if ($found -lt 1 -or $handles[0] -eq [IntPtr]::Zero) {
            throw ("Dugme {0}: INF trazi ikonu {1}, a G2710.Wia.dll je nema. " -f $button, $index) +
                  'Nenegativan broj je REDNI BROJ ikone; ID resursa se pise negativnim brojem.'
        }
        [void][IconProbe]::DestroyIcon($handles[0])
    }
    Write-Host ("      ikone dugmadi: {0} od {0} nadjeno" -f $iconReferences.Count)
}

Write-Host "=== 1/4  InfVerif ==="
# Napomena: /w (universal driver) rezim odbija HKCR AddReg, koji nam treba za
# COM registraciju WIA minidriver-a. Vidi docs/SIGNING.md - odluka o universal
# paketu se donosi u G2710-12, kada znamo da li idemo na Partner Center.
& $infVerif $inf.FullName
if ($LASTEXITCODE -ne 0) { throw "InfVerif je pao ($LASTEXITCODE)" }

Write-Host "=== 2/4  Inf2Cat  os=$OsTargets ==="
& $inf2Cat "/driver:$PackageDir" "/os:$OsTargets"
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat je pao ($LASTEXITCODE)" }

$cat = Get-ChildItem -Path $PackageDir -Filter '*.cat' | Select-Object -First 1
if (-not $cat) { throw "Inf2Cat nije proizveo katalog" }

Write-Host "=== 3/4  signtool sign  ($SigningMode) ==="
if ($SigningMode -eq 'Development') {
    if (-not (Test-Path $PfxPath)) {
        throw "Nema razvojnog sertifikata: $PfxPath  (pokreni make-dev-cert.ps1)"
    }
    & $signTool sign /fd SHA256 /f $PfxPath /p $PfxPassword /tr $TimestampUrl /td SHA256 $cat.FullName
} else {
    # Release: kljuc dolazi iz sertifikata instaliranog na potpisnickoj masini
    # (EV token / HSM), nikada iz fajla u repozitorijumu.
    & $signTool sign /fd SHA256 /a /tr $TimestampUrl /td SHA256 $cat.FullName
}
if ($LASTEXITCODE -ne 0) { throw "signtool sign je pao ($LASTEXITCODE)" }

Write-Host "=== 4/4  signtool verify ==="
& $signTool verify /pa /v $cat.FullName
if ($LASTEXITCODE -ne 0) { throw "signtool verify je pao ($LASTEXITCODE)" }

Write-Host ""
Write-Host "Paket potpisan: $($cat.FullName)"
