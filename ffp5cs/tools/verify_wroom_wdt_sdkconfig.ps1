# Compatibilite : delegue vers verify_wroom_sdkconfig.ps1 (TWDT + SPIRAM + taille firmware).
param(
    [string]$Environment = "wroom-test"
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $here "verify_wroom_sdkconfig.ps1") -Environment $Environment
