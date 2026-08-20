<#
.SYNOPSIS
    Dokazuje da motorni kod NE POSTOJI u build-u sa BuildSafetyCeiling < 3.

.DESCRIPTION
    docs/SAFETY.md tvrdi da plafon nije samo runtime provera nego odsustvo
    koda. Tvrdnja koja se ne meri je zelja, pa je ovo meri: gradi jezgro sa
    plafonom 1 i sa plafonom 5, pa uporedjuje simbole u statickoj biblioteci.

    Ocekivano:
      plafon 5  -> applyMotorCurrent postoji
      plafon 1  -> applyMotorCurrent NE postoji
      lampStatus (nivo 1) postoji u OBA - inace bi test prolazio zato sto se
      nista nije prevelo, a ne zato sto motorni put nedostaje.

    Izlaz 0 = tvrdnja vazi.

.EXAMPLE
    powershell -File tools/verify-safety-ceiling.ps1
#>
[CmdletBinding()]
param(
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Find-DumpBin {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere nije nadjen' }

    foreach ($install in (& $vswhere -all -products * -property installationPath)) {
        $msvc = Join-Path $install 'VC\Tools\MSVC'
        if (-not (Test-Path $msvc)) { continue }
        $found = Get-ChildItem $msvc -Filter 'dumpbin.exe' -Recurse -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match 'Hostx64\\x64' } |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw 'dumpbin.exe nije nadjen'
}

function Build-Core {
    param([int]$Ceiling, [string]$BuildDir)

    cmake -S $root -B $BuildDir -G $Generator -A $Architecture `
          "-DG2710_BUILD_SAFETY_CEILING=$Ceiling" -DG2710_BUILD_TESTS=OFF | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "cmake configure je pao za plafon $Ceiling" }

    cmake --build $BuildDir --config Release --target g2710_core | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "build je pao za plafon $Ceiling" }

    $lib = Join-Path $BuildDir 'native\core\Release\g2710_core.lib'
    if (-not (Test-Path $lib)) { throw "biblioteka nije nastala: $lib" }
    return $lib
}

function Count-Symbol {
    param([string]$DumpBin, [string]$Library, [string]$Pattern)
    $symbols = & $DumpBin /SYMBOLS $Library 2>$null
    return ($symbols | Select-String -Pattern $Pattern -SimpleMatch | Measure-Object).Count
}

$dumpbin = Find-DumpBin
Write-Host "dumpbin: $dumpbin"

$failures = 0

# Marker motornog puta i kontrolni marker koji mora postojati u oba build-a.
$motorSymbol = 'applyMotorCurrent'
$controlSymbol = 'lampStatus'

foreach ($case in @(@{Ceiling = 5; Expect = $true}, @{Ceiling = 1; Expect = $false})) {
    $ceiling = $case.Ceiling
    $dir = Join-Path $root ".build-ceiling$ceiling"

    Write-Host ""
    Write-Host "=== plafon $ceiling ==="
    $lib = Build-Core -Ceiling $ceiling -BuildDir $dir

    $motor = Count-Symbol -DumpBin $dumpbin -Library $lib -Pattern $motorSymbol
    $control = Count-Symbol -DumpBin $dumpbin -Library $lib -Pattern $controlSymbol

    Write-Host ("  {0,-20} {1}" -f $motorSymbol, $motor)
    Write-Host ("  {0,-20} {1}" -f $controlSymbol, $control)

    if ($control -eq 0) {
        Write-Host "  FAIL: kontrolni simbol nedostaje - jezgro se nije prevelo kako treba"
        $failures++
    }

    if ($case.Expect -and $motor -eq 0) {
        Write-Host "  FAIL: motorni kod nedostaje iako plafon $ceiling to dozvoljava"
        $failures++
    }
    if (-not $case.Expect -and $motor -ne 0) {
        Write-Host "  FAIL: motorni kod POSTOJI u build-u sa plafonom $ceiling"
        $failures++
    }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "OK: motorni put postoji na plafonu 5 i ne postoji na plafonu 1"
    exit 0
}

Write-Host "NEUSPEH: $failures provera nije proslo"
exit 1
