# Script d'itération pour stabilité système v11.80
# Flash et monitoring en boucle jusqu'à stabilité

param(
    [int]$MaxIterations = 5,
    [int]$MonitorDurationSeconds = 90
)

$iteration = 1
$stableCount = 0
$requiredStableRuns = 2

Write-Host "=== ITÉRATION STABILITÉ V11.80 ===" -ForegroundColor Green
Write-Host "Maximum d'itérations: $MaxIterations" -ForegroundColor Yellow
Write-Host "Durée monitoring par itération: $MonitorDurationSeconds secondes" -ForegroundColor Yellow
Write-Host "Runs stables requis pour arrêt: $requiredStableRuns" -ForegroundColor Yellow
Write-Host ""

while ($iteration -le $MaxIterations) {
    Write-Host "=== ITÉRATION $iteration/$MaxIterations ===" -ForegroundColor Cyan
    Write-Host "Démarrage: $(Get-Date -Format 'HH:mm:ss')" -ForegroundColor Gray
    
    # 1. Flash firmware
    Write-Host "1. Flash firmware..." -ForegroundColor Yellow
    $flashResult = pio run --target upload --upload-port COM6
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Échec flash firmware (itération $iteration)" -ForegroundColor Red
        $iteration++
        continue
    }
    Write-Host "✅ Flash firmware réussi" -ForegroundColor Green
    
    # Attendre stabilisation
    Start-Sleep -Seconds 3
    
    # 2. Flash SPIFFS
    Write-Host "2. Flash SPIFFS..." -ForegroundColor Yellow
    $spiffsResult = pio run --target uploadfs --upload-port COM6
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Échec flash SPIFFS (itération $iteration)" -ForegroundColor Red
        $iteration++
        continue
    }
    Write-Host "✅ Flash SPIFFS réussi" -ForegroundColor Green
    
    # Attendre stabilisation post-flash
    Start-Sleep -Seconds 5
    
    # 3. Monitoring
    Write-Host "3. Monitoring $MonitorDurationSeconds secondes..." -ForegroundColor Yellow
    $logFile = "monitor_iteration_${iteration}_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
    
    try {
        # Démarrer le monitoring en arrière-plan
        $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("device", "monitor", "--port", "COM6", "--baud", "115200") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
        
        # Attendre la durée spécifiée
        Start-Sleep -Seconds $MonitorDurationSeconds
        
        # Arrêter le monitoring
        if (-not $monitorProcess.HasExited) {
            $monitorProcess.Kill()
            Start-Sleep -Seconds 2
        }
        
        # Analyser les logs pour détecter des problèmes
        if (Test-Path $logFile) {
            $logContent = Get-Content $logFile -Raw
            
            # Rechercher des patterns d'erreur
            $criticalIssues = @(
                [regex]::Matches($logContent, "Guru Meditation", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase),
                [regex]::Matches($logContent, "Panic", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase),
                [regex]::Matches($logContent, "Brownout", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase),
                [regex]::Matches($logContent, "Core dump", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            )
            
            $totalIssues = $criticalIssues | ForEach-Object { $_.Count } | Measure-Object -Sum | Select-Object -ExpandProperty Sum
            
            if ($totalIssues -eq 0) {
                Write-Host "✅ Itération $iteration - Aucun problème critique détecté" -ForegroundColor Green
                $stableCount++
                
                if ($stableCount -ge $requiredStableRuns) {
                    Write-Host "🎉 SYSTÈME STABLE ! $requiredStableRuns runs consécutifs sans problème critique" -ForegroundColor Green
                    Write-Host "Arrêt des itérations - système fonctionnel" -ForegroundColor Green
                    break
                }
            } else {
                Write-Host "⚠️ Itération $iteration - $totalIssues problème(s) critique(s) détecté(s)" -ForegroundColor Red
                $stableCount = 0 # Reset du compteur de stabilité
                Write-Host "Logs disponibles: $logFile" -ForegroundColor Gray
            }
        } else {
            Write-Host "⚠️ Impossible de générer les logs (itération $iteration)" -ForegroundColor Yellow
            $stableCount = 0
        }
        
    } catch {
        Write-Host "❌ Erreur pendant le monitoring (itération $iteration): $($_.Exception.Message)" -ForegroundColor Red
        $stableCount = 0
    }
    
    $iteration++
    Write-Host ""
}

if ($iteration -gt $MaxIterations) {
    Write-Host "⚠️ Maximum d'itérations atteint ($MaxIterations) - Vérification manuelle requise" -ForegroundColor Yellow
}

Write-Host "=== FIN ITÉRATIONS ===" -ForegroundColor Green
Write-Host "Total itérations: $($iteration - 1)" -ForegroundColor Gray
Write-Host "Runs stables: $stableCount" -ForegroundColor Gray




