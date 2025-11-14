# Script de monitoring post-flash v11.118
# Monitoring de 3 minutes pour vérifier la stabilité après simplifications Phase 1

Write-Host "=== MONITORING POST-FLASH V11.118 (SIMPLIFICATIONS) ===" -ForegroundColor Green
Write-Host "Démarrage du monitoring de 3 minutes pour vérifier la stabilité..." -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$logFile = "monitor_90s_v11.118_simplification_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "🔍 Points d'attention pour cette version:" -ForegroundColor Yellow
Write-Host "   - Simplification JsonPool (fonctionnalité préservée)" -ForegroundColor White
Write-Host "   - Simplification SensorCache (fonctionnalité préservée)" -ForegroundColor White  
Write-Host "   - Simplification NetworkOptimizer (fonctionnalité préservée)" -ForegroundColor White
Write-Host "   - Suppression OptimizedLogger (redondant)" -ForegroundColor White
Write-Host "   - Vérifier que les endpoints web fonctionnent" -ForegroundColor White
Write-Host "   - Vérifier que le cache capteurs fonctionne" -ForegroundColor White
Write-Host ""

Write-Host "📋 Démarrage monitoring sur COM6 (3 minutes)..." -ForegroundColor Cyan
Write-Host "Log sauvegardé dans: $logFile" -ForegroundColor Gray
Write-Host ""

try {
    # Lancement du monitoring avec redirection vers fichier ET console
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("device", "monitor", "--port", "COM6", "--baud", "115200") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    # Timer de 3 minutes (180 secondes)
    $endTime = (Get-Date).AddSeconds(180)
    $progressBar = 0
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = 180 - $remaining
        
        # Barre de progression simple
        if ($progress -gt $progressBar) {
            Write-Host "." -NoNewline -ForegroundColor Green
            $progressBar = $progress
        }
        
        Start-Sleep -Milliseconds 500
        
        # Vérifier que le processus est encore en vie
        if ($monitorProcess.HasExited) {
            Write-Host "`n⚠️ Le processus de monitoring s'est arrêté prématurément" -ForegroundColor Red
            break
        }
    }
    
    # Arrêter le monitoring
    if (-not $monitorProcess.HasExited) {
        Write-Host "`n🛑 Arrêt du monitoring..." -ForegroundColor Yellow
        $monitorProcess.Kill()
        Start-Sleep -Seconds 2
    }
    
    Write-Host "`n✅ Monitoring terminé !" -ForegroundColor Green
    Write-Host ""
    
    # Analyse rapide du log
    if (Test-Path $logFile) {
        Write-Host "📊 Analyse rapide du log:" -ForegroundColor Cyan
        $logContent = Get-Content $logFile -Raw
        
        # Recherche des patterns critiques
        $criticalPatterns = @(
            @{ Pattern = "Guru Meditation"; Color = "Red"; Type = "CRITIQUE" },
            @{ Pattern = "Panic"; Color = "Red"; Type = "CRITIQUE" },
            @{ Pattern = "Brownout"; Color = "Red"; Type = "CRITIQUE" },
            @{ Pattern = "Core dump"; Color = "Red"; Type = "CRITIQUE" },
            @{ Pattern = "Watchdog.*timeout"; Color = "Red"; Type = "CRITIQUE" },
            @{ Pattern = "Démarrage FFP5CS v11"; Color = "Green"; Type = "SUCCESS" },
            @{ Pattern = "Init done"; Color = "Green"; Type = "SUCCESS" },
            @{ Pattern = "WiFi.*connecté|WiFi.*connected"; Color = "Green"; Type = "SUCCESS" },
            @{ Pattern = "/api/status|/api/sensors"; Color = "Green"; Type = "INFO" },
            @{ Pattern = "jsonPool|JsonPool"; Color = "Yellow"; Type = "INFO" },
            @{ Pattern = "sensorCache|SensorCache"; Color = "Yellow"; Type = "INFO" },
            @{ Pattern = "NetworkOptimizer"; Color = "Yellow"; Type = "INFO" }
        )
        
        foreach ($check in $criticalPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $color = switch ($check.Color) {
                    "Red" { "Red" }
                    "Green" { "Green" }
                    default { "Yellow" }
                }
                Write-Host "  $($check.Type): $($check.Pattern) ($($matches.Count) occurrence(s))" -ForegroundColor $color
            }
        }
        
        Write-Host ""
        Write-Host "📁 Log complet disponible: $logFile" -ForegroundColor Gray
        
        # Statistiques basiques
        $lineCount = (Get-Content $logFile).Count
        Write-Host "📈 Total lignes loggées: $lineCount" -ForegroundColor Gray
        
    } else {
        Write-Host "⚠️ Aucun fichier de log généré" -ForegroundColor Yellow
    }
    
} catch {
    Write-Host "❌ Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Vérifiez les logs pour confirmer que les simplifications n'ont pas causé de régression." -ForegroundColor Cyan

