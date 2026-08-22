<#
.SYNOPSIS
    Pravi proverljiv raspakovani layout aplikacije i drajvera.

.DESCRIPTION
    Ovo nije zamena za MSI: TWAIN još nije implementiran, pa bi MSI sada
    oglašavao komponentu koju ne može da isporuči. Layout je ipak kompletan za
    aplikaciju + WIA: native DLL stoji pored EXE-a, a INF/DLL su zajedno za
    pnputil. Kada TWAIN prođe harness, isti layout postaje ulaz za WiX.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [switch]$SkipDriver,
    [switch]$VerifyStartup
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist\G2710-ProductLayout' }
$build = Join-Path $repo 'build'
$buildX86 = Join-Path $repo 'build-x86'

cmake --build $build --config Release --target g2710_native | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'G2710.Native.dll se nije izgradio' }
if (-not $SkipDriver) {
    cmake --build $build --config Release --target g2710_wia | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'G2710.Wia.dll se nije izgradio' }
}
cmake --build $build --config Release --target g2710_twain | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'x64 G2710.Twain.dll se nije izgradio' }
cmake --build $buildX86 --config Release --target g2710_twain | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'x86 G2710.Twain.dll se nije izgradio' }

if (Test-Path $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$app = Join-Path $OutputDirectory 'app'
dotnet publish (Join-Path $repo 'managed\G2710.App\G2710.App.csproj') -c Release -r win-x64 --self-contained false -o $app | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'objavljivanje aplikacije nije uspelo' }

$required = 'G2710.App.exe', 'G2710.Interop.dll', 'G2710.Native.dll'
foreach ($name in $required) {
    if (-not (Test-Path (Join-Path $app $name))) { throw "objavljeni layout nema $name" }
}
if ($VerifyStartup) {
    & (Join-Path $repo 'tools\verify-app-startup.ps1') -AppPath (Join-Path $app 'G2710.App.exe')
}

if (-not $SkipDriver) {
    $driver = Join-Path $OutputDirectory 'driver'
    New-Item -ItemType Directory -Path $driver | Out-Null
    $package = Join-Path $build 'package'
    foreach ($name in 'g2710.inf', 'G2710.Wia.dll') {
        $source = Join-Path $package $name
        if (-not (Test-Path $source)) { throw "driver paket nema $name" }
        Copy-Item $source $driver
    }
    Copy-Item (Join-Path $repo 'tools\package\install.ps1') $driver
}

# DSM otkriva izvore po arhitekturi, zato layout cuva obe putanje kao i MSI.
$twain64 = Join-Path $OutputDirectory 'twain_64'
$twain32 = Join-Path $OutputDirectory 'twain_32'
New-Item -ItemType Directory -Path $twain64, $twain32 | Out-Null
$twain64Source = Join-Path $build 'native\twain\Release\G2710.Twain.dll'
$twain32Source = Join-Path $buildX86 'native\twain\Release\G2710.Twain.dll'
foreach ($source in $twain64Source, $twain32Source) {
    if (-not (Test-Path -LiteralPath $source)) { throw "TWAIN build nema DLL: $source" }
}
Copy-Item -LiteralPath $twain64Source -Destination (Join-Path $twain64 'G2710.Twain.ds')
Copy-Item -LiteralPath $twain32Source -Destination (Join-Path $twain32 'G2710.Twain.ds')

@{
    version = '0.1.0'; architecture = 'x64'; twain = @{
        x64 = 'twain_64\G2710.Twain.ds'; x86 = 'twain_32\G2710.Twain.ds'
    };
    app = $required; driver = -not $SkipDriver
} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'manifest.json') -Encoding utf8
Write-Host "Gotovo: $OutputDirectory" -ForegroundColor Green
