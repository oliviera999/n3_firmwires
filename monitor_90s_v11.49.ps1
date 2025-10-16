# Script de monitoring 90 secondes pour wroom-test v11.49
# Conformément aux règles du projet FFP5CS

Write-Host "🔍 Monitoring ESP32-WROOM v11.49 - 90 secondes" -ForegroundColor Green
Write-Host "📅 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

$logFile = "monitor_v11.49_wroom_test_90s_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$errorFile = "monitor_v11.49_wroom_test_90s_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log.err"

Write-Host "📝 Log file: $logFile" -ForegroundColor Yellow
Write-Host "⏱️  Durée: 90 secondes" -ForegroundColor Yellow
Write-Host "🎯 Focus: Stabilité système, watchdog, mémoire, WiFi/WebSocket" -ForegroundColor Yellow

try {
    # Démarrer le monitoring avec timeout de 90 secondes
    $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--environment", "wroom-test", "--baud", "115200", "--filter", "esp32_exception_decoder" -RedirectStandardOutput $logFile -RedirectStandardError $errorFile -PassThru -NoNewWindow
    
    Write-Host "🚀 Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
    
    # Attendre 90 secondes
    $timeout = 90
    $elapsed = 0
    
    while ($elapsed -lt $timeout -and !$process.HasExited) {
        Start-Sleep -Seconds 5
        $elapsed += 5
        $remaining = $timeout - $elapsed
        Write-Host "⏳ Temps restant: $remaining secondes" -ForegroundColor Cyan
    }
    
    # Arrêter le processus si il n'est pas terminé
    if (!$process.HasExited) {
        Write-Host "🛑 Arrêt du monitoring après 90 secondes" -ForegroundColor Yellow
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
    
} catch {
    Write-Host "❌ Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n🎯 Prochaines étapes:" -ForegroundColor Green
Write-Host "1. Analyser le log pour identifier les points d'attention" -ForegroundColor White
Write-Host "2. Focus sur: Guru Meditation, Panic, Brownout, Core dump" -ForegroundColor White
Write-Host "3. Vérifier: Warnings watchdog, timeouts WiFi/WebSocket" -ForegroundColor White
Write-Host "4. Monitorer: Utilisation memoire (heap/stack)" -ForegroundColor White
