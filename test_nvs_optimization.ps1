# Test des optimisations NVS - Phase 1
# Script de validation des améliorations du gestionnaire NVS centralisé

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST OPTIMISATIONS NVS - PHASE 1" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Compilation du projet
Write-Host "`n[1/4] Compilation du projet..." -ForegroundColor Yellow
$compileResult = pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation" -ForegroundColor Red
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
$logFile = "monitor_nvs_optimization_$timestamp.log"

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
    
    # Vérifications spécifiques aux optimisations NVS
    $checks = @{
        "Initialisation NVS Manager" = $logContent -match "Initialisation du gestionnaire NVS centralisé"
        "Migration depuis ancien système" = $logContent -match "Migration depuis l'ancien système NVS"
        "Chargement flags depuis NVS centralisé" = $logContent -match "Chargement flags depuis NVS centralisé"
        "Sauvegarde flags vers NVS centralisé" = $logContent -match "Sauvegarde flags vers NVS centralisé"
        "Statistiques d'utilisation" = $logContent -match "Statistiques d'utilisation"
        "Aucune erreur critique" = -not ($logContent -match "Guru Meditation|Panic|Brownout|Core dump")
        "Cache NVS fonctionnel" = $logContent -match "Chargé depuis cache|Sauvegardé.*NVS"
    }
    
    Write-Host "`n📋 RÉSULTATS DES TESTS:" -ForegroundColor Cyan
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
    
    # Statistiques d'utilisation NVS
    $nvsStats = $logContent | Select-String "Statistiques d'utilisation" -Context 0, 10
    if ($nvsStats) {
        Write-Host "`n📊 STATISTIQUES NVS:" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        Write-Host $nvsStats.Context.PostContext -ForegroundColor White
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
        Write-Host "🎉 TOUS LES TESTS SONT PASSÉS!" -ForegroundColor Green
        Write-Host "✅ Les optimisations NVS Phase 1 sont fonctionnelles" -ForegroundColor Green
    } else {
        Write-Host "⚠️ CERTAINS TESTS ONT ÉCHOUÉ" -ForegroundColor Yellow
        Write-Host "🔍 Vérifiez les logs pour plus de détails" -ForegroundColor Yellow
    }
    Write-Host "========================================" -ForegroundColor Cyan
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile" -ForegroundColor Red
}

Write-Host "`n📁 Fichier de log: $logFile" -ForegroundColor Cyan
Write-Host "🔍 Pour analyser manuellement: Get-Content \$logFile" -ForegroundColor Cyan
