# Script de monitoring de 15 minutes pour ESP32 FFP5CS v11.70
# Après effacement complet et flash firmware + SPIFFS

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_15min_v11.70_$timestamp.log"
$port = "COM6"
$baudRate = 115200

Write-Host "=== MONITORING ESP32 FFP5CS v11.70 - 15 MINUTES ===" -ForegroundColor Green
Write-Host "Début: $(Get-Date)" -ForegroundColor Green
Write-Host "Port: $port" -ForegroundColor Green
Write-Host "Log file: $logFile" -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Green

# Fonction pour capturer les logs série
function Start-SerialMonitoring {
    param(
        [string]$Port,
        [int]$BaudRate,
        [string]$LogFile,
        [int]$DurationMinutes
    )
    
    $duration = $DurationMinutes * 60 * 1000  # Convertir en millisecondes
    $startTime = Get-Date
    
    Write-Host "Démarrage du monitoring série..." -ForegroundColor Yellow
    
    try {
        # Utiliser pio device monitor pour capturer les logs
        $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $Port, "--baud", $BaudRate -RedirectStandardOutput $LogFile -RedirectStandardError "error_$LogFile" -PassThru -NoNewWindow
        
        Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
        
        # Attendre la durée spécifiée
        Start-Sleep -Milliseconds $duration
        
        Write-Host "Arrêt du monitoring après $DurationMinutes minutes..." -ForegroundColor Yellow
        
        # Arrêter le processus
        if (!$process.HasExited) {
            $process.Kill()
            $process.WaitForExit(5000)
        }
        
        Write-Host "Monitoring terminé." -ForegroundColor Green
        
    } catch {
        Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Démarrer le monitoring
Start-SerialMonitoring -Port $port -BaudRate $baudRate -LogFile $logFile -DurationMinutes 15

Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Green
Write-Host "Log file: $logFile" -ForegroundColor Green

# Afficher les dernières lignes du log
if (Test-Path $logFile) {
    Write-Host "`n=== DERNIÈRES LIGNES DU LOG ===" -ForegroundColor Cyan
    Get-Content $logFile -Tail 20
}
