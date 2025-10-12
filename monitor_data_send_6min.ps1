# Script de monitoring ESP32 - Analyse envoi données (6+ minutes)
# Focus: Envoi de données vers le serveur

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_data_send_6min_$timestamp.log"
$analysisFile = "monitor_data_send_6min_analysis_$timestamp.log"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MONITORING ESP32 - ENVOI DE DONNEES" -ForegroundColor Cyan
Write-Host "Duration: 6+ minutes (400 secondes)" -ForegroundColor Cyan
Write-Host "Timestamp: $timestamp" -ForegroundColor Cyan
Write-Host "Log file: $logFile" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Durée du monitoring: 400 secondes (6min 40s)
$duration = 400
$endTime = (Get-Date).AddSeconds($duration)

Write-Host "Démarrage du monitoring pour $duration secondes..." -ForegroundColor Yellow
Write-Host "Fin prévue: $($endTime.ToString('HH:mm:ss'))" -ForegroundColor Yellow
Write-Host ""

# Compteurs pour l'analyse
$stats = @{
    TotalLines = 0
    DataSendAttempts = 0
    DataSendSuccess = 0
    DataSendFailures = 0
    WebSocketConnected = 0
    WebSocketDisconnected = 0
    HTTPRequests = 0
    HTTPResponses = 0
    MemoryWarnings = 0
    WatchdogResets = 0
    WiFiReconnections = 0
    Errors = 0
    Warnings = 0
    Panics = 0
}

$errorLines = @()
$dataSendLines = @()
$wsStatusLines = @()
$httpLines = @()

