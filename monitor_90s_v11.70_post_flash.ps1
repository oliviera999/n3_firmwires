# Script de monitoring 90 secondes post-flash v11.70
# Capture des logs série pour analyse de stabilité

$Port = "COM6"
$BaudRate = 115200
$Duration = 90  # secondes
$LogFile = "monitor_90s_v11.70_post_flash_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "=== MONITORING POST-FLASH v11.70 - 90 SECONDES ===" -ForegroundColor Green
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Baud: $BaudRate" -ForegroundColor Yellow
Write-Host "Durée: $Duration secondes" -ForegroundColor Yellow
Write-Host "Log: $LogFile" -ForegroundColor Yellow
Write-Host "Démarrage: $(Get-Date)" -ForegroundColor Cyan
Write-Host ""

# Créer le fichier de log
"=== MONITORING POST-FLASH v11.70 - $(Get-Date) ===" | Out-File -FilePath $LogFile -Encoding UTF8
"Port: $Port, Baud: $BaudRate, Durée: $Duration secondes" | Out-File -FilePath $LogFile -Append -Encoding UTF8
"" | Out-File -FilePath $LogFile -Append -Encoding UTF8

# Fonction pour capturer les logs
function Start-LogCapture {
    param($Port, $BaudRate, $LogFile, $Duration)
    
    try {
        # Utiliser pio device monitor pour capturer les logs
        $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $Port, "--baud", $BaudRate, "--filter", "time" -RedirectStandardOutput $LogFile -RedirectStandardError "error_$LogFile" -PassThru -NoNewWindow
        
        Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
        
        # Attendre la durée spécifiée
        Start-Sleep -Seconds $Duration
        
        # Arrêter le processus
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        
        Write-Host "Monitoring terminé après $Duration secondes" -ForegroundColor Green
        
    } catch {
        Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Démarrer la capture
Start-LogCapture -Port $Port -BaudRate $BaudRate -LogFile $LogFile -Duration $Duration

Write-Host ""
Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Green
Write-Host "Fichier de log: $LogFile" -ForegroundColor Yellow

# Analyser les logs capturés
if (Test-Path $LogFile) {
    $logs = Get-Content $LogFile -Raw
    
    Write-Host ""
    Write-Host "=== RÉSUMÉ DE L'ANALYSE ===" -ForegroundColor Cyan
    
    # Compter les erreurs critiques
    $guruCount = ($logs | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" -AllMatches).Matches.Count
    $watchdogCount = ($logs | Select-String -Pattern "watchdog|WDT" -AllMatches).Matches.Count
    $wifiCount = ($logs | Select-String -Pattern "WiFi|WIFI" -AllMatches).Matches.Count
    $websocketCount = ($logs | Select-String -Pattern "WebSocket|websocket" -AllMatches).Matches.Count
    $memoryCount = ($logs | Select-String -Pattern "heap|memory|free" -AllMatches).Matches.Count
    
    Write-Host "ERREURS CRITIQUES (Guru/Panic/Brownout): $guruCount" -ForegroundColor $(if($guruCount -gt 0) {"Red"} else {"Green"})
    Write-Host "WARNINGS WATCHDOG: $watchdogCount" -ForegroundColor $(if($watchdogCount -gt 0) {"Yellow"} else {"Green"})
    Write-Host "REFERENCES WIFI: $wifiCount" -ForegroundColor Cyan
    Write-Host "REFERENCES WEBSOCKET: $websocketCount" -ForegroundColor Cyan
    Write-Host "REFERENCES MEMOIRE: $memoryCount" -ForegroundColor Cyan
    
    # Afficher les dernières lignes importantes
    Write-Host ""
    Write-Host "=== DERNIÈRES LIGNES IMPORTANTES ===" -ForegroundColor Cyan
    $logs | Select-String -Pattern "Guru|Panic|Brownout|Core|watchdog|WiFi|WebSocket|heap|free|ERROR|WARN" | Select-Object -Last 10 | ForEach-Object {
        Write-Host $_.Line -ForegroundColor White
    }
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $LogFile" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Cyan
