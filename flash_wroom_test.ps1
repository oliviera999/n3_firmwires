# Script de flash wroom-test
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

# Arrêter tout processus qui pourrait bloquer le port COM
Write-Host "1. Vérification des processus bloquant COM..." -ForegroundColor Cyan
try {
    $comProcesses = Get-Process | Where-Object { $_.ProcessName -like "*python*" -or 
                                               $_.ProcessName -like "*pio*" -or
                                               $_.MainWindowTitle -like "*monitor*" -or 
                                               $_.MainWindowTitle -like "*serial*" }
    if ($comProcesses) {
        Write-Host "Processus détectés: $($comProcesses.Count)" -ForegroundColor Yellow
        foreach ($proc in $comProcesses) {
            Write-Host "  - $($proc.ProcessName) (ID: $($proc.Id))" -ForegroundColor Yellow
        }
    } else {
        Write-Host "Aucun processus bloquant détecté" -ForegroundColor Green
    }
} catch {
    Write-Host "Erreur lors de la vérification des processus: $($_.Exception.Message)" -ForegroundColor Red
}

# Attendre un peu pour laisser le temps aux processus de se libérer
Write-Host "2. Attente de libération du port (5 secondes)..." -ForegroundColor Cyan
Start-Sleep -Seconds 5

# Compiler d'abord pour vérifier qu'il n'y a pas d'erreurs
Write-Host "3. Compilation du firmware wroom-test..." -ForegroundColor Cyan
$compileResult = pio run -e wroom-test
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
