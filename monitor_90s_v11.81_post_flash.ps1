# Monitoring de 90 secondes apres flash v11.81
# Test des optimisations NVS Phase 1 et 2

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MONITORING POST-FLASH v11.81 (90s)" -ForegroundColor Cyan
Write-Host "Test optimisations NVS Phase 1 & 2" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_90s_v11.81_post_flash_$timestamp.log"

Write-Host "`nDemarrage du monitoring (90s)..." -ForegroundColor Cyan
Write-Host "Logs sauvegardes dans: $logFile" -ForegroundColor Cyan

# Demarrer le monitoring
$monitorProcess = Start-Process -FilePath "pio" -ArgumentList "device", "monitor" -RedirectStandardOutput $logFile -PassThru

# Attendre 90 secondes
Start-Sleep -Seconds 90

# Arreter le monitoring
Stop-Process -Id $monitorProcess.Id -Force
Write-Host "Monitoring termine" -ForegroundColor Green

# Analyse des logs
Write-Host "`nANALYSE DES LOGS:" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if (Test-Path $logFile) {
    $logContent = Get-Content $logFile -Raw
    
    # Verifications specifiques aux optimisations NVS
    $checks = @{
        "Version 11.81 detectee" = $logContent -match "v11\.81|Version.*11\.81"
        "Initialisation NVS Manager" = $logContent -match "Initialisation du gestionnaire NVS centralise"
        "Migration depuis ancien systeme" = $logContent -match "Migration depuis l'ancien systeme NVS"
        "Flush differe active" = $logContent -match "Flush differe.*active"
        "Intervalle flush configure" = $logContent -match "Intervalle flush:"
        "Compression JSON fonctionnelle" = $logContent -match "Compression JSON pour|JSON compresse"
        "Decompression JSON fonctionnelle" = $logContent -match "Decompression JSON pour|JSON decompresse"
        "Cache unifie actif" = $logContent -match "Cle marquee sale|Flush force"
        "Verification periodique" = $logContent -match "Verification periodique du flush differe"
        "Aucune erreur critique" = -not ($logContent -match "Guru Meditation|Panic|Brownout|Core dump")
        "Statistiques de compression" = $logContent -match "bytes.*%"
    }
    
    Write-Host "`nRESULTATS DES TESTS:" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    $allPassed = $true
    foreach ($check in $checks.GetEnumerator()) {
        $status = if ($check.Value) { "PASS" } else { "FAIL" }
        $color = if ($check.Value) { "Green" } else { "Red" }
        Write-Host "$($check.Key): $status" -ForegroundColor $color
        
        if (-not $check.Value) {
            $allPassed = $false
        }
    }
    
    # Statistiques de compression
    $compressionStats = $logContent | Select-String "JSON compresse.*bytes.*%" -AllMatches
    if ($compressionStats) {
        Write-Host "`nSTATISTIQUES DE COMPRESSION:" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        foreach ($stat in $compressionStats.Matches) {
            Write-Host "  $($stat.Value)" -ForegroundColor White
        }
    }
    
    # Statistiques de flush
    $flushStats = $logContent | Select-String "Flush.*termine|Cle marquee sale" -AllMatches
    if ($flushStats) {
        Write-Host "`nSTATISTIQUES DE FLUSH:" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        $flushCount = ($logContent | Select-String "Cle marquee sale").Count
        $forceFlushCount = ($logContent | Select-String "Flush force").Count
        Write-Host "  Cles marquees sales: $flushCount" -ForegroundColor White
        Write-Host "  Flush forces: $forceFlushCount" -ForegroundColor White
    }
    
    # Detection d'erreurs
    $errors = $logContent | Select-String "ERROR|FAIL" | Measure-Object
    if ($errors.Count -gt 0) {
        Write-Host "`nERREURS DETECTEES: $($errors.Count)" -ForegroundColor Yellow
        $logContent | Select-String "ERROR|FAIL" | ForEach-Object {
            Write-Host "  $($_.Line)" -ForegroundColor Yellow
        }
    }
    
    Write-Host "`n========================================" -ForegroundColor Cyan
    if ($allPassed) {
        Write-Host "TOUS LES TESTS SONT PASSES!" -ForegroundColor Green
        Write-Host "Les optimisations NVS Phase 1 & 2 sont fonctionnelles" -ForegroundColor Green
        Write-Host "Compression JSON et cache unifie operationnels" -ForegroundColor Green
    } else {
        Write-Host "CERTAINS TESTS ONT ECHOUE" -ForegroundColor Yellow
        Write-Host "Verifiez les logs pour plus de details" -ForegroundColor Yellow
    }
    Write-Host "========================================" -ForegroundColor Cyan
    
} else {
    Write-Host "Fichier de log non trouve: $logFile" -ForegroundColor Red
}

Write-Host "`nFichier de log: $logFile" -ForegroundColor Cyan
Write-Host "Pour analyser manuellement: Get-Content `$logFile" -ForegroundColor Cyan
Write-Host "`nRESUME OPTIMISATIONS NVS:" -ForegroundColor Cyan
Write-Host "- Phase 1: Gestionnaire centralise, consolidation namespaces" -ForegroundColor White
Write-Host "- Phase 2: Compression JSON, cache unifie avec flush differe" -ForegroundColor White
Write-Host "- Reduction estimee: 50-60% espace NVS, 70-80% ecritures" -ForegroundColor White