# Script de surveillance du monitoring ESP32 v11.51
# Surveille le processus de monitoring et analyse les logs

Write-Host "=== SURVEILLANCE MONITORING ESP32 v11.51 ===" -ForegroundColor Green
Write-Host "Heure de debut: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Yellow

# Attendre 20 minutes
Write-Host "Surveillance pendant 20 minutes..." -ForegroundColor Blue
Write-Host "Le monitoring PlatformIO est en cours..." -ForegroundColor Cyan

# Attendre 20 minutes (1200 secondes)
for ($i = 1; $i -le 1200; $i++) {
    if ($i % 60 -eq 0) {
        $minutes = $i / 60
        Write-Host "Temps ecoule: $minutes minutes" -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 1
}

Write-Host "`n20 minutes ecoulees - Analyse des logs..." -ForegroundColor Green

# Chercher les fichiers de log récents
$logFiles = Get-ChildItem -Path "." -Name "*monitor*" | Where-Object { $_ -like "*.log" } | Sort-Object LastWriteTime -Descending

if ($logFiles) {
    $latestLog = $logFiles[0]
    Write-Host "`n=== ANALYSE DU LOG: $latestLog ===" -ForegroundColor Magenta
    
    $logInfo = Get-Item $latestLog
    Write-Host "Taille: $([math]::Round($logInfo.Length/1KB, 2)) KB" -ForegroundColor Cyan
    Write-Host "Derniere modification: $($logInfo.LastWriteTime)" -ForegroundColor Cyan
    
    # Lire le contenu du log
    $logContent = Get-Content $latestLog -Raw
    $lines = ($logContent -split "`n").Count
    
    Write-Host "Nombre de lignes: $lines" -ForegroundColor Cyan
    
    # Analyser les erreurs
    Write-Host "`n=== ANALYSE DES ERREURS ===" -ForegroundColor Red
    
    # Guru Meditation / Panic
    $guruMeditation = ($logContent | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" -AllMatches).Matches.Count
    if ($guruMeditation -gt 0) {
        Write-Host "🔴 GURU MEDITATION/PANIC: $guruMeditation occurrences" -ForegroundColor Red
        $logContent | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" | ForEach-Object {
            Write-Host "  - $($_.Line)" -ForegroundColor Red
        }
    } else {
        Write-Host "✅ Aucun Guru Meditation/Panic detecte" -ForegroundColor Green
    }
    
    # Watchdog timeouts
    $watchdogTimeouts = ($logContent | Select-String -Pattern "watchdog.*timeout|WDT.*timeout" -AllMatches).Matches.Count
    if ($watchdogTimeouts -gt 0) {
        Write-Host "🟡 WATCHDOG TIMEOUTS: $watchdogTimeouts occurrences" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Aucun timeout watchdog detecte" -ForegroundColor Green
    }
    
    # Erreurs WiFi
    $wifiErrors = ($logContent | Select-String -Pattern "WiFi.*error|WiFi.*fail|WiFi.*timeout" -AllMatches).Matches.Count
    if ($wifiErrors -gt 0) {
        Write-Host "🟡 ERREURS WIFI: $wifiErrors occurrences" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Aucune erreur WiFi detectee" -ForegroundColor Green
    }
    
    # Erreurs WebSocket
    $websocketErrors = ($logContent | Select-String -Pattern "WebSocket.*error|WebSocket.*fail|WebSocket.*timeout" -AllMatches).Matches.Count
    if ($websocketErrors -gt 0) {
        Write-Host "🟡 ERREURS WEBSOCKET: $websocketErrors occurrences" -ForegroundColor Yellow
    } else {
        Write-Host "✅ Aucune erreur WebSocket detectee" -ForegroundColor Green
    }
    
    # Utilisation mémoire
    $memoryUsage = $logContent | Select-String -Pattern "heap.*free|memory.*free|RAM.*free" | Select-Object -Last 5
    if ($memoryUsage) {
        Write-Host "`n📊 Dernieres utilisations memoire:" -ForegroundColor Cyan
        $memoryUsage | ForEach-Object {
            Write-Host "  - $($_.Line)" -ForegroundColor Cyan
        }
    }
    
    # Dernières lignes du log
    Write-Host "`n📋 Dernieres lignes du log:" -ForegroundColor Magenta
    $lastLines = Get-Content $latestLog -Tail 10
    $lastLines | ForEach-Object {
        Write-Host "  $_" -ForegroundColor Gray
    }
    
    # Résumé final
    Write-Host "`n=== RESUMÉ FINAL ===" -ForegroundColor White -BackgroundColor DarkBlue
    
    $criticalIssues = $guruMeditation
    $warningIssues = $watchdogTimeouts + $wifiErrors + $websocketErrors
    
    if ($criticalIssues -eq 0) {
        Write-Host "✅ STABILITE: Aucun probleme critique detecte" -ForegroundColor Green
    } else {
        Write-Host "🔴 STABILITE: $criticalIssues probleme(s) critique(s) detecte(s)" -ForegroundColor Red
    }
    
    if ($warningIssues -eq 0) {
        Write-Host "✅ FONCTIONNEMENT: Aucun avertissement detecte" -ForegroundColor Green
    } else {
        Write-Host "🟡 FONCTIONNEMENT: $warningIssues avertissement(s) detecte(s)" -ForegroundColor Yellow
    }
    
} else {
    Write-Host "❌ Aucun fichier de log trouve" -ForegroundColor Red
}

Write-Host "`n=== FIN DE L'ANALYSE ===" -ForegroundColor Green
Write-Host "Heure de fin: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Yellow

