# Workflow complet pour wroom-s3-prod : erase + flash + LittleFS + monitor 2 min + analyse
# N'applique PAS les étapes spécifiques à wroom-s3-test (patches, fullclean, etc.).
# Usage:
#   .\run_wroom_s3_prod_workflow.ps1
#   .\run_wroom_s3_prod_workflow.ps1 -Port COM3
#
# Référence : .cursorrules / FFP5CS - utiliser erase_flash_fs_monitor_5min_analyze.ps1 pour le workflow complet.

param([string]$Port = "COM7")

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

$envName = "wroom-s3-prod"
$durationMinutes = 2

Write-Host "=== Workflow wroom-s3-prod (2 min) ===" -ForegroundColor Green
Write-Host "Environnement: $envName (aucune étape S3-test)" -ForegroundColor Cyan
Write-Host "Port: $Port | Durée: ${durationMinutes} min" -ForegroundColor Cyan
Write-Host ""

& "$projectRoot\erase_flash_fs_monitor_5min_analyze.ps1" -Environment $envName -Port $Port -DurationMinutes $durationMinutes
exit $LASTEXITCODE
