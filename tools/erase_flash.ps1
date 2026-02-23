# Efface la flash de l'ESP via PlatformIO (outils pio).
# Carte sur COM7 = ESP32-S3 -> utiliser -Environment wroom-s3-test.
#
# Usage:
#   .\tools\erase_flash.ps1 -Port COM7
#   .\tools\erase_flash.ps1 -Port COM7 -Environment wroom-s3-test
#   .\tools\erase_flash.ps1 -Port COM4 -Environment wroom-test

param(
    [string]$Port = "COM7",
    [string]$Environment = "wroom-s3-test"
)

$ErrorActionPreference = "Stop"
$projectRoot = if ($PSScriptRoot) {
    $d = Split-Path $PSScriptRoot -Parent
    if (Test-Path (Join-Path $d "platformio.ini")) { $d } else { Get-Location }
} else { Get-Location }

Set-Location $projectRoot
$env:UPLOAD_PORT = $Port
Write-Host "Erase flash: env=$Environment port=$Port" -ForegroundColor Cyan
pio run -e $Environment -t erase
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "OK" -ForegroundColor Green
