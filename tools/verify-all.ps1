<#
.SYNOPSIS
    Sve offline kapije, jednim pozivom, sa ispravnim izlaznim kodom.

.DESCRIPTION
    Postoji zato sto su kapije postojale a niko ih nije zvao.

    verify-reference-gates.py u svom opisu kaze "Namenjeno CI-ju" - a CI nije
    postojao. verify-source-hygiene.py se pominjao samo u README-u, kao nesto
    sto covek otkuca ako se seti. Posledica je izmerena: tri ne-ASCII znaka su
    usla u izvor i prosla kroz commit.

    IZLAZNI KOD JE POENTA. Svaka provera se poziva tako da se njen neuspeh vidi;
    nista se ne provlaci kroz cev koja bi kod zamenila svojim. Neuspeh bilo koje
    kapije obara ceo poziv.

.PARAMETER GatesOnly
    Samo jeftine provere (poreklo koda i higijena izvora). Traje manje od
    sekunde; ovo zovu skripte za pakovanje pre nego sto bilo sta posalju.

.PARAMETER SkipManaged
    Preskace .NET testove. Za masinu bez .NET SDK-a.

.EXAMPLE
    powershell -File tools/verify-all.ps1
    powershell -File tools/verify-all.ps1 -GatesOnly
#>
[CmdletBinding()]
param(
    [switch]$GatesOnly,
    [switch]$SkipManaged
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$failures = @()

# Jedno mesto na kome se meri uspeh. Native alati javljaju kroz $LASTEXITCODE, a
# on se cita ODMAH - svaka sledeca komanda ga prepise.
function Invoke-Gate {
    param([string]$Name, [scriptblock]$Body)

    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    $global:LASTEXITCODE = 0
    try {
        & $Body
        $code = $LASTEXITCODE
    } catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
        $code = 1
    }
    if ($code -ne 0) {
        $script:failures += "$Name (izlazni kod $code)"
        Write-Host "PAO: $Name" -ForegroundColor Red
    }
}

Invoke-Gate 'poreklo koda (G2710-0 kapije A i B)' {
    & python (Join-Path $repo 'tools\verify-reference-gates.py')
}

Invoke-Gate 'higijena izvora' {
    & python (Join-Path $repo 'tools\verify-source-hygiene.py')
}

if (-not $GatesOnly) {
    foreach ($pair in @(
        @{ Name = 'native x64'; Dir = 'build' },
        @{ Name = 'native x86'; Dir = 'build-x86' }
    )) {
        $directory = Join-Path $repo $pair.Dir
        if (-not (Test-Path -LiteralPath $directory)) {
            $failures += "$($pair.Name): nema $directory - pokrenite cmake -S . -B $($pair.Dir)"
            Write-Host "PRESKOCENO: $($pair.Name) - nema $directory" -ForegroundColor Red
            continue
        }
        Invoke-Gate "$($pair.Name): prevodjenje" {
            & cmake --build $directory --config Release
        }
        # -C Release nije opciono: tri testa su registrovana po konfiguraciji i
        # bez toga se prijave kao "Not Run", sto ctest broji kao pad.
        Invoke-Gate "$($pair.Name): testovi" {
            & ctest --test-dir $directory -C Release --output-on-failure
        }
    }

    if (-not $SkipManaged) {
        Invoke-Gate 'upravljani testovi' {
            & dotnet test (Join-Path $repo 'managed\G2710.sln') -c Release --nologo
        }
    }
}

Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host 'Sve kapije prolaze.' -ForegroundColor Green
    exit 0
}
Write-Host "PALO: $($failures.Count)" -ForegroundColor Red
$failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
exit 1
