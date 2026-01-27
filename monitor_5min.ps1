# Script de monitoring 5 minutes
$logFile = "monitor_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
Write-Host "=== MONITORING ESP32 - 5 MINUTES ===" -ForegroundColor Green
Write-Host "Durée: 5 minutes (300 secondes)" -ForegroundColor Cyan
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

# Démarrer le monitoring
$monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", "wroom-test") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"

$endTime = (Get-Date).AddSeconds(300)
Write-Host "Monitoring démarré..." -ForegroundColor Green
Write-Host "Appuyez sur Ctrl+C pour arrêter prématurément" -ForegroundColor Gray
Write-Host ""

$lastUpdate = 0
while ((Get-Date) -lt $endTime) {
    $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
    $progress = 300 - $remaining
    $progressPercent = [math]::Round(($progress / 300) * 100)
    $elapsedMinutes = [math]::Floor($progress / 60)
    $elapsedSeconds = $progress % 60
    $remainingMinutes = [math]::Floor($remaining / 60)
    $remainingSeconds = $remaining % 60
    
    # Afficher progression toutes les 30 secondes
    if ($progress -ge $lastUpdate + 30) {
        $timeElapsed = "$($elapsedMinutes.ToString('00')):$($elapsedSeconds.ToString('00'))"
        $timeRemaining = "$($remainingMinutes.ToString('00')):$($remainingSeconds.ToString('00'))"
        Write-Host "[$timeElapsed] Progression: $progressPercent% ($timeRemaining restant)" -ForegroundColor Cyan
        $lastUpdate = $progress
    }
    
    Start-Sleep -Seconds 1
    
    if ($monitorProcess.HasExited) {
        Write-Host ""
        Write-Host "Le processus de monitoring s'est arrêté prématurément" -ForegroundColor Red
        break
    }
}

if (-not $monitorProcess.HasExited) {
    Write-Host ""
    Write-Host "Arrêt du monitoring..." -ForegroundColor Yellow
    $monitorProcess.Kill()
    Start-Sleep -Seconds 2
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Log sauvegardé: $logFile" -ForegroundColor Cyan
if (Test-Path "$logFile.errors") {
    Write-Host "Erreurs: $logFile.errors" -ForegroundColor Yellow
}
