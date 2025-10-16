# Script de monitoring 30 minutes pour ESP32-WROOM v11.49
# Conformément aux règles du projet FFP5CS

Write-Host "🔍 Monitoring ESP32-WROOM v11.49 - 30 minutes" -ForegroundColor Green
Write-Host "📅 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

$logFile = "monitor_v11.49_wroom_test_30min_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$errorFile = "monitor_v11.49_wroom_test_30min_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log.err"

Write-Host "📝 Log file: $logFile" -ForegroundColor Yellow
Write-Host "⏱️  Durée: 30 minutes (1800 secondes)" -ForegroundColor Yellow
Write-Host "🎯 Focus: Stabilité système, watchdog, mémoire, WiFi/WebSocket" -ForegroundColor Yellow
Write-Host "🔍 Priorités: Guru Meditation, Panic, Brownout, Core dump" -ForegroundColor Red

try {
    # Démarrer le monitoring avec timeout de 30 minutes
    $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--environment", "wroom-test", "--baud", "115200", "--filter", "esp32_exception_decoder" -RedirectStandardOutput $logFile -RedirectStandardError $errorFile -PassThru -NoNewWindow
    
    Write-Host "🚀 Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
    
    # Attendre 30 minutes
    $timeout = 1800  # 30 minutes
    $elapsed = 0
    $lastUpdate = 0
    
    while ($elapsed -lt $timeout -and !$process.HasExited) {
        Start-Sleep -Seconds 30
        $elapsed += 30
        $remaining = $timeout - $elapsed
        $minutesElapsed = [math]::Floor($elapsed / 60)
        $minutesRemaining = [math]::Floor($remaining / 60)
        
        Write-Host "⏳ Temps écoulé: $minutesElapsed min | Restant: $minutesRemaining min" -ForegroundColor Cyan
        
        # Afficher les dernières lignes du log toutes les 5 minutes
        if ($elapsed - $lastUpdate -ge 300) {
            $lastUpdate = $elapsed
            if (Test-Path $logFile) {
                Write-Host "`n📊 Dernières lignes du log (minute $minutesElapsed):" -ForegroundColor Cyan
                Get-Content $logFile -Tail 10 | ForEach-Object { Write-Host $_ -ForegroundColor White }
            }
        }
    }
    
    # Arrêter le processus si il n'est pas terminé
    if (!$process.HasExited) {
        Write-Host "🛑 Arrêt du monitoring après 30 minutes" -ForegroundColor Yellow
        Stop-Process -Id $process.Id -Force
    }
    
    Write-Host "✅ Monitoring terminé" -ForegroundColor Green
    
    # Afficher les dernières lignes du log
    if (Test-Path $logFile) {
        Write-Host "`n📊 Dernières lignes du log:" -ForegroundColor Cyan
        Get-Content $logFile -Tail 20 | ForEach-Object { Write-Host $_ -ForegroundColor White }
    }
    
    if (Test-Path $errorFile) {
        Write-Host "`n⚠️  Erreurs capturées:" -ForegroundColor Red
        Get-Content $errorFile | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    }
    
    # Statistiques du fichier de log
    if (Test-Path $logFile) {
        $lineCount = (Get-Content $logFile).Count
        $fileSize = (Get-Item $logFile).Length
        Write-Host "`n📈 Statistiques du log:" -ForegroundColor Green
        Write-Host "   Lignes: $lineCount" -ForegroundColor White
        Write-Host "   Taille: $([math]::Round($fileSize/1KB, 2)) KB" -ForegroundColor White
    }
    
} catch {
    Write-Host "❌ Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n🎯 Prochaines étapes:" -ForegroundColor Green
Write-Host "1. Analyser le log pour identifier les points d'attention" -ForegroundColor White
Write-Host "2. Focus sur: Guru Meditation, Panic, Brownout, Core dump" -ForegroundColor White
Write-Host "3. Vérifier: Warnings watchdog, timeouts WiFi/WebSocket" -ForegroundColor White
Write-Host "4. Monitorer: Utilisation mémoire (heap/stack)" -ForegroundColor White
Write-Host "5. Générer un rapport détaillé avec recommandations" -ForegroundColor White
