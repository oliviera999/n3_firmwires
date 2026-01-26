# Script de monitoring 15 minutes pour validation fragmentation mémoire
# Usage: .\monitor_15min_validation.ps1

$duration = 15 * 60  # 15 minutes en secondes
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_wroom_test_validation_$timestamp.log"

Write-Host "Démarrage monitoring de $($duration/60) minutes..."
Write-Host "Log: $logFile"
Write-Host "Appuyez sur Ctrl+C pour arrêter avant la fin"

# Lancer pio device monitor et rediriger vers fichier
$process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "-e", "wroom-test" -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"

# Attendre la durée spécifiée
Start-Sleep -Seconds $duration

# Arrêter le processus
Stop-Process -Id $process.Id -Force

Write-Host "Monitoring terminé. Log sauvegardé: $logFile"