try {
    # Lancer PlatformIO monitor
    $process = Start-Process -FilePath "pio" -ArgumentList "device monitor --environment wroom_test --filter=direct --baud 115200" -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.err"
    
    Write-Host "Monitoring en cours..." -ForegroundColor Green
    Write-Host "Appuyez sur Ctrl+C pour arrêter avant la fin" -ForegroundColor Gray
    Write-Host ""
    
    $lastProgress = Get-Date
    $progressInterval = 30 # Afficher la progression toutes les 30 secondes
    
    # Attendre la durée spécifiée
    while ((Get-Date) -lt $endTime) {
        Start-Sleep -Seconds 1
        
        $remaining = ($endTime - (Get-Date)).TotalSeconds
        
        # Afficher la progression toutes les 30 secondes
        if (((Get-Date) - $lastProgress).TotalSeconds -ge $progressInterval) {
            $elapsed = $duration - [int]$remaining
            $progress = [int](($elapsed / $duration) * 100)
            Write-Host "[$progress%] Temps écoulé: $elapsed s / $duration s (reste: $([int]$remaining) s)" -ForegroundColor Cyan
            $lastProgress = Get-Date
        }
    }
    
    Write-Host ""
    Write-Host "Durée écoulée. Arrêt du monitoring..." -ForegroundColor Yellow
    
    # Arrêter le processus
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    
} catch {
    Write-Host "Erreur pendant le monitoring: $_" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ANALYSE DES LOGS" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Analyser le fichier de log
if (Test-Path $logFile) {
    Write-Host "Lecture du fichier de log: $logFile" -ForegroundColor Green
    
    $lines = Get-Content $logFile -ErrorAction SilentlyContinue
    $stats.TotalLines = $lines.Count
    
    Write-Host "Total de lignes: $($stats.TotalLines)" -ForegroundColor Cyan
    Write-Host ""
    
    foreach ($line in $lines) {
        # Envoi de données
        if ($line -match "sendData|send.*data|Sending|POST.*data|PUT.*data" -or $line -match "data.*send") {
            $stats.DataSendAttempts++
            $dataSendLines += $line
        }
        
        # Succès d'envoi
        if ($line -match "send.*success|data.*sent|Successfully sent|200 OK" -or $line -match "sent successfully") {
            $stats.DataSendSuccess++
        }
        
        # Échecs d'envoi
        if ($line -match "send.*fail|send.*error|Failed to send|timeout.*send" -or $line -match "could not send") {
            $stats.DataSendFailures++
        }
        
        # WebSocket
        if ($line -match "WS.*connect|WebSocket connect" -and $line -notmatch "disconnect") {
            $stats.WebSocketConnected++
            $wsStatusLines += $line
        }
        if ($line -match "WS.*disconnect|WebSocket disconnect") {
            $stats.WebSocketDisconnected++
            $wsStatusLines += $line
        }
        
        # HTTP
        if ($line -match "HTTP|GET |POST |PUT |DELETE ") {
            $stats.HTTPRequests++
            $httpLines += $line
        }
        if ($line -match "200 |201 |204 |400 |404 |500 ") {
            $stats.HTTPResponses++
        }
        
        # Problèmes
        if ($line -match "Guru Meditation|Panic|Brownout") {
            $stats.Panics++
            $errorLines += "[PANIC] $line"
        }
        if ($line -match "error|Error|ERROR") {
            $stats.Errors++
            $errorLines += "[ERROR] $line"
        }
        if ($line -match "warning|Warning|WARNING") {
            $stats.Warnings++
        }
        if ($line -match "watchdog|Watchdog|WATCHDOG") {
            $stats.WatchdogResets++
            $errorLines += "[WATCHDOG] $line"
        }
        if ($line -match "heap|memory|Memory|MEMORY" -and $line -match "low|fail|error") {
            $stats.MemoryWarnings++
            $errorLines += "[MEMORY] $line"
        }
        if ($line -match "WiFi.*reconnect|Reconnecting WiFi") {
            $stats.WiFiReconnections++
        }
    }
    
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "STATISTIQUES" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    Write-Host "📊 ENVOI DE DONNÉES:" -ForegroundColor Yellow
    Write-Host "  - Tentatives d'envoi détectées: $($stats.DataSendAttempts)" -ForegroundColor White
    Write-Host "  - Envois réussis: $($stats.DataSendSuccess)" -ForegroundColor Green
    Write-Host "  - Envois échoués: $($stats.DataSendFailures)" -ForegroundColor $(if ($stats.DataSendFailures -gt 0) { "Red" } else { "Green" })
    if ($stats.DataSendAttempts -gt 0) {
        $successRate = [math]::Round(($stats.DataSendSuccess / $stats.DataSendAttempts) * 100, 2)
        Write-Host "  - Taux de réussite: $successRate%" -ForegroundColor $(if ($successRate -ge 95) { "Green" } elseif ($successRate -ge 80) { "Yellow" } else { "Red" })
    }
    Write-Host ""
    
    Write-Host "🌐 WEBSOCKET:" -ForegroundColor Yellow
    Write-Host "  - Connexions: $($stats.WebSocketConnected)" -ForegroundColor White
    Write-Host "  - Déconnexions: $($stats.WebSocketDisconnected)" -ForegroundColor $(if ($stats.WebSocketDisconnected -gt 5) { "Yellow" } else { "White" })
    Write-Host ""
    
    Write-Host "🌍 HTTP:" -ForegroundColor Yellow
    Write-Host "  - Requêtes: $($stats.HTTPRequests)" -ForegroundColor White
    Write-Host "  - Réponses: $($stats.HTTPResponses)" -ForegroundColor White
    Write-Host ""
    
    Write-Host "⚠️  PROBLÈMES:" -ForegroundColor Yellow
    Write-Host "  - Panics/Guru Meditation: $($stats.Panics)" -ForegroundColor $(if ($stats.Panics -gt 0) { "Red" } else { "Green" })
    Write-Host "  - Erreurs: $($stats.Errors)" -ForegroundColor $(if ($stats.Errors -gt 0) { "Yellow" } else { "Green" })
    Write-Host "  - Warnings: $($stats.Warnings)" -ForegroundColor $(if ($stats.Warnings -gt 10) { "Yellow" } else { "White" })
    Write-Host "  - Watchdog resets: $($stats.WatchdogResets)" -ForegroundColor $(if ($stats.WatchdogResets -gt 0) { "Red" } else { "Green" })
    Write-Host "  - Memory warnings: $($stats.MemoryWarnings)" -ForegroundColor $(if ($stats.MemoryWarnings -gt 0) { "Yellow" } else { "Green" })
    Write-Host "  - WiFi reconnections: $($stats.WiFiReconnections)" -ForegroundColor $(if ($stats.WiFiReconnections -gt 3) { "Yellow" } else { "White" })
    Write-Host ""
    
    # Sauvegarder l'analyse détaillée
    $analysisContent = @"
========================================
ANALYSE MONITORING ESP32 - ENVOI DE DONNÉES
========================================
Timestamp: $timestamp
Durée: $duration secondes (6+ minutes)
Fichier log: $logFile

========================================
STATISTIQUES GLOBALES
========================================
Total lignes analysées: $($stats.TotalLines)

📊 ENVOI DE DONNÉES:
  - Tentatives d'envoi détectées: $($stats.DataSendAttempts)
  - Envois réussis: $($stats.DataSendSuccess)
  - Envois échoués: $($stats.DataSendFailures)

🌐 WEBSOCKET:
  - Connexions: $($stats.WebSocketConnected)
  - Déconnexions: $($stats.WebSocketDisconnected)

🌍 HTTP:
  - Requêtes: $($stats.HTTPRequests)
  - Réponses: $($stats.HTTPResponses)

⚠️  PROBLÈMES:
  - Panics/Guru Meditation: $($stats.Panics)
  - Erreurs: $($stats.Errors)
  - Warnings: $($stats.Warnings)
  - Watchdog resets: $($stats.WatchdogResets)
  - Memory warnings: $($stats.MemoryWarnings)
  - WiFi reconnections: $($stats.WiFiReconnections)

========================================
DÉTAILS - ENVOI DE DONNÉES
========================================
$($dataSendLines -join "`n")

========================================
DÉTAILS - STATUT WEBSOCKET
========================================
$($wsStatusLines -join "`n")

========================================
DÉTAILS - REQUÊTES HTTP
========================================
$($httpLines -join "`n")

========================================
DÉTAILS - ERREURS ET PROBLÈMES
========================================
$($errorLines -join "`n")

========================================
FIN DE L'ANALYSE
========================================
"@
    
    $analysisContent | Out-File -FilePath $analysisFile -Encoding UTF8
    Write-Host "Analyse détaillée sauvegardée: $analysisFile" -ForegroundColor Green
    
} else {
    Write-Host "ERREUR: Fichier de log non trouvé: $logFile" -ForegroundColor Red
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MONITORING TERMINÉ" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Fichiers générés:" -ForegroundColor Yellow
Write-Host "  - Log brut: $logFile" -ForegroundColor White
Write-Host "  - Analyse: $analysisFile" -ForegroundColor White
Write-Host ""

