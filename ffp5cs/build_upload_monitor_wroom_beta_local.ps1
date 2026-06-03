# Build wroom-beta-local, upload et moniteur série.
# Prérequis : secrets.h, local_server_overrides.h, warmup pioarduino si besoin (voir docs/technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md).
# Usage : .\build_upload_monitor_wroom_beta_local.ps1
#         .\build_upload_monitor_wroom_beta_local.ps1 -Port COM5
#         .\build_upload_monitor_wroom_beta_local.ps1 -Port COM5 -SkipBuild

param(
    [string]$Port = "COM4",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $SkipBuild) {
    Write-Host "[1/3] Build wroom-beta-local..."
    $env:PYTHONUTF8 = "1"
    pio run -e wroom-beta-local
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build echoue. Essayer warmup: cd ..\n3pp; pio run -e esp32dev" -ForegroundColor Yellow
        exit $LASTEXITCODE
    }
} else {
    Write-Host "[1/3] Build ignore (-SkipBuild)."
}

Write-Host "[2/3] Upload sur $Port..."
. .\scripts\Release-ComPort.ps1
Release-ComPortIfNeeded -Port $Port
pio run -e wroom-beta-local -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload echoue (souvent CP210x sans auto-boot). Voir docs/technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md section 3.3" -ForegroundColor Yellow
    exit $LASTEXITCODE
}

Write-Host "[3/3] Moniteur serie ($Port, Ctrl+C pour quitter)..."
pio device monitor -e wroom-beta-local --port $Port
