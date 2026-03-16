# Script de flash wroom-s3-test (ESP32-S3)
param(
    [string]$Port = ""
)

$configPath = Join-Path $PSScriptRoot "include\config.h"
$fwVersion = "?"
if (Test-Path $configPath) {
    $cfg = Get-Content $configPath -Raw
    if ($cfg -match 'VERSION\s*=\s*"(\d+\.\d+)"') { $fwVersion = "v$($matches[1])" }
}

Write-Host "=== FLASH WROOM-S3-TEST $fwVersion ===" -ForegroundColor Green
Write-Host "Environnement: wroom-s3-test (ESP32-S3)" -ForegroundColor Yellow
Write-Host ""

# Port COM : paramètre, détection pio, ou défaut
$portToUse = $Port
if (-not $portToUse) {
    try {
        $pioDevices = pio device list 2>&1 | Out-String
        if ($pioDevices -match "(COM\d+)") {
            $portToUse = $Matches[1]
            Write-Host "Port detecte: $portToUse" -ForegroundColor Gray
        }
    } catch { }
    if (-not $portToUse) { $portToUse = "COM7" }
}

. (Join-Path $PSScriptRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $portToUse
if ($portToUse) { $env:PLATFORMIO_UPLOAD_PORT = $portToUse }

# Compiler d'abord pour vérifier qu'il n'y a pas d'erreurs
Write-Host "3. Compilation du firmware wroom-s3-test..." -ForegroundColor Cyan
$buildJobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
$compileResult = pio run -e wroom-s3-test @buildJobs
if ($LASTEXITCODE -ne 0) {
    Write-Host "Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Compilation reussie" -ForegroundColor Green

# Flash du firmware
Write-Host "4. Flash du firmware wroom-s3-test..." -ForegroundColor Cyan
$uploadResult = pio run -e wroom-s3-test --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "Flash firmware reussi !" -ForegroundColor Green
} else {
    Write-Host "Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    Start-Sleep -Seconds 10
    $retryResult = pio run -e wroom-s3-test --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Flash firmware reussi au 2eme essai !" -ForegroundColor Green
    } else {
        Write-Host "Echec definitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du système de fichiers
Write-Host "5. Flash du système de fichiers LittleFS..." -ForegroundColor Cyan
Start-Sleep -Seconds 3
$uploadfsResult = pio run -e wroom-s3-test --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "Flash LittleFS reussi !" -ForegroundColor Green
} else {
    Write-Host "Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    Start-Sleep -Seconds 10
    $retryfsResult = pio run -e wroom-s3-test --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Flash LittleFS reussi au 2eme essai !" -ForegroundColor Green
    } else {
        Write-Host "Echec definitif du flash LittleFS" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "=== FLASH COMPLET REUSSI ===" -ForegroundColor Green
Write-Host "Version $fwVersion flashee avec succes sur wroom-s3-test !" -ForegroundColor Green
Write-Host ""
Write-Host "Recommandation: Executer le monitoring pour verifier la stabilite (ex. .\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port $portToUse)" -ForegroundColor Cyan
