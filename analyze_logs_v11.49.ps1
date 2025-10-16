# Analyseur automatique des logs ESP32-WROOM v11.49
# Conformément aux règles du projet FFP5CS

param(
    [string]$LogFile = "",
    [string]$ErrorFile = ""
)

Write-Host "🔍 Analyseur de logs ESP32-WROOM v11.49" -ForegroundColor Green
Write-Host "📅 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

# Trouver les fichiers de log les plus récents si non spécifiés
if ($LogFile -eq "") {
    $LogFile = Get-ChildItem -Path "." -Filter "monitor_v11.49_wroom_test_30min_*.log" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($LogFile) {
        $LogFile = $LogFile.FullName
    }
}

if ($ErrorFile -eq "") {
    $ErrorFile = Get-ChildItem -Path "." -Filter "monitor_v11.49_wroom_test_30min_*.log.err" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($ErrorFile) {
        $ErrorFile = $ErrorFile.FullName
    }
}

Write-Host "📝 Log file: $LogFile" -ForegroundColor Yellow
Write-Host "⚠️  Error file: $ErrorFile" -ForegroundColor Yellow

if (-not (Test-Path $LogFile)) {
    Write-Host "❌ Fichier de log non trouvé: $LogFile" -ForegroundColor Red
    exit 1
}

# Lire le contenu du log
$logContent = Get-Content $LogFile
$errorContent = if (Test-Path $ErrorFile) { Get-Content $ErrorFile } else { @() }

Write-Host "`n📊 STATISTIQUES GÉNÉRALES" -ForegroundColor Cyan
Write-Host "=========================" -ForegroundColor Cyan
Write-Host "Lignes de log: $($logContent.Count)" -ForegroundColor White
Write-Host "Taille fichier: $([math]::Round((Get-Item $LogFile).Length/1KB, 2)) KB" -ForegroundColor White
Write-Host "Erreurs capturées: $($errorContent.Count)" -ForegroundColor White

# Analyse des erreurs critiques
Write-Host "`n🔴 ANALYSE DES ERREURS CRITIQUES" -ForegroundColor Red
Write-Host "=================================" -ForegroundColor Red

$criticalErrors = @{
    "Guru Meditation" = 0
    "Panic" = 0
    "Brownout" = 0
    "Core dump" = 0
    "Stack overflow" = 0
    "Assert failed" = 0
    "Exception" = 0
}

foreach ($line in $logContent) {
    if ($line -match "Guru Meditation|GURU MEDITATION") {
        $criticalErrors["Guru Meditation"]++
        Write-Host "🚨 GURU MEDITATION détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Panic|PANIC") {
        $criticalErrors["Panic"]++
        Write-Host "🚨 PANIC détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Brownout|BROWNOUT") {
        $criticalErrors["Brownout"]++
        Write-Host "🚨 BROWNOUT détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Core dump|CORE DUMP") {
        $criticalErrors["Core dump"]++
        Write-Host "🚨 CORE DUMP détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Stack overflow|STACK OVERFLOW") {
        $criticalErrors["Stack overflow"]++
        Write-Host "🚨 STACK OVERFLOW détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Assert failed|ASSERT FAILED") {
        $criticalErrors["Assert failed"]++
        Write-Host "🚨 ASSERT FAILED détecté: $line" -ForegroundColor Red
    }
    if ($line -match "Exception|EXCEPTION") {
        $criticalErrors["Exception"]++
    }
}

$totalCritical = ($criticalErrors.Values | Measure-Object -Sum).Sum
if ($totalCritical -eq 0) {
    Write-Host "✅ Aucune erreur critique détectée" -ForegroundColor Green
} else {
    Write-Host "❌ $totalCritical erreurs critiques détectées" -ForegroundColor Red
    foreach ($error in $criticalErrors.GetEnumerator()) {
        if ($error.Value -gt 0) {
            Write-Host "   $($error.Key): $($error.Value)" -ForegroundColor Red
        }
    }
}

# Analyse des warnings système
Write-Host "`n🟡 ANALYSE DES WARNINGS SYSTÈME" -ForegroundColor Yellow
Write-Host "===============================" -ForegroundColor Yellow

$systemWarnings = @{
    "Watchdog" = 0
    "WiFi timeout" = 0
    "WebSocket timeout" = 0
    "HTTP error" = 0
    "Memory warning" = 0
}

foreach ($line in $logContent) {
    if ($line -match "watchdog|WATCHDOG") {
        $systemWarnings["Watchdog"]++
    }
    if ($line -match "WiFi.*timeout|WIFI.*TIMEOUT") {
        $systemWarnings["WiFi timeout"]++
    }
    if ($line -match "WebSocket.*timeout|WEBSOCKET.*TIMEOUT") {
        $systemWarnings["WebSocket timeout"]++
    }
    if ($line -match "HTTP.*[45][0-9][0-9]") {
        $systemWarnings["HTTP error"]++
    }
    if ($line -match "memory|Memory|MEMORY") {
        $systemWarnings["Memory warning"]++
    }
}

