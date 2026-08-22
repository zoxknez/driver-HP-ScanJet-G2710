[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateSet('Development', 'Release')]
    [string]$SigningMode = 'Development'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
$publish = Join-Path $repo 'build\installer-app'
$driver = Join-Path $repo 'build\installer-driver'
$twain64 = Join-Path $repo 'build\installer-twain-x64'
$twain86 = Join-Path $repo 'build\installer-twain-x86'
dotnet publish (Join-Path $repo 'managed\G2710.App\G2710.App.csproj') -c Release -r win-x64 --self-contained false -o $publish | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'publish aplikacije nije uspeo' }
foreach ($file in 'G2710.App.exe','G2710.Native.dll','G2710.Interop.dll') { if (-not (Test-Path (Join-Path $publish $file))) { throw "nema $file uz aplikaciju" } }
& (Join-Path $repo 'tools\verify-app-startup.ps1') -AppPath (Join-Path $publish 'G2710.App.exe')
cmake --build (Join-Path $repo 'build') --config Release --target g2710_wia | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'WIA driver se nije izgradio' }
cmake --build (Join-Path $repo 'build') --config Release --target g2710_twain | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'TWAIN x64 se nije izgradio' }
cmake --build (Join-Path $repo 'build-x86') --config Release --target g2710_twain | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'TWAIN x86 se nije izgradio' }
if (Test-Path $driver) { Remove-Item -LiteralPath $driver -Recurse -Force }
New-Item -ItemType Directory -Path $driver | Out-Null
Copy-Item (Join-Path $repo 'build\package\g2710.inf') $driver
Copy-Item (Join-Path $repo 'build\package\G2710.Wia.dll') $driver
Copy-Item (Join-Path $repo 'tools\package\install.ps1') $driver
& (Join-Path $repo 'driver\sign\sign-package.ps1') -PackageDir $driver -SigningMode $SigningMode
if ($LASTEXITCODE -ne 0) { throw "Potpisivanje drajvera nije uspelo ($SigningMode)." }
$catalog = Get-ChildItem -LiteralPath $driver -Filter '*.cat' | Select-Object -First 1
if (-not $catalog) { throw 'Potpisani katalog nije nastao.' }
$includeDevCertificate = 0
if ($SigningMode -eq 'Development') {
    $certificate = Join-Path $repo 'driver\sign\out\g2710-dev.cer'
    if (-not (Test-Path $certificate)) { throw "Nema razvojnog sertifikata: $certificate" }
    Copy-Item $certificate $driver
    $includeDevCertificate = 1
}
foreach ($pair in @(
    @{ Source = (Join-Path $repo 'build\native\twain\Release\G2710.Twain.dll'); Destination = $twain64 },
    @{ Source = (Join-Path $repo 'build-x86\native\twain\Release\G2710.Twain.dll'); Destination = $twain86 }
)) {
    if (-not (Test-Path $pair.Source)) { throw "nema TWAIN fajla: $($pair.Source)" }
    if (Test-Path $pair.Destination) { Remove-Item -LiteralPath $pair.Destination -Recurse -Force }
    New-Item -ItemType Directory -Path $pair.Destination | Out-Null
    Copy-Item $pair.Source $pair.Destination
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$msi = Join-Path $OutputDirectory 'G2710-0.1.0-x64.msi'
$wix = (Get-Command wix -ErrorAction SilentlyContinue).Source
if (-not $wix) { $wix = 'C:\Program Files\WiX Toolset v7.0\bin\wix.exe' }
if (-not (Test-Path $wix)) { throw 'WiX CLI nije pronadjen; instalirajte WiXToolset.WiXCLI.' }
& $wix build -arch x64 -d "AppDir=$publish" -d "DriverDir=$driver" `
    -d "Twain64Dir=$twain64" -d "Twain86Dir=$twain86" `
    -d "IncludeDevCertificate=$includeDevCertificate" -o $msi (Join-Path $repo 'installer\Product.wxs')
if ($LASTEXITCODE -ne 0) { throw 'WiX build nije uspeo' }
if (-not (Test-Path $msi)) { throw 'MSI nije nastao' }
$verification = @{ MsiPath = $msi }
if ($SigningMode -eq 'Development') { $verification.RequireDevelopmentCertificate = $true }
& (Join-Path $repo 'tools\verify-installer.ps1') @verification
Write-Host "Gotovo: $msi" -ForegroundColor Green
