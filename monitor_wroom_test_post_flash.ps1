# Script de monitoring post-flash wroom-test
# Monitoring de 3 minutes pour vérifier la stabilité

Write-Host "=== MONITORING POST-FLASH WROOM-TEST ===" -ForegroundColor Green
Write-Host "Durée: 3 minutes (180 secondes)" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$logFile = "monitor_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
Write-Host "📋 Log sauvegardé dans: $logFile" -ForegroundColor Cyan
Write-Host ""
Write-Host "🔍 Points de vérification:" -ForegroundColor Yellow
Write-Host "   - Démarrage du système" -ForegroundColor White
Write-Host "   - Connexion WiFi" -ForegroundColor White
Write-Host "   - Initialisation des capteurs" -ForegroundColor White
Write-Host "   - Serveur web opérationnel" -ForegroundColor White
Write-Host "   - Absence d'erreurs critiques" -ForegroundColor White
Write-Host ""

Write-Host "📡 Démarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    # Lancement du monitoring avec redirection vers fichier ET console
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "-e", "wroom-test", "--target", "monitor") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    # Timer de 3 minutes (180 secondes)
    $endTime = (Get-Date).AddSeconds(180)
    $lastProgressUpdate = 0
    
    Write-Host "Monitoring en cours... (appuyez sur Ctrl+C pour arrêter prématurément)" -ForegroundColor Gray
    Write-Host ""
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = 180 - $remaining
        $progressPercent = [math]::Round(($progress / 180) * 100)
        $elapsedMinutes = [math]::Floor($progress / 60)
        $elapsedSeconds = $progress % 60
        
        # Mise à jour toutes les 30 secondes
        if ($progress -ge $lastProgressUpdate + 30) {
            $timeStr = "$elapsedMinutes" + ":" + $elapsedSeconds.ToString('00')
            Write-Host "[$timeStr] Progression: $progressPercent% ($remaining s restantes)" -ForegroundColor Cyan
            $lastProgressUpdate = $progress
        }
        
        Start-Sleep -Seconds 1
        
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
        $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
        
        if ($logContent) {
            # Recherche des patterns critiques
            $criticalPatterns = @(
                @{ Pattern = "Guru Meditation"; Color = "Red"; Type = "❌ CRITIQUE" },
                @{ Pattern = "Panic(?!.*diag_hasPanic)"; Color = "Red"; Type = "❌ CRITIQUE" },
                @{ Pattern = "Brownout"; Color = "Red"; Type = "❌ CRITIQUE" },
                @{ Pattern = "Core dump"; Color = "Red"; Type = "❌ CRITIQUE" },
                @{ Pattern = "Watchdog.*timeout"; Color = "Yellow"; Type = "⚠️ WARNING" },
                @{ Pattern = "Démarrage FFP5CS v11|App start v11|BOOT FFP5CS v11"; Color = "Green"; Type = "✅ SUCCESS" },
                @{ Pattern = "Init done|Initialization complete"; Color = "Green"; Type = "✅ SUCCESS" },
                @{ Pattern = "WiFi.*connecté|WiFi.*connected|Connected to.*SSID"; Color = "Green"; Type = "✅ SUCCESS" },
                @{ Pattern = "/api/status|/api/sensors|HTTP.*200"; Color = "Cyan"; Type = "ℹ️ INFO" }
            )
            
            $foundIssues = $false
            foreach ($check in $criticalPatterns) {
                $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
                if ($matches.Count -gt 0) {
                    Write-Host "  $($check.Type): $($check.Pattern) ($($matches.Count) occurrence(s))" -ForegroundColor $check.Color
                    $foundIssues = $true
                }
            }
            
            if (-not $foundIssues) {
                Write-Host "  ✅ Aucun problème critique détecté dans le log" -ForegroundColor Green
            }
            
            Write-Host ""
            
            # Statistiques basiques
            $lines = Get-Content $logFile -ErrorAction SilentlyContinue
            $lineCount = if ($lines) { $lines.Count } else { 0 }
            $fileSize = if (Test-Path $logFile) { (Get-Item $logFile).Length / 1KB } else { 0 }
            
            Write-Host "📈 Statistiques:" -ForegroundColor Cyan
            Write-Host "  - Total lignes: $lineCount" -ForegroundColor Gray
            Write-Host "  - Taille log: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
        } else {
            Write-Host "  ⚠️ Le fichier de log est vide" -ForegroundColor Yellow
        }
        
        Write-Host ""
        Write-Host "📁 Log complet disponible: $logFile" -ForegroundColor Gray
        
    } else {
        Write-Host "⚠️ Aucun fichier de log généré" -ForegroundColor Yellow
    }
    
} catch {
    Write-Host "❌ Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Vérifiez les logs pour confirmer le bon fonctionnement du système." -ForegroundColor Cyan
