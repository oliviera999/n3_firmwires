# Surveillance et analyse automatique du monitoring ESP32-WROOM v11.49
# Conformément aux règles du projet FFP5CS

Write-Host "🔍 Surveillance du monitoring ESP32-WROOM v11.49" -ForegroundColor Green
Write-Host "📅 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

# Attendre que le monitoring génère des logs
Write-Host "⏳ Attente des logs de monitoring..." -ForegroundColor Yellow

$maxWaitTime = 1800  # 30 minutes
$elapsed = 0
$checkInterval = 30  # Vérifier toutes les 30 secondes

while ($elapsed -lt $maxWaitTime) {
    Start-Sleep -Seconds $checkInterval
    $elapsed += $checkInterval
    
    # Vérifier les fichiers de log
    $logFiles = Get-ChildItem -Path "." -Filter "monitor_v11.49_wroom_test_30min_*.log" | Sort-Object LastWriteTime -Descending
    
    if ($logFiles) {
        $latestLog = $logFiles[0]
        $logSize = $latestLog.Length
        $logAge = (Get-Date) - $latestLog.LastWriteTime
        
        Write-Host "📝 Log trouvé: $($latestLog.Name)" -ForegroundColor Green
        Write-Host "   Taille: $([math]::Round($logSize/1KB, 2)) KB" -ForegroundColor White
        Write-Host "   Âge: $([math]::Round($logAge.TotalMinutes, 1)) minutes" -ForegroundColor White
        
        # Si le fichier n'a pas été modifié depuis plus de 2 minutes, le monitoring est probablement terminé
        if ($logAge.TotalMinutes -gt 2) {
            Write-Host "✅ Monitoring terminé - Analyse des logs..." -ForegroundColor Green
            break
        }
        
        # Afficher les dernières lignes
        Write-Host "📊 Dernières lignes:" -ForegroundColor Cyan
        Get-Content $latestLog.FullName -Tail 5 | ForEach-Object { Write-Host "   $_" -ForegroundColor White }
        
    } else {
        $minutesElapsed = [math]::Floor($elapsed / 60)
        $minutesRemaining = [math]::Floor(($maxWaitTime - $elapsed) / 60)
        Write-Host "⏳ Temps écoulé: $minutesElapsed min | Restant: $minutesRemaining min" -ForegroundColor Cyan
    }
}

# Analyser les logs si disponibles
$logFiles = Get-ChildItem -Path "." -Filter "monitor_v11.49_wroom_test_30min_*.log" | Sort-Object LastWriteTime -Descending