$totalWarnings = ($systemWarnings.Values | Measure-Object -Sum).Sum
if ($totalWarnings -eq 0) {
    Write-Host "✅ Aucun warning système détecté" -ForegroundColor Green
} else {
    Write-Host "⚠️  $totalWarnings warnings système détectés" -ForegroundColor Yellow
    foreach ($warning in $systemWarnings.GetEnumerator()) {
        if ($warning.Value -gt 0) {
            Write-Host "   $($warning.Key): $($warning.Value)" -ForegroundColor Yellow
        }
    }
}

# Analyse de la mémoire
Write-Host "`n📊 ANALYSE DE LA MÉMOIRE" -ForegroundColor Cyan
Write-Host "========================" -ForegroundColor Cyan

$heapValues = @()
foreach ($line in $logContent) {
    if ($line -match "Free heap: (\d+) bytes") {
        $heapValues += [int]$matches[1]
    }
}

if ($heapValues.Count -gt 0) {
    $minHeap = ($heapValues | Measure-Object -Minimum).Minimum
    $maxHeap = ($heapValues | Measure-Object -Maximum).Maximum
    $avgHeap = [math]::Round(($heapValues | Measure-Object -Average).Average, 0)
    
    Write-Host "Heap libre - Min: $minHeap bytes, Max: $maxHeap bytes, Moy: $avgHeap bytes" -ForegroundColor White
    
    if ($minHeap -lt 50000) {
        Write-Host "⚠️  Heap minimum critique (< 50KB)" -ForegroundColor Yellow
    } elseif ($minHeap -lt 80000) {
        Write-Host "⚠️  Heap minimum faible (< 80KB)" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Heap stable" -ForegroundColor Green
    }
} else {
    Write-Host "⚠️  Aucune donnée de heap trouvée" -ForegroundColor Yellow
}

# Analyse du réseau
Write-Host "`n🌐 ANALYSE DU RÉSEAU" -ForegroundColor Cyan
Write-Host "====================" -ForegroundColor Cyan

$httpSuccess = 0
$httpErrors = 0
$rssiValues = @()

foreach ($line in $logContent) {
    if ($line -match "HTTP.*200") {
        $httpSuccess++
    }
    if ($line -match "HTTP.*[45][0-9][0-9]") {
        $httpErrors++
    }
    if ($line -match "RSSI: (-\d+) dBm") {
        $rssiValues += [int]$matches[1]
    }
}

if ($httpSuccess -gt 0 -or $httpErrors -gt 0) {
    $totalHttp = $httpSuccess + $httpErrors
    $successRate = [math]::Round(($httpSuccess / $totalHttp) * 100, 1)
    Write-Host "HTTP Success Rate: $successRate% ($httpSuccess/$totalHttp)" -ForegroundColor White
    
    if ($successRate -lt 80) {
        Write-Host "⚠️  Taux de succès HTTP faible" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Taux de succès HTTP bon" -ForegroundColor Green
    }
}

if ($rssiValues.Count -gt 0) {
    $minRssi = ($rssiValues | Measure-Object -Minimum).Minimum
    $maxRssi = ($rssiValues | Measure-Object -Maximum).Maximum
    Write-Host "RSSI - Min: $minRssi dBm, Max: $maxRssi dBm" -ForegroundColor White
    
    if ($minRssi -lt -80) {
        Write-Host "⚠️  Signal WiFi faible" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Signal WiFi stable" -ForegroundColor Green
    }
}

# Recommandations finales
Write-Host "`n🎯 RECOMMANDATIONS FINALES" -ForegroundColor Green
Write-Host "===========================" -ForegroundColor Green

if ($totalCritical -eq 0 -and $totalWarnings -lt 10) {
    Write-Host "✅ SYSTÈME STABLE - Prêt pour production" -ForegroundColor Green
    Write-Host "   - Aucune erreur critique" -ForegroundColor White
    Write-Host "   - Warnings système acceptables" -ForegroundColor White
    Write-Host "   - Mémoire et réseau stables" -ForegroundColor White
} elseif ($totalCritical -eq 0) {
    Write-Host "⚠️  SYSTÈME FONCTIONNEL - Optimisations recommandées" -ForegroundColor Yellow
    Write-Host "   - Aucune erreur critique" -ForegroundColor White
    Write-Host "   - Corriger les warnings système" -ForegroundColor White
    Write-Host "   - Optimiser la mémoire si nécessaire" -ForegroundColor White
} else {
    Write-Host "🔴 SYSTÈME INSTABLE - Correction requise" -ForegroundColor Red
    Write-Host "   - Erreurs critiques détectées" -ForegroundColor White
    Write-Host "   - Arrêter le déploiement" -ForegroundColor White
    Write-Host "   - Analyser et corriger les problèmes" -ForegroundColor White
}

Write-Host "`n📝 Rapport généré le $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
