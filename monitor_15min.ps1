# Script de monitoring ESP32 - 15 minutes
# Capture la sortie série pendant 15 minutes

$duration = 900  # 15 minutes en secondes
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_wroom_test_$timestamp.log"
$errorFile = "$logFile.errors"

Write-Host "=== MONITORING ESP32 - 15 MINUTES ===" -ForegroundColor Cyan
Write-Host "Durée: 15 minutes (900 secondes)" -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

Write-Host "Monitoring démarré..." -ForegroundColor Green
Write-Host "Appuyez sur Ctrl+C pour arrêter prématurément" -ForegroundColor Yellow
Write-Host ""

# Démarrer le monitoring en arrière-plan
$job = Start-Job -ScriptBlock {
    param($logFile, $errorFile)
    cd "C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs"
    pio device monitor -e wroom-test --filter time 2>&1 | Tee-Object -FilePath $logFile
} -ArgumentList $logFile, $errorFile

# Afficher la progression
$startTime = Get-Date
$elapsed = 0
$lastProgress = 0

while ($elapsed -lt $duration) {
    Start-Sleep -Seconds 30
    $elapsed = ((Get-Date) - $startTime).TotalSeconds
    $progress = [math]::Round(($elapsed / $duration) * 100)
    $remaining = [math]::Round(($duration - $elapsed) / 60, 1)
    
    if ($progress -ne $lastProgress) {
        $minutes = [math]::Floor($elapsed / 60)
        $seconds = [math]::Round($elapsed % 60)
        Write-Host "[$($minutes.ToString('00')):$($seconds.ToString('00'))] Progression: $progress% ($remaining min restant)" -ForegroundColor Cyan
        $lastProgress = $progress
    }
}

# Arrêter le monitoring
Write-Host ""
Write-Host "Arrêt du monitoring..." -ForegroundColor Yellow
Stop-Job $job
Remove-Job $job

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Log sauvegardé: $logFile" -ForegroundColor Cyan
Write-Host "Erreurs: $errorFile" -ForegroundColor $(if (Test-Path $errorFile) { "Yellow" } else { "Green" })
