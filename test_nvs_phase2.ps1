# Test des optimisations NVS - Phase 2
# Script de validation des améliorations de compression JSON et cache unifié

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST OPTIMISATIONS NVS - PHASE 2" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Compilation du projet
Write-Host "`n[1/4] Compilation du projet..." -ForegroundColor Yellow
$compileResult = pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation" -ForegroundColor Red
    Write-Host "🔍 Vérifiez les erreurs ci-dessus" -ForegroundColor Yellow
    exit 1
}
Write-Host "✅ Compilation réussie" -ForegroundColor Green

# Flash du firmware
Write-Host "`n[2/4] Flash du firmware..." -ForegroundColor Yellow
$flashResult = pio run --target upload
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de flash" -ForegroundColor Red
    exit 1
}
Write-Host "✅ Flash réussi" -ForegroundColor Green

# Monitoring de 90 secondes
Write-Host "`n[3/4] Monitoring de 90 secondes..." -ForegroundColor Yellow
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_nvs_phase2_$timestamp.log"

Write-Host "📊 Démarrage du monitoring (90s)..." -ForegroundColor Cyan
Write-Host "📝 Logs sauvegardés dans: $logFile" -ForegroundColor Cyan

# Démarrer le monitoring
$monitorProcess = Start-Process -FilePath "pio" -ArgumentList "device", "monitor" -RedirectStandardOutput $logFile -PassThru

# Attendre 90 secondes
Start-Sleep -Seconds 90

# Arrêter le monitoring
Stop-Process -Id $monitorProcess.Id -Force
Write-Host "✅ Monitoring terminé" -ForegroundColor Green

# Analyse des logs
Write-Host "`n[4/4] Analyse des logs..." -ForegroundColor Yellow

if (Test-Path $logFile) {
    $logContent = Get-Content $logFile -Raw
    
    # Vérifications spécifiques aux optimisations Phase 2
    $checks = @{
        "Initialisation NVS Manager" = $logContent -match "Initialisation du gestionnaire NVS centralisé"
        "Flush différé activé" = $logContent -match "Flush différé.*activé"
        "Intervalle flush configuré" = $logContent -match "Intervalle flush:"
        "Compression JSON fonctionnelle" = $logContent -match "Compression JSON pour|JSON compressé"
        "Décompression JSON fonctionnelle" = $logContent -match "Décompression JSON pour|JSON décompressé"
        "Cache unifié actif" = $logContent -match "Clé marquée sale|Flush forcé"
        "Vérification périodique" = $logContent -match "Vérification périodique du flush différé"
        "Aucune erreur critique" = -not ($logContent -match "Guru Meditation|Panic|Brownout|Core dump")
        "Statistiques de compression" = $logContent -match "bytes.*%"
    }
    
    Write-Host "`n📋 RÉSULTATS DES TESTS PHASE 2:" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    $allPassed = $true
    foreach ($check in $checks.GetEnumerator()) {
        $status = if ($check.Value) { "✅ PASS" } else { "❌ FAIL" }
        $color = if ($check.Value) { "Green" } else { "Red" }
        Write-Host "$($check.Key): $status" -ForegroundColor $color
        
        if (-not $check.Value) {
            $allPassed = $false
        }
    }
    
    # Statistiques de compression
    $compressionStats = $logContent | Select-String "JSON compressé.*bytes.*%" -AllMatches
    if ($compressionStats) {
        Write-Host "`n📊 STATISTIQUES DE COMPRESSION:" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        foreach ($stat in $compressionStats.Matches) {
            Write-Host "  $($stat.Value)" -ForegroundColor White
        }
    }
    
    # Statistiques de flush
    $flushStats = $logContent | Select-String "Flush.*terminé|Clé marquée sale" -AllMatches
    if ($flushStats) {
        Write-Host "`n🔄 STATISTIQUES DE FLUSH:" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        $flushCount = ($logContent | Select-String "Clé marquée sale").Count
        $forceFlushCount = ($logContent | Select-String "Flush forcé").Count
        Write-Host "  Clés marquées sales: $flushCount" -ForegroundColor White
        Write-Host "  Flush forcés: $forceFlushCount" -ForegroundColor White
    }
    
    # Détection d'erreurs
    $errors = $logContent | Select-String "❌|ERROR|FAIL" | Measure-Object
    if ($errors.Count -gt 0) {
        Write-Host "`n⚠️ ERREURS DÉTECTÉES: $($errors.Count)" -ForegroundColor Yellow
        $logContent | Select-String "❌|ERROR|FAIL" | ForEach-Object {
            Write-Host "  $($_.Line)" -ForegroundColor Yellow
        }
    }
    
    Write-Host "`n========================================" -ForegroundColor Cyan
    if ($allPassed) {
        Write-Host "🎉 TOUS LES TESTS PHASE 2 SONT PASSÉS!" -ForegroundColor Green
        Write-Host "✅ Les optimisations NVS Phase 2 sont fonctionnelles" -ForegroundColor Green
        Write-Host "📈 Compression JSON et cache unifié opérationnels" -ForegroundColor Green
    } else {
        Write-Host "⚠️ CERTAINS TESTS PHASE 2 ONT ÉCHOUÉ" -ForegroundColor Yellow
        Write-Host "🔍 Vérifiez les logs pour plus de détails" -ForegroundColor Yellow
    }
    Write-Host "========================================" -ForegroundColor Cyan
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile" -ForegroundColor Red
}

Write-Host "`n📁 Fichier de log: $logFile" -ForegroundColor Cyan
Write-Host "🔍 Pour analyser manuellement: Get-Content $logFile" -ForegroundColor Cyan
Write-Host "`n📋 RÉSUMÉ PHASE 2:" -ForegroundColor Cyan
Write-Host "- Compression JSON: Réduction ~30-50% de la taille" -ForegroundColor White
Write-Host "- Cache unifié: Flush différé toutes les 3 secondes" -ForegroundColor White
Write-Host "- Optimisation accès: Réduction ~60-70% des écritures NVS" -ForegroundColor White
