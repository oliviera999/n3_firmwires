# Script de monitoring 90 secondes sans limitations v11.70
# Utilise le nouveau script Python pour éviter les arrêts du moniteur

$Port = "COM6"
$BaudRate = 115200
$Duration = 90  # secondes
$LogFile = "monitor_unlimited_v11.70_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "=== MONITORING SANS LIMITES v11.70 - 90 SECONDES ===" -ForegroundColor Green
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Baud: $BaudRate" -ForegroundColor Yellow
Write-Host "Durée: $Duration secondes" -ForegroundColor Yellow
Write-Host "Log: $LogFile" -ForegroundColor Yellow
Write-Host "Démarrage: $(Get-Date)" -ForegroundColor Cyan
Write-Host ""

# Vérifier que Python est disponible
try {
    $pythonVersion = python --version 2>&1
    Write-Host "Python détecté: $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "❌ Python non trouvé. Veuillez installer Python et pyserial" -ForegroundColor Red
    Write-Host "Installation: pip install pyserial" -ForegroundColor Yellow
    exit 1
}

# Vérifier que pyserial est installé
try {
    python -c "import serial" 2>&1 | Out-Null
    Write-Host "✅ pyserial disponible" -ForegroundColor Green
} catch {
    Write-Host "❌ pyserial manquant. Installation: pip install pyserial" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== LANCEMENT DU MONITORING ===" -ForegroundColor Green

# Lancer le monitoring avec le script Python amélioré
try {
    $process = Start-Process -FilePath "python" -ArgumentList "tools/monitor/monitor_unlimited.py", "--port", $Port, "--baud", $BaudRate, "--duration", $Duration, "--output", $LogFile -PassThru -NoNewWindow
    
    Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
    Write-Host "Le monitoring va durer $Duration secondes..." -ForegroundColor Yellow
    Write-Host "Appuyez sur Ctrl+C pour arrêter prématurément" -ForegroundColor Cyan
    Write-Host ""
    
    # Attendre que le processus se termine
    $process.WaitForExit()
    
    Write-Host "Monitoring terminé (Code de sortie: $($process.ExitCode))" -ForegroundColor Green
    
} catch {
    Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Green

# Analyser les logs capturés
if (Test-Path $LogFile) {
    $logs = Get-Content $LogFile -Raw
    
    Write-Host "Fichier de log: $LogFile" -ForegroundColor Yellow
    Write-Host "Taille du fichier: $((Get-Item $LogFile).Length) bytes" -ForegroundColor Yellow
    Write-Host ""
    
    Write-Host "=== RÉSUMÉ DE L'ANALYSE ===" -ForegroundColor Cyan
    
    # Compter les erreurs critiques
    $guruCount = ($logs | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" -AllMatches).Matches.Count
    $watchdogCount = ($logs | Select-String -Pattern "watchdog|WDT" -AllMatches).Matches.Count
    $wifiCount = ($logs | Select-String -Pattern "WiFi|WIFI" -AllMatches).Matches.Count
    $websocketCount = ($logs | Select-String -Pattern "WebSocket|websocket" -AllMatches).Matches.Count
    $memoryCount = ($logs | Select-String -Pattern "heap|memory|free" -AllMatches).Matches.Count
    $errorCount = ($logs | Select-String -Pattern "ERROR|Error|error" -AllMatches).Matches.Count
    $warningCount = ($logs | Select-String -Pattern "WARN|Warning|warning" -AllMatches).Matches.Count
    
    Write-Host "ERREURS CRITIQUES (Guru/Panic/Brownout): $guruCount" -ForegroundColor $(if($guruCount -gt 0) {"Red"} else {"Green"})
    Write-Host "ERREURS GÉNÉRALES: $errorCount" -ForegroundColor $(if($errorCount -gt 0) {"Red"} else {"Green"})
    Write-Host "WARNINGS: $warningCount" -ForegroundColor $(if($warningCount -gt 0) {"Yellow"} else {"Green"})
    Write-Host "WARNINGS WATCHDOG: $watchdogCount" -ForegroundColor $(if($watchdogCount -gt 0) {"Yellow"} else {"Green"})
    Write-Host "RÉFÉRENCES WIFI: $wifiCount" -ForegroundColor Cyan
    Write-Host "RÉFÉRENCES WEBSOCKET: $websocketCount" -ForegroundColor Cyan
    Write-Host "RÉFÉRENCES MÉMOIRE: $memoryCount" -ForegroundColor Cyan
    
    # Compter le nombre total de lignes
    $totalLines = ($logs -split "`n").Count
    Write-Host "TOTAL LIGNES CAPTURÉES: $totalLines" -ForegroundColor Cyan
    
    # Afficher les dernières lignes importantes
    Write-Host ""
    Write-Host "=== DERNIÈRES LIGNES IMPORTANTES ===" -ForegroundColor Cyan
    $logs | Select-String -Pattern "Guru|Panic|Brownout|Core|watchdog|WiFi|WebSocket|heap|free|ERROR|WARN" | Select-Object -Last 10 | ForEach-Object {
        Write-Host $_.Line -ForegroundColor White
    }
    
    # Vérifier si le monitoring s'est bien déroulé
    Write-Host ""
    Write-Host "=== STATUT DU MONITORING ===" -ForegroundColor Cyan
    if ($totalLines -lt 50) {
        Write-Host "⚠️  Peu de lignes capturées ($totalLines). Vérifiez la connexion série." -ForegroundColor Yellow
    } elseif ($totalLines -gt 1000) {
        Write-Host "✅ Monitoring réussi - $totalLines lignes capturées" -ForegroundColor Green
    } else {
        Write-Host "✅ Monitoring normal - $totalLines lignes capturées" -ForegroundColor Green
    }
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $LogFile" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Cyan




