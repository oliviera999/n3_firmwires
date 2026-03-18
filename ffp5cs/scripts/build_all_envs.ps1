# Compile tous les environnements firmware FFP5CS + tests natifs Unity.
# Usage : .\scripts\build_all_envs.ps1   (depuis firmwires/ffp5cs)
# Les envs WROOM pioarduino en parallele peuvent corrompre le cache ; build sequentiel.

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot\..

$envs = @(
    "wroom-prod",
    "wroom-beta",
    "wroom-test",
    "wroom-tls-test",
    "wroom-s3-test",
    "wroom-s3-prod",
    "wroom-s3-test-psram",
    "wroom-s3-test-psram-v2",
    "wroom-s3-test-devkit",
    "wroom-s3-test-usb"
)

foreach ($e in $envs) {
    Write-Host "=== pio run -e $e ===" -ForegroundColor Cyan
    pio run -e $e
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "=== pio test -c platformio-native.ini -e native (compilation) ===" -ForegroundColor Cyan
pio test -c platformio-native.ini -e native --without-testing
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "OK : tous les envs + tests natifs compilent." -ForegroundColor Green