if ($logFiles) {
    $latestLog = $logFiles[0]
    Write-Host "`n🔍 ANALYSE DES LOGS" -ForegroundColor Green
    Write-Host "===================" -ForegroundColor Green
    
    $logContent = Get-Content $latestLog.FullName
    
    # Statistiques générales
    Write-Host "📊 Statistiques générales:" -ForegroundColor Cyan
    Write-Host "   Lignes: $($logContent.Count)" -ForegroundColor White
    Write-Host "   Taille: $([math]::Round($latestLog.Length/1KB, 2)) KB" -ForegroundColor White
    
    # Recherche d'erreurs critiques
    Write-Host "`n🔴 Recherche d'erreurs critiques:" -ForegroundColor Red
    
    $criticalErrors = @{
        "Guru Meditation" = 0
        "Panic" = 0
        "Brownout" = 0
        "Core dump" = 0
        "Stack overflow" = 0
        "Assert failed" = 0
    }
    
    foreach ($line in $logContent) {
        if ($line -match "Guru Meditation|GURU MEDITATION") {
            $criticalErrors["Guru Meditation"]++
            Write-Host "🚨 GURU MEDITATION: $line" -ForegroundColor Red
        }
        if ($line -match "Panic|PANIC") {
            $criticalErrors["Panic"]++
            Write-Host "🚨 PANIC: $line" -ForegroundColor Red
        }
        if ($line -match "Brownout|BROWNOUT") {
            $criticalErrors["Brownout"]++
            Write-Host "🚨 BROWNOUT: $line" -ForegroundColor Red
        }
        if ($line -match "Core dump|CORE DUMP") {
            $criticalErrors["Core dump"]++
            Write-Host "🚨 CORE DUMP: $line" -ForegroundColor Red
        }
        if ($line -match "Stack overflow|STACK OVERFLOW") {
            $criticalErrors["Stack overflow"]++
            Write-Host "🚨 STACK OVERFLOW: $line" -ForegroundColor Red
        }
        if ($line -match "Assert failed|ASSERT FAILED") {
            $criticalErrors["Assert failed"]++
            Write-Host "🚨 ASSERT FAILED: $line" -ForegroundColor Red
        }
    }
    
    $totalCritical = ($criticalErrors.Values | Measure-Object -Sum).Sum
    if ($totalCritical -eq 0) {
        Write-Host "✅ Aucune erreur critique détectée" -ForegroundColor Green
    } else {
        Write-Host "❌ $totalCritical erreurs critiques détectées" -ForegroundColor Red
    }
    
    # Recherche de warnings
    Write-Host "`n🟡 Recherche de warnings:" -ForegroundColor Yellow
    
    $warnings = @{
        "Watchdog" = 0
        "WiFi timeout" = 0
        "HTTP error" = 0
        "Memory warning" = 0
    }
    
    foreach ($line in $logContent) {
        if ($line -match "watchdog|WATCHDOG") { $warnings["Watchdog"]++ }
        if ($line -match "WiFi.*timeout|WIFI.*TIMEOUT") { $warnings["WiFi timeout"]++ }
        if ($line -match "HTTP.*[45][0-9][0-9]") { $warnings["HTTP error"]++ }
        if ($line -match "memory|Memory|MEMORY") { $warnings["Memory warning"]++ }
    }
    
    $totalWarnings = ($warnings.Values | Measure-Object -Sum).Sum
    if ($totalWarnings -eq 0) {
        Write-Host "✅ Aucun warning détecté" -ForegroundColor Green
    } else {
        Write-Host "⚠️  $totalWarnings warnings détectés" -ForegroundColor Yellow
        foreach ($warning in $warnings.GetEnumerator()) {
            if ($warning.Value -gt 0) {
                Write-Host "   $($warning.Key): $($warning.Value)" -ForegroundColor Yellow
            }
        }
    }
    
    # Recommandations finales
    Write-Host "`n🎯 RECOMMANDATIONS FINALES" -ForegroundColor Green
    Write-Host "===========================" -ForegroundColor Green
    
    if ($totalCritical -eq 0 -and $totalWarnings -lt 20) {
        Write-Host "✅ SYSTÈME STABLE - Prêt pour production" -ForegroundColor Green
        Write-Host "   - Aucune erreur critique sur 30 minutes" -ForegroundColor White
        Write-Host "   - Warnings acceptables" -ForegroundColor White
        Write-Host "   - Système opérationnel" -ForegroundColor White
    } elseif ($totalCritical -eq 0) {
        Write-Host "⚠️  SYSTÈME FONCTIONNEL - Optimisations recommandées" -ForegroundColor Yellow
        Write-Host "   - Aucune erreur critique" -ForegroundColor White
        Write-Host "   - Corriger les warnings" -ForegroundColor White
    } else {
        Write-Host "🔴 SYSTÈME INSTABLE - Correction requise" -ForegroundColor Red
        Write-Host "   - Erreurs critiques détectées" -ForegroundColor White
        Write-Host "   - Arrêter le déploiement" -ForegroundColor White
    }
    
} else {
    Write-Host "❌ Aucun fichier de log trouvé après 30 minutes" -ForegroundColor Red
    Write-Host "   Vérifier que le monitoring est bien lancé" -ForegroundColor White
}

Write-Host "`n📝 Analyse terminée le $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
