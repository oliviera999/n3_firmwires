# Script de monitoring 90 secondes pour validation correction I2C v11.57
# Date: 2024-12-19 17:30:00
# Objectif: Valider la correction des erreurs I2C massives

Write-Host "=== MONITORING 90 SECONDES - ESP32 v11.57 ===" -ForegroundColor Green
Write-Host "Objectif: Validation correction erreurs I2C massives" -ForegroundColor Yellow
Write-Host "Démarrage: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

# Configuration
$port = "COM6"
$baudRate = 115200
$duration = 90
$logFile = "monitor_90s_v11.57_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "Port: $port, Baud: $baudRate, Durée: ${duration}s" -ForegroundColor White
Write-Host "Fichier de log: $logFile" -ForegroundColor White

# Vérification du port
if (-not (Get-WmiObject -Class Win32_SerialPort | Where-Object { $_.DeviceID -eq $port })) {
    Write-Host "ERREUR: Port $port non trouvé!" -ForegroundColor Red
    exit 1
}

# Lancement du monitoring
Write-Host "`n=== DÉMARRAGE DU MONITORING ===" -ForegroundColor Green
Write-Host "Recherche des erreurs I2C dans les logs..." -ForegroundColor Yellow

try {
    # Utilisation de pio device monitor pour la capture
    $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $port, "--baud", $baudRate -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.err" -PassThru -NoNewWindow
    
    Write-Host "Processus de monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
    
    # Attendre la durée spécifiée
    Write-Host "Monitoring en cours pendant $duration secondes..." -ForegroundColor Yellow
    Start-Sleep -Seconds $duration
    
    # Arrêter le processus
    Stop-Process -Id $process.Id -Force
    Write-Host "Monitoring terminé." -ForegroundColor Green
    
    # Analyser les logs
    Write-Host "`n=== ANALYSE DES LOGS ===" -ForegroundColor Green
    
    if (Test-Path $logFile) {
        $content = Get-Content $logFile -Raw
        
        # Recherche des erreurs I2C
        $i2cErrors = ($content | Select-String -Pattern "I2C.*NACK|ESP_ERR_INVALID_STATE|i2c.*transaction.*failed" -AllMatches).Matches.Count
        $totalLines = ($content -split "`n").Count
        
        Write-Host "Lignes totales: $totalLines" -ForegroundColor White
        Write-Host "Erreurs I2C détectées: $i2cErrors" -ForegroundColor $(if ($i2cErrors -eq 0) { "Green" } else { "Red" })
        
        if ($i2cErrors -eq 0) {
            Write-Host "✅ SUCCÈS: Aucune erreur I2C détectée!" -ForegroundColor Green
            Write-Host "✅ La correction v11.57 fonctionne correctement" -ForegroundColor Green
        } else {
            Write-Host "❌ ÉCHEC: $i2cErrors erreurs I2C encore présentes" -ForegroundColor Red
            Write-Host "❌ La correction nécessite des ajustements" -ForegroundColor Red
            
            # Afficher quelques exemples d'erreurs
            Write-Host "`nExemples d'erreurs détectées:" -ForegroundColor Yellow
            $content | Select-String -Pattern "I2C.*NACK|ESP_ERR_INVALID_STATE" | Select-Object -First 5 | ForEach-Object {
                Write-Host "  $($_.Line)" -ForegroundColor Red
            }
        }
        
        # Recherche d'autres problèmes
        $panics = ($content | Select-String -Pattern "Guru Meditation|Panic|Brownout" -AllMatches).Matches.Count
        $watchdog = ($content | Select-String -Pattern "watchdog.*timeout" -AllMatches).Matches.Count
        
        Write-Host "`nAutres problèmes détectés:" -ForegroundColor Yellow
        Write-Host "  Panics/Crashes: $panics" -ForegroundColor $(if ($panics -eq 0) { "Green" } else { "Red" })
        Write-Host "  Watchdog timeouts: $watchdog" -ForegroundColor $(if ($watchdog -eq 0) { "Green" } else { "Red" })
        
    } else {
        Write-Host "ERREUR: Fichier de log non créé!" -ForegroundColor Red
    }
    
} catch {
    Write-Host "ERREUR lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n=== FIN DU MONITORING ===" -ForegroundColor Green
Write-Host "Fichiers générés:" -ForegroundColor Cyan
Write-Host "  - Log: $logFile" -ForegroundColor White
Write-Host "  - Erreurs: $logFile.err" -ForegroundColor White
Write-Host "Fin: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
