# Script de monitoring simple pour ESP32 v11.51
# Capture les logs pendant 20 minutes et génère un rapport

Write-Host "=== DEMARRAGE MONITORING ESP32 v11.51 ===" -ForegroundColor Green
Write-Host "Heure de debut: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Yellow

# Créer le nom du fichier de log
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_20min_v11.51_$timestamp.log"

Write-Host "Fichier de log: $logFile" -ForegroundColor Cyan

# Démarrer le monitoring en arrière-plan
Write-Host "Demarrage du monitoring PlatformIO..." -ForegroundColor Blue

try {
    # Démarrer pio device monitor en arrière-plan
    $process = Start-Process -FilePath "pio" -ArgumentList "device monitor --port COM6 --baud 115200 --echo" -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError $logFile
    
    Write-Host "Monitoring demarre (PID: $($process.Id))" -ForegroundColor Green
    
    # Attendre 20 minutes (1200 secondes)
    Write-Host "Attente de 20 minutes..." -ForegroundColor Yellow
    Start-Sleep -Seconds 1200
    
    # Arrêter le processus
    Write-Host "Arret du monitoring..." -ForegroundColor Yellow
    if (-not $process.HasExited) {
        $process.CloseMainWindow()
        Start-Sleep -Seconds 2
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
    
    Write-Host "Monitoring termine" -ForegroundColor Green
    
    # Analyser le fichier de log
    if (Test-Path $logFile) {
        Write-Host "`n=== ANALYSE DU LOG ===" -ForegroundColor Magenta
        
        $logContent = Get-Content $logFile -Raw
        $lines = ($logContent -split "`n").Count
        $size = (Get-Item $logFile).Length
        
        Write-Host "Taille du fichier: $([math]::Round($size/1KB, 2)) KB" -ForegroundColor Cyan
        Write-Host "Nombre de lignes: $lines" -ForegroundColor Cyan
        
        # Rechercher les erreurs critiques
        $guruMeditation = ($logContent | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" -AllMatches).Matches.Count
        $watchdogTimeouts = ($logContent | Select-String -Pattern "watchdog.*timeout|WDT.*timeout" -AllMatches).Matches.Count
        $wifiErrors = ($logContent | Select-String -Pattern "WiFi.*error|WiFi.*fail" -AllMatches).Matches.Count
        $websocketErrors = ($logContent | Select-String -Pattern "WebSocket.*error|WebSocket.*fail" -AllMatches).Matches.Count
        
        Write-Host "`n=== RESULTATS ===" -ForegroundColor White -BackgroundColor DarkBlue
        
        if ($guruMeditation -eq 0) {
            Write-Host "✅ Aucun Guru Meditation/Panic detecte" -ForegroundColor Green
        } else {
            Write-Host "🔴 Guru Meditation/Panic: $guruMeditation occurrences" -ForegroundColor Red
        }
        
        if ($watchdogTimeouts -eq 0) {
            Write-Host "✅ Aucun timeout watchdog detecte" -ForegroundColor Green
        } else {
            Write-Host "🟡 Timeouts watchdog: $watchdogTimeouts occurrences" -ForegroundColor Yellow
        }
        
        if ($wifiErrors -eq 0) {
            Write-Host "✅ Aucune erreur WiFi detectee" -ForegroundColor Green
        } else {
            Write-Host "🟡 Erreurs WiFi: $wifiErrors occurrences" -ForegroundColor Yellow
        }
        
        if ($websocketErrors -eq 0) {
            Write-Host "✅ Aucune erreur WebSocket detectee" -ForegroundColor Green
        } else {
            Write-Host "🟡 Erreurs WebSocket: $websocketErrors occurrences" -ForegroundColor Yellow
        }
        
        # Afficher les dernières lignes
        Write-Host "`n=== DERNIERES LIGNES ===" -ForegroundColor Magenta
        $lastLines = Get-Content $logFile -Tail 10
        $lastLines | ForEach-Object {
            Write-Host "  $_" -ForegroundColor Gray
        }
        
    } else {
        Write-Host "❌ Fichier de log non trouve: $logFile" -ForegroundColor Red
    }
    
} catch {
    Write-Host "❌ Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n=== FIN DU MONITORING ===" -ForegroundColor Green
Write-Host "Heure de fin: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Yellow

