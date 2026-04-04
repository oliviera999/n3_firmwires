# Build wroom-beta-local, upload sur COM4, puis moniteur série.
# Prérequis : si erreur "fichier utilisé par un autre processus", fermer Cursor/VSCode,
# supprimer %USERPROFILE%\.platformio\packages\framework-arduinoespressif32, puis relancer.
# Usage : .\build_upload_monitor_wroom_beta_local_com4.ps1

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "[1/3] Build wroom-beta-local..."
pio run -e wroom-beta-local
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[2/3] Upload sur COM4..."
pio run -e wroom-beta-local -t upload --upload-port COM4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[3/3] Moniteur serie (COM4, Ctrl+C pour quitter)..."
pio device monitor -e wroom-beta-local --port COM4
