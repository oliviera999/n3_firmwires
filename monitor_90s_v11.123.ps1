# Script de monitoring post-flash v11.120
# Monitoring de 90 secondes pour vérifier la stabilité

Write-Host "=== MONITORING POST-FLASH V11.123 ===" -ForegroundColor Green
Write-Host "Démarrage du monitoring de 90 secondes pour vérifier la stabilité..." -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$logFile = "monitor_90s_v11.123_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "📋 Démarrage monitoring sur COM4 (90 secondes)..." -ForegroundColor Cyan
Write-Host "Log sauvegardé dans: $logFile" -ForegroundColor Gray
Write-Host ""

try {
    # Lancement du monitoring avec redirection vers fichier ET console
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("device", "monitor", "--port", "COM4", "--baud", "115200") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    # Timer de 90 secondes
    $endTime = (Get-Date).AddSeconds(90)
    $progressBar = 0
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = 90 - $remaining
        
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
        $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
        
        if ($logContent) {
            # Recherche des patterns critiques
            $criticalPatterns = @(
                @{ Pattern = "Guru Meditation"; Color = "Red"; Type = "CRITIQUE" },
                @{ Pattern = "Panic(?!.*diag_hasPanic)"; Color = "Red"; Type = "CRITIQUE" },
                @{ Pattern = "Brownout"; Color = "Red"; Type = "CRITIQUE" },
                # On ignore l'erreur de partition manquante car on a désactivé le core dump mais le bootloader le cherche encore
                @{ Pattern = "Core dump(?!.*No core dump partition)"; Color = "Red"; Type = "CRITIQUE" },
                @{ Pattern = "Watchdog.*timeout"; Color = "Red"; Type = "CRITIQUE" },
                @{ Pattern = "Démarrage FFP5CS v11|App start v11"; Color = "Green"; Type = "SUCCESS" },
                @{ Pattern = "Init done|Initialization complete"; Color = "Green"; Type = "SUCCESS" },
                @{ Pattern = "WiFi.*connecté|WiFi.*connected|WiFi connected"; Color = "Green"; Type = "SUCCESS" },
                @{ Pattern = "/api/status|/api/sensors"; Color = "Green"; Type = "INFO" },
                @{ Pattern = "heap.*free|Free heap|Free.*heap"; Color = "Yellow"; Type = "INFO" }
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
            $lines = Get-Content $logFile -ErrorAction SilentlyContinue
            if ($lines) {
                $lineCount = $lines.Count
                Write-Host "📈 Total lignes loggées: $lineCount" -ForegroundColor Gray
            }
        } else {
            Write-Host "⚠️ Le fichier de log est vide" -ForegroundColor Yellow
        }
    } else {
        Write-Host "⚠️ Aucun fichier de log généré" -ForegroundColor Yellow
    }
    
} catch {
    Write-Host "❌ Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Vérifiez les logs pour confirmer la stabilité du système." -ForegroundColor Cyan
