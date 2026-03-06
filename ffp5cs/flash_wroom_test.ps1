# Script de flash wroom-test
param(
    [string]$Port = ""
)

$configPath = Join-Path $PSScriptRoot "include\config.h"
$fwVersion = "?"
if (Test-Path $configPath) {
    $cfg = Get-Content $configPath -Raw
    if ($cfg -match 'VERSION\s*=\s*"(\d+\.\d+)"') { $fwVersion = "v$($matches[1])" }
}

Write-Host "=== FLASH WROOM-TEST $fwVersion ===" -ForegroundColor Green
Write-Host "Environnement: wroom-test" -ForegroundColor Yellow
Write-Host "Core dump: Outils d'extraction/analyse + corrections config" -ForegroundColor Cyan
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
    if (-not $portToUse) { $portToUse = "COM4" }
}

. (Join-Path $PSScriptRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $portToUse
if ($portToUse) { $env:PLATFORMIO_UPLOAD_PORT = $portToUse }

# Compiler d'abord pour vérifier qu'il n'y a pas d'erreurs
Write-Host "3. Compilation du firmware wroom-test..." -ForegroundColor Cyan
$buildJobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
$compileResult = pio run -e wroom-test @buildJobs
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "✅ Compilation réussie" -ForegroundColor Green

# Flash du firmware
Write-Host "4. Flash du firmware wroom-test..." -ForegroundColor Cyan
$uploadResult = pio run -e wroom-test --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash firmware réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryResult = pio run -e wroom-test --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash firmware réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du système de fichiers
Write-Host "5. Flash du système de fichiers LittleFS..." -ForegroundColor Cyan
Start-Sleep -Seconds 3  # Petit délai entre les flashes
$uploadfsResult = pio run -e wroom-test --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash LittleFS réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryfsResult = pio run -e wroom-test --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash LittleFS réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash LittleFS" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "=== FLASH COMPLET RÉUSSI ===" -ForegroundColor Green
Write-Host "Version $fwVersion flashée avec succès sur wroom-test !" -ForegroundColor Green
Write-Host ""
Write-Host "Recommandation: Exécuter le monitoring pour vérifier la stabilité" -ForegroundColor Cyan
