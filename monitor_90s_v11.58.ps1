# Script de monitoring automatique v11.58 - Correction OLED/I2C
# Capture les logs pendant 90 secondes pour vérifier la suppression des erreurs I2C

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_90s_v11.58_$timestamp.log"
$errorFile = "monitor_90s_v11.58_$timestamp.log.err"

Write-Host "=== MONITORING ESP32 v11.58 - CORRECTION OLED/I2C ===" -ForegroundColor Green
Write-Host "Démarrage du monitoring pour 90 secondes..." -ForegroundColor Yellow
Write-Host "Logs sauvegardés dans: $logFile" -ForegroundColor Cyan
Write-Host "Erreurs dans: $errorFile" -ForegroundColor Red
Write-Host ""

# Démarrer le monitoring avec redirection
$process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--baud", "115200", "--filter", "time" -RedirectStandardOutput $logFile -RedirectStandardError $errorFile -PassThru -NoNewWindow

# Attendre 90 secondes
Write-Host "Monitoring en cours... (90 secondes)" -ForegroundColor Yellow
Start-Sleep -Seconds 90

# Arrêter le processus
Write-Host "Arrêt du monitoring..." -ForegroundColor Yellow
Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Green

# Analyser les erreurs I2C
if (Test-Path $errorFile) {
    $errorContent = Get-Content $errorFile -Raw
    if ($errorContent) {
        Write-Host "❌ Erreurs détectées dans $errorFile" -ForegroundColor Red
        Write-Host $errorContent -ForegroundColor Red
    } else {
        Write-Host "✅ Aucune erreur détectée" -ForegroundColor Green
    }
}

# Analyser les logs pour les erreurs I2C
if (Test-Path $logFile) {
    $logContent = Get-Content $logFile -Raw
    
    # Compter les erreurs I2C spécifiques
    $i2cErrors = ($logContent | Select-String -Pattern "I2C.*failed|NACK detected|ESP_ERR_INVALID_STATE|i2c_master_transmit failed" -AllMatches).Matches.Count
    $oledErrors = ($logContent | Select-String -Pattern "OLED.*ERROR|OLED.*FAILED" -AllMatches).Matches.Count
    
    Write-Host ""
    Write-Host "=== RÉSULTATS DE L'ANALYSE ===" -ForegroundColor Cyan
    Write-Host "Erreurs I2C détectées: $i2cErrors" -ForegroundColor $(if ($i2cErrors -eq 0) { "Green" } else { "Red" })
    Write-Host "Erreurs OLED détectées: $oledErrors" -ForegroundColor $(if ($oledErrors -eq 0) { "Green" } else { "Red" })
    
    if ($i2cErrors -eq 0 -and $oledErrors -eq 0) {
        Write-Host ""
        Write-Host "🎉 SUCCÈS ! Aucune erreur I2C/OLED détectée" -ForegroundColor Green
        Write-Host "✅ La correction v11.58 a résolu le problème" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "⚠️  Des erreurs persistent encore" -ForegroundColor Yellow
        Write-Host "📋 Vérifiez les logs détaillés dans $logFile" -ForegroundColor Yellow
    }
    
    # Afficher les dernières lignes des logs
    Write-Host ""
    Write-Host "=== DERNIÈRES LIGNES DES LOGS ===" -ForegroundColor Cyan
    $lastLines = Get-Content $logFile | Select-Object -Last 10
    foreach ($line in $lastLines) {
        Write-Host $line
    }
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fichiers générés:" -ForegroundColor Yellow
Write-Host "  - Logs: $logFile" -ForegroundColor Cyan
Write-Host "  - Erreurs: $errorFile" -ForegroundColor Red
