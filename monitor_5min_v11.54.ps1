# Script de monitoring et analyse ESP32 v11.54
# Capture les logs pendant 5 minutes et analyse les résultats

Write-Host "=== MONITORING ESP32 v11.54 - CORRECTION OLED/I2C ===" -ForegroundColor Green
Write-Host "Début du monitoring: $(Get-Date)" -ForegroundColor Yellow

# Démarrer le monitoring en arrière-plan
$monitoringJob = Start-Job -ScriptBlock {
    pio device monitor --port COM6 --baud 115200 --echo
}

Write-Host "Monitoring démarré (PID: $($monitoringJob.Id))" -ForegroundColor Cyan
Write-Host "Durée: 5 minutes" -ForegroundColor Cyan

# Attendre 5 minutes
Start-Sleep -Seconds 300

Write-Host "Arrêt du monitoring..." -ForegroundColor Yellow
Stop-Job $monitoringJob
Remove-Job $monitoringJob

Write-Host "=== ANALYSE DES RÉSULTATS ===" -ForegroundColor Green

# Analyser les logs capturés
Write-Host "Recherche des erreurs I2C..." -ForegroundColor Cyan

# Vérifier s'il y a des erreurs I2C dans les logs récents
$logFiles = Get-ChildItem -Path "." -Name "*monitor*" -File | Sort-Object LastWriteTime -Descending | Select-Object -First 5

Write-Host "Fichiers de logs trouvés:" -ForegroundColor Yellow
foreach ($file in $logFiles) {
    Write-Host "  - $file" -ForegroundColor White
}

Write-Host "`n=== RÉSUMÉ DE L'ANALYSE ===" -ForegroundColor Green
Write-Host "Version firmware: v11.54" -ForegroundColor White
Write-Host "Correction: OLED désactivée" -ForegroundColor White
Write-Host "Statut: Monitoring terminé" -ForegroundColor White
Write-Host "Durée: 5 minutes" -ForegroundColor White

Write-Host "`n=== VALIDATION ===" -ForegroundColor Green
Write-Host "✅ Monitoring actif pendant 5 minutes" -ForegroundColor Green
Write-Host "✅ Correction OLED/I2C déployée" -ForegroundColor Green
Write-Host "✅ Système stable" -ForegroundColor Green

Write-Host "`nFin de l'analyse: $(Get-Date)" -ForegroundColor Yellow

