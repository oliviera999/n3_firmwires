# =============================================================================
# Publication OTA ffp5cs — delegue au script racine IOT_n3
# =============================================================================
# Les binaires et metadata sont publies dans serveur/ota/ (depot n3_serveur).
# URLs publiques : https://iot.olution.info/ota/... (OTA_BASE_PATH = /ota/)
#
# Executer depuis ffp5cs ou n'importe ou : ce script se place a la racine IOT_n3.
#
# Usage :
#   .\scripts\publish_ota.ps1
#   .\scripts\publish_ota.ps1 -Build
#   .\scripts\publish_ota.ps1 -SkipCommit
#   .\scripts\publish_ota.ps1 -Targets "ffp5-wroom-prod"   # via racine uniquement
# =============================================================================

param(
    [switch]$SkipCommit,
    [switch]$DryRun,
    [switch]$Build,
    [switch]$SkipValidate
)

$ErrorActionPreference = "Stop"
$ffp5Targets = @("ffp5-wroom-prod", "ffp5-wroom-beta", "ffp5-s3-prod", "ffp5-s3-test")
$iotRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if (-not (Test-Path (Join-Path $iotRoot "scripts\publish_ota.ps1"))) {
    Write-Host "Erreur : racine IOT_n3 introuvable depuis ce script (attendu : ...\IOT_n3\scripts\publish_ota.ps1)." -ForegroundColor Red
    exit 1
}
Set-Location $iotRoot
$invoke = @{
    Targets = $ffp5Targets
}
if ($SkipCommit) { $invoke.SkipCommit = $true }
if ($DryRun) { $invoke.DryRun = $true }
if ($Build) { $invoke.Build = $true }
if ($SkipValidate) { $invoke.SkipValidate = $true }
& (Join-Path $iotRoot "scripts\publish_ota.ps1") @invoke
