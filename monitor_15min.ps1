# Script de monitoring ESP32 - 15 minutes (900 secondes)
# Version: v11.09 - Analyse complète selon priorités du projet
# Focus: Stabilité système, mémoire, réseau, capteurs

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_15min_$timestamp.log"
$analysisFile = "monitor_15min_analysis_$timestamp.log"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MONITORING ESP32 - 15 MINUTES COMPLET" -ForegroundColor Cyan
Write-Host "Version: v11.09" -ForegroundColor Cyan
Write-Host "Duration: 900 secondes (15 minutes)" -ForegroundColor Cyan
Write-Host "Timestamp: $timestamp" -ForegroundColor Cyan
Write-Host "Log file: $logFile" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Durée du monitoring: 900 secondes (15 minutes)
$duration = 900
$endTime = (Get-Date).AddSeconds($duration)

Write-Host "Démarrage du monitoring pour $duration secondes..." -ForegroundColor Yellow
Write-Host "Fin prévue: $($endTime.ToString('HH:mm:ss'))" -ForegroundColor Yellow
Write-Host ""

# Compteurs pour l'analyse selon les priorités du projet
$stats = @{
    TotalLines = 0
    StartTime = Get-Date
    EndTime = $null
    
    # P1 - CRITIQUE
    GuruMeditation = 0
    Panics = 0
    Brownouts = 0
    CoreDumps = 0
    SystemCrashes = 0
    
    # P2 - IMPORTANT
    WatchdogWarnings = 0
    WatchdogResets = 0
    WiFiTimeouts = 0
    WebSocketTimeouts = 0
    WiFiReconnections = 0
    WiFiDisconnections = 0
    
    # P3 - MÉMOIRE
    HeapReadings = @()
    HeapMin = [int]::MaxValue
    HeapMax = 0
    HeapAvg = 0
    MemoryWarnings = 0
    MemoryCritical = 0
    LowMemoryEvents = 0
    
    # RÉSEAU
    WiFiRSSI = @()
    WiFiQuality = @()
    WebSocketConnected = 0
    WebSocketDisconnected = 0
    HTTPRequests = 0
    HTTPSuccess = 0
    HTTPFailures = 0
    HTTPTimeouts = 0
    
    # SERVEUR DISTANT
    PostDataAttempts = 0
    PostDataSuccess = 0
    PostDataFailures = 0
    RemoteStatePolls = 0
    RemoteStateSuccess = 0
    RemoteStateFailures = 0
    
    # CAPTEURS (SECONDAIRE)
    DHT22Readings = 0
    DHT22Success = 0
    DHT22Failures = 0
    DHT22FilteringFailed = 0
    DS18B20Readings = 0
    DS18B20Success = 0
    DS18B20Failures = 0
    HCSR04Readings = 0
    HCSR04Success = 0
    HCSR04Failures = 0
    
    # PERFORMANCE
    BootTime = 0
    WiFiConnectTime = 0
    NTPSyncTime = 0
    NTPDriftPPM = 0
    SensorReadTime = 0
    
    # MODEM SLEEP
    ModemSleepEnabled = $false
    LightSleepDetected = 0
    DTIMConfigured = $false
    
    # GÉNÉRAUX
    Errors = 0
    Warnings = 0
    InfoMessages = 0
}

# Collections pour analyse détaillée
$criticalLines = @()
$importantLines = @()
$memoryLines = @()
$networkLines = @()
$sensorLines = @()
$errorLines = @()
$warningLines = @()

try {
    # Lancer PlatformIO monitor
    $process = Start-Process -FilePath "pio" -ArgumentList "device monitor --environment wroom_test --filter=direct --baud 115200" -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.err"
    
    Write-Host "Monitoring en cours..." -ForegroundColor Green
    Write-Host "Progression affichée toutes les 30 secondes" -ForegroundColor Gray
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
            $elapsedMin = [math]::Floor($elapsed / 60)
            $elapsedSec = $elapsed % 60
            $remainingMin = [math]::Floor($remaining / 60)
            $remainingSec = [int]($remaining % 60)
            Write-Host "[$progress%] Écoulé: $elapsedMin min $elapsedSec s | Reste: $remainingMin min $remainingSec s" -ForegroundColor Cyan
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

$stats.EndTime = Get-Date

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
    Write-Host "Analyse en cours..." -ForegroundColor Yellow
    Write-Host ""
    
    foreach ($line in $lines) {
        # ===========================
        # P1 - ERREURS CRITIQUES
        # ===========================
        if ($line -match "Guru Meditation") {
            $stats.GuruMeditation++
            $stats.Panics++
            $criticalLines += "[GURU MEDITATION] $line"
        }
        if ($line -match "Panic|panic") {
            $stats.Panics++
            $criticalLines += "[PANIC] $line"
        }
        if ($line -match "Brownout|brownout") {
            $stats.Brownouts++
            $criticalLines += "[BROWNOUT] $line"
        }
        if ($line -match "Core.*dump|core dump") {
            $stats.CoreDumps++
            $criticalLines += "[CORE DUMP] $line"
        }
        if ($line -match "rst:|reset|Reset|RESET" -and $line -notmatch "watchdog") {
            $stats.SystemCrashes++
            $criticalLines += "[SYSTEM RESET] $line"
        }
        
        # ===========================
        # P2 - PROBLÈMES IMPORTANTS
        # ===========================
        if ($line -match "watchdog|Watchdog|WATCHDOG") {
            if ($line -match "warning|Warning|WARNING") {
                $stats.WatchdogWarnings++
                $importantLines += "[WATCHDOG WARNING] $line"
            } elseif ($line -match "reset|Reset|timeout") {
                $stats.WatchdogResets++
                $importantLines += "[WATCHDOG RESET] $line"
            }
        }
        
        if ($line -match "WiFi.*timeout|wifi.*timeout") {
            $stats.WiFiTimeouts++
            $importantLines += "[WIFI TIMEOUT] $line"
        }
        if ($line -match "WebSocket.*timeout|WS.*timeout|ws.*timeout") {
            $stats.WebSocketTimeouts++
            $importantLines += "[WEBSOCKET TIMEOUT] $line"
        }
        if ($line -match "WiFi.*reconnect|wifi.*reconnect|Reconnecting WiFi") {
            $stats.WiFiReconnections++
            $importantLines += "[WIFI RECONNECT] $line"
        }
        if ($line -match "WiFi.*disconnect|wifi.*disconnect" -and $line -notmatch "reconnect") {
            $stats.WiFiDisconnections++
            $importantLines += "[WIFI DISCONNECT] $line"
        }
        
        # ===========================
        # P3 - MÉMOIRE
        # ===========================
        if ($line -match "heap|Heap|HEAP") {
            # Extraire les valeurs de heap
            if ($line -match "(\d+)\s*KB.*free|free.*heap.*?(\d+)") {
                $heapKB = [int]$matches[1]
                $stats.HeapReadings += $heapKB
                if ($heapKB -lt $stats.HeapMin) { $stats.HeapMin = $heapKB }
                if ($heapKB -gt $stats.HeapMax) { $stats.HeapMax = $heapKB }
            }
            
            if ($line -match "low.*memory|memory.*low|heap.*low") {
                $stats.MemoryWarnings++
                $stats.LowMemoryEvents++
                $memoryLines += "[LOW MEMORY] $line"
            }
            if ($line -match "critical|CRITICAL|out of memory|OOM") {
                $stats.MemoryCritical++
                $memoryLines += "[MEMORY CRITICAL] $line"
            }
        }
        
        # ===========================
        # RÉSEAU
        # ===========================
        if ($line -match "RSSI.*?(-?\d+)") {
            $rssi = [int]$matches[1]
            $stats.WiFiRSSI += $rssi
            
            # Calculer qualité WiFi
            if ($rssi -ge -50) { $quality = 100 }
            elseif ($rssi -ge -60) { $quality = 80 }
            elseif ($rssi -ge -70) { $quality = 60 }
            elseif ($rssi -ge -80) { $quality = 40 }
            else { $quality = 20 }
            $stats.WiFiQuality += $quality
        }
        
        if ($line -match "WS.*connect|WebSocket.*connect" -and $line -notmatch "disconnect") {
            $stats.WebSocketConnected++
            $networkLines += "[WS CONNECT] $line"
        }
        if ($line -match "WS.*disconnect|WebSocket.*disconnect") {
            $stats.WebSocketDisconnected++
            $networkLines += "[WS DISCONNECT] $line"
        }
        
        if ($line -match "HTTP|GET |POST |PUT |DELETE ") {
            $stats.HTTPRequests++
            if ($line -match "200|201|204") {
                $stats.HTTPSuccess++
            } elseif ($line -match "400|404|500|502|503|timeout") {
                $stats.HTTPFailures++
            }
        }
        
        # ===========================
        # SERVEUR DISTANT
        # ===========================
        if ($line -match "POST.*data|post.*data|sendData|Sending data") {
            $stats.PostDataAttempts++
            if ($line -match "success|Success|200|sent") {
                $stats.PostDataSuccess++
            } elseif ($line -match "fail|error|timeout") {
                $stats.PostDataFailures++
            }
        }
        
        if ($line -match "pollRemoteState|poll.*remote|Remote state") {
            $stats.RemoteStatePolls++
            if ($line -match "success|Success|200") {
                $stats.RemoteStateSuccess++
            } elseif ($line -match "fail|error|timeout") {
                $stats.RemoteStateFailures++
            }
        }
        
        # ===========================
        # CAPTEURS (SECONDAIRE)
        # ===========================
        if ($line -match "DHT22|DHT|dht") {
            $stats.DHT22Readings++
            if ($line -match "success|Success|read.*OK|valid") {
                $stats.DHT22Success++
            } elseif ($line -match "fail|error|invalid|timeout") {
                $stats.DHT22Failures++
                $sensorLines += "[DHT22 ERROR] $line"
            }
            if ($line -match "filter.*fail|filtering.*fail") {
                $stats.DHT22FilteringFailed++
                $sensorLines += "[DHT22 FILTER] $line"
            }
        }
        
        if ($line -match "DS18B20|ds18b20|Water temp|water temp") {
            $stats.DS18B20Readings++
            if ($line -match "success|Success|°C|valid") {
                $stats.DS18B20Success++
            } elseif ($line -match "fail|error|invalid|timeout") {
                $stats.DS18B20Failures++
                $sensorLines += "[DS18B20 ERROR] $line"
            }
        }
        
        if ($line -match "HC-SR04|ultrasonic|Ultrasonic|level.*cm") {
            $stats.HCSR04Readings++
            if ($line -match "success|Success|cm|valid") {
                $stats.HCSR04Success++
            } elseif ($line -match "fail|error|invalid|timeout") {
                $stats.HCSR04Failures++
                $sensorLines += "[HC-SR04 ERROR] $line"
            }
        }
        
        # ===========================
        # PERFORMANCE
        # ===========================
        if ($line -match "Boot.*time.*?(\d+)") {
            $stats.BootTime = [int]$matches[1]
        }
        if ($line -match "WiFi.*connected.*?(\d+)\s*ms") {
            $stats.WiFiConnectTime = [int]$matches[1]
        }
        if ($line -match "NTP.*sync.*?(\d+)") {
            $stats.NTPSyncTime = [int]$matches[1]
        }
        if ($line -match "drift.*?(\d+\.?\d*)\s*PPM") {
            $stats.NTPDriftPPM = [float]$matches[1]
        }
        if ($line -match "Sensor.*read.*?(\d+)\s*ms") {
            $stats.SensorReadTime = [int]$matches[1]
        }
        
        # ===========================
        # MODEM SLEEP
        # ===========================
        if ($line -match "Modem.*sleep.*enable|WIFI_PS_MIN_MODEM") {
            $stats.ModemSleepEnabled = $true
        }
        if ($line -match "Light.*sleep|light.*sleep") {
            $stats.LightSleepDetected++
        }
        if ($line -match "DTIM.*config|dtim.*config") {
            $stats.DTIMConfigured = $true
        }
        
        # ===========================
        # GÉNÉRAUX
        # ===========================
        if ($line -match "error|Error|ERROR") {
            $stats.Errors++
            $errorLines += $line
        }
        if ($line -match "warning|Warning|WARNING") {
            $stats.Warnings++
            $warningLines += $line
        }
        if ($line -match "info|Info|INFO") {
            $stats.InfoMessages++
        }
    }
    
    # Calculer moyennes
    if ($stats.HeapReadings.Count -gt 0) {
        $stats.HeapAvg = [math]::Round(($stats.HeapReadings | Measure-Object -Average).Average, 2)
    }
    if ($stats.HeapMin -eq [int]::MaxValue) { $stats.HeapMin = 0 }
    
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "STATISTIQUES PAR PRIORITÉ" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    # P1 - CRITIQUE
    Write-Host "🔴 P1 - ERREURS CRITIQUES:" -ForegroundColor Red
    $p1Total = $stats.GuruMeditation + $stats.Panics + $stats.Brownouts + $stats.CoreDumps + $stats.SystemCrashes
    if ($p1Total -eq 0) {
        Write-Host "  ✅ AUCUNE erreur critique détectée" -ForegroundColor Green
    } else {
        Write-Host "  ❌ $p1Total ERREUR(S) CRITIQUE(S) DÉTECTÉE(S)" -ForegroundColor Red
        Write-Host "    - Guru Meditation: $($stats.GuruMeditation)" -ForegroundColor Red
        Write-Host "    - Panics: $($stats.Panics)" -ForegroundColor Red
        Write-Host "    - Brownouts: $($stats.Brownouts)" -ForegroundColor Red
        Write-Host "    - Core dumps: $($stats.CoreDumps)" -ForegroundColor Red
        Write-Host "    - System crashes: $($stats.SystemCrashes)" -ForegroundColor Red
    }
    Write-Host ""
    
    # P2 - IMPORTANT
    Write-Host "🟡 P2 - PROBLÈMES IMPORTANTS:" -ForegroundColor Yellow
    $p2Total = $stats.WatchdogWarnings + $stats.WatchdogResets + $stats.WiFiTimeouts + $stats.WebSocketTimeouts
    if ($p2Total -eq 0) {
        Write-Host "  ✅ Aucun problème important" -ForegroundColor Green
    } else {
        Write-Host "  ⚠️  $p2Total problème(s) important(s) détecté(s)" -ForegroundColor Yellow
        Write-Host "    - Watchdog warnings: $($stats.WatchdogWarnings)" -ForegroundColor $(if ($stats.WatchdogWarnings -gt 0) { "Yellow" } else { "Green" })
        Write-Host "    - Watchdog resets: $($stats.WatchdogResets)" -ForegroundColor $(if ($stats.WatchdogResets -gt 0) { "Red" } else { "Green" })
        Write-Host "    - WiFi timeouts: $($stats.WiFiTimeouts)" -ForegroundColor $(if ($stats.WiFiTimeouts -gt 0) { "Yellow" } else { "Green" })
        Write-Host "    - WebSocket timeouts: $($stats.WebSocketTimeouts)" -ForegroundColor $(if ($stats.WebSocketTimeouts -gt 0) { "Yellow" } else { "Green" })
    }
    Write-Host "  Réseau:" -ForegroundColor Cyan
    Write-Host "    - WiFi reconnexions: $($stats.WiFiReconnections)" -ForegroundColor $(if ($stats.WiFiReconnections -gt 5) { "Yellow" } else { "Green" })
    Write-Host "    - WiFi déconnexions: $($stats.WiFiDisconnections)" -ForegroundColor $(if ($stats.WiFiDisconnections -gt 3) { "Yellow" } else { "Green" })
    Write-Host ""
    
    # P3 - MÉMOIRE
    Write-Host "🟢 P3 - MÉTRIQUES MÉMOIRE:" -ForegroundColor Green
    Write-Host "  Heap (RAM libre):" -ForegroundColor Cyan
    Write-Host "    - Minimum: $($stats.HeapMin) KB" -ForegroundColor $(if ($stats.HeapMin -lt 50) { "Red" } elseif ($stats.HeapMin -lt 80) { "Yellow" } else { "Green" })
    Write-Host "    - Maximum: $($stats.HeapMax) KB" -ForegroundColor White
    Write-Host "    - Moyenne: $($stats.HeapAvg) KB" -ForegroundColor White
    Write-Host "    - Lectures: $($stats.HeapReadings.Count)" -ForegroundColor White
    Write-Host "  Alertes mémoire:" -ForegroundColor Cyan
    Write-Host "    - Warnings: $($stats.MemoryWarnings)" -ForegroundColor $(if ($stats.MemoryWarnings -gt 0) { "Yellow" } else { "Green" })
    Write-Host "    - Critiques: $($stats.MemoryCritical)" -ForegroundColor $(if ($stats.MemoryCritical -gt 0) { "Red" } else { "Green" })
    Write-Host "    - Low memory events: $($stats.LowMemoryEvents)" -ForegroundColor $(if ($stats.LowMemoryEvents -gt 0) { "Yellow" } else { "Green" })
    Write-Host ""
    
    # RÉSEAU
    Write-Host "🌐 RÉSEAU ET CONNECTIVITÉ:" -ForegroundColor Cyan
    Write-Host "  WiFi:" -ForegroundColor Cyan
    if ($stats.WiFiRSSI.Count -gt 0) {
        $avgRSSI = [math]::Round(($stats.WiFiRSSI | Measure-Object -Average).Average, 1)
        $avgQuality = [math]::Round(($stats.WiFiQuality | Measure-Object -Average).Average, 0)
        Write-Host "    - RSSI moyen: $avgRSSI dBm" -ForegroundColor $(if ($avgRSSI -ge -60) { "Green" } elseif ($avgRSSI -ge -70) { "Yellow" } else { "Red" })
        Write-Host "    - Qualité moyenne: $avgQuality%" -ForegroundColor $(if ($avgQuality -ge 70) { "Green" } elseif ($avgQuality -ge 50) { "Yellow" } else { "Red" })
    } else {
        Write-Host "    - Pas de mesures RSSI capturées" -ForegroundColor Gray
    }
    Write-Host "  WebSocket:" -ForegroundColor Cyan
    Write-Host "    - Connexions: $($stats.WebSocketConnected)" -ForegroundColor White
    Write-Host "    - Déconnexions: $($stats.WebSocketDisconnected)" -ForegroundColor $(if ($stats.WebSocketDisconnected -gt 10) { "Yellow" } else { "White" })
    Write-Host "  HTTP:" -ForegroundColor Cyan
    Write-Host "    - Requêtes totales: $($stats.HTTPRequests)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.HTTPSuccess)" -ForegroundColor Green
    Write-Host "    - Échecs: $($stats.HTTPFailures)" -ForegroundColor $(if ($stats.HTTPFailures -gt 0) { "Red" } else { "Green" })
    Write-Host ""
    
    # SERVEUR DISTANT
    Write-Host "☁️  SERVEUR DISTANT:" -ForegroundColor Cyan
    Write-Host "  POST Data:" -ForegroundColor Cyan
    Write-Host "    - Tentatives: $($stats.PostDataAttempts)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.PostDataSuccess)" -ForegroundColor $(if ($stats.PostDataSuccess -gt 0) { "Green" } else { "Red" })
    Write-Host "    - Échecs: $($stats.PostDataFailures)" -ForegroundColor $(if ($stats.PostDataFailures -gt 0) { "Red" } else { "Green" })
    if ($stats.PostDataAttempts -gt 0) {
        $successRate = [math]::Round(($stats.PostDataSuccess / $stats.PostDataAttempts) * 100, 1)
        Write-Host "    - Taux réussite: $successRate%" -ForegroundColor $(if ($successRate -ge 90) { "Green" } elseif ($successRate -ge 50) { "Yellow" } else { "Red" })
    }
    Write-Host "  Remote State:" -ForegroundColor Cyan
    Write-Host "    - Polls: $($stats.RemoteStatePolls)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.RemoteStateSuccess)" -ForegroundColor $(if ($stats.RemoteStateSuccess -gt 0) { "Green" } else { "Red" })
    Write-Host "    - Échecs: $($stats.RemoteStateFailures)" -ForegroundColor $(if ($stats.RemoteStateFailures -gt 0) { "Red" } else { "Green" })
    Write-Host ""
    
    # CAPTEURS (SECONDAIRE)
    Write-Host "⚪ CAPTEURS (SECONDAIRE):" -ForegroundColor White
    Write-Host "  DHT22 (Air):" -ForegroundColor Cyan
    Write-Host "    - Lectures: $($stats.DHT22Readings)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.DHT22Success)" -ForegroundColor $(if ($stats.DHT22Success -gt 0) { "Green" } else { "Yellow" })
    Write-Host "    - Échecs: $($stats.DHT22Failures)" -ForegroundColor $(if ($stats.DHT22Failures -gt 0) { "Yellow" } else { "Green" })
    Write-Host "    - Filtrage échoué: $($stats.DHT22FilteringFailed)" -ForegroundColor $(if ($stats.DHT22FilteringFailed -gt 0) { "Yellow" } else { "Green" })
    Write-Host "  DS18B20 (Eau):" -ForegroundColor Cyan
    Write-Host "    - Lectures: $($stats.DS18B20Readings)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.DS18B20Success)" -ForegroundColor $(if ($stats.DS18B20Success -gt 0) { "Green" } else { "Yellow" })
    Write-Host "    - Échecs: $($stats.DS18B20Failures)" -ForegroundColor $(if ($stats.DS18B20Failures -gt 0) { "Yellow" } else { "Green" })
    Write-Host "  HC-SR04 (Niveaux):" -ForegroundColor Cyan
    Write-Host "    - Lectures: $($stats.HCSR04Readings)" -ForegroundColor White
    Write-Host "    - Succès: $($stats.HCSR04Success)" -ForegroundColor $(if ($stats.HCSR04Success -gt 0) { "Green" } else { "Yellow" })
    Write-Host "    - Échecs: $($stats.HCSR04Failures)" -ForegroundColor $(if ($stats.HCSR04Failures -gt 0) { "Yellow" } else { "Green" })
    Write-Host ""
    
    # PERFORMANCE
    Write-Host "⚡ PERFORMANCE:" -ForegroundColor Cyan
    if ($stats.BootTime -gt 0) {
        Write-Host "  - Boot time: $($stats.BootTime) ms" -ForegroundColor White
    }
    if ($stats.WiFiConnectTime -gt 0) {
        Write-Host "  - WiFi connect: $($stats.WiFiConnectTime) ms" -ForegroundColor White
    }
    if ($stats.NTPSyncTime -gt 0) {
        Write-Host "  - NTP sync: $($stats.NTPSyncTime) ms" -ForegroundColor White
    }
    if ($stats.NTPDriftPPM -gt 0) {
        Write-Host "  - NTP drift: $($stats.NTPDriftPPM) PPM" -ForegroundColor $(if ($stats.NTPDriftPPM -lt 100) { "Green" } else { "Yellow" })
    }
    if ($stats.SensorReadTime -gt 0) {
        Write-Host "  - Sensor read: $($stats.SensorReadTime) ms" -ForegroundColor White
    }
    Write-Host ""
    
    # MODEM SLEEP
    Write-Host "💤 MODEM SLEEP:" -ForegroundColor Cyan
    Write-Host "  - Modem sleep activé: $(if ($stats.ModemSleepEnabled) { '✅ Oui' } else { '❌ Non' })" -ForegroundColor $(if ($stats.ModemSleepEnabled) { "Green" } else { "Yellow" })
    Write-Host "  - DTIM configuré: $(if ($stats.DTIMConfigured) { '✅ Oui' } else { '❌ Non' })" -ForegroundColor $(if ($stats.DTIMConfigured) { "Green" } else { "Yellow" })
    Write-Host "  - Light sleep détecté: $($stats.LightSleepDetected) fois" -ForegroundColor White
    Write-Host ""
    
    # GÉNÉRAUX
    Write-Host "📊 STATISTIQUES GÉNÉRALES:" -ForegroundColor Cyan
    Write-Host "  - Total lignes: $($stats.TotalLines)" -ForegroundColor White
    Write-Host "  - Erreurs: $($stats.Errors)" -ForegroundColor $(if ($stats.Errors -gt 20) { "Red" } elseif ($stats.Errors -gt 10) { "Yellow" } else { "Green" })
    Write-Host "  - Warnings: $($stats.Warnings)" -ForegroundColor $(if ($stats.Warnings -gt 50) { "Yellow" } else { "White" })
    Write-Host "  - Messages info: $($stats.InfoMessages)" -ForegroundColor White
    Write-Host ""
    
    # Sauvegarder l'analyse détaillée
    $analysisContent = @"
========================================
ANALYSE MONITORING ESP32 - 15 MINUTES
========================================
Version: v11.09
Timestamp début: $($stats.StartTime.ToString('yyyy-MM-dd HH:mm:ss'))
Timestamp fin: $($stats.EndTime.ToString('yyyy-MM-dd HH:mm:ss'))
Durée: 900 secondes (15 minutes)
Fichier log: $logFile

========================================
RÉSUMÉ EXÉCUTIF
========================================
Total lignes analysées: $($stats.TotalLines)

🔴 P1 - ERREURS CRITIQUES: $p1Total
  - Guru Meditation: $($stats.GuruMeditation)
  - Panics: $($stats.Panics)
  - Brownouts: $($stats.Brownouts)
  - Core dumps: $($stats.CoreDumps)
  - System crashes: $($stats.SystemCrashes)

🟡 P2 - PROBLÈMES IMPORTANTS: $p2Total
  - Watchdog warnings: $($stats.WatchdogWarnings)
  - Watchdog resets: $($stats.WatchdogResets)
  - WiFi timeouts: $($stats.WiFiTimeouts)
  - WebSocket timeouts: $($stats.WebSocketTimeouts)
  - WiFi reconnexions: $($stats.WiFiReconnections)
  - WiFi déconnexions: $($stats.WiFiDisconnections)

🟢 P3 - MÉTRIQUES MÉMOIRE:
  Heap: Min=$($stats.HeapMin) KB, Max=$($stats.HeapMax) KB, Avg=$($stats.HeapAvg) KB
  Warnings: $($stats.MemoryWarnings), Critiques: $($stats.MemoryCritical)

🌐 RÉSEAU:
  WiFi: RSSI moyen=$(if ($stats.WiFiRSSI.Count -gt 0) { [math]::Round(($stats.WiFiRSSI | Measure-Object -Average).Average, 1) } else { 'N/A' }) dBm
  WebSocket: $($stats.WebSocketConnected) connexions, $($stats.WebSocketDisconnected) déconnexions
  HTTP: $($stats.HTTPRequests) requêtes, $($stats.HTTPSuccess) succès, $($stats.HTTPFailures) échecs

☁️  SERVEUR DISTANT:
  POST Data: $($stats.PostDataAttempts) tentatives, $($stats.PostDataSuccess) succès, $($stats.PostDataFailures) échecs
  Remote State: $($stats.RemoteStatePolls) polls, $($stats.RemoteStateSuccess) succès, $($stats.RemoteStateFailures) échecs

⚪ CAPTEURS:
  DHT22: $($stats.DHT22Readings) lectures, $($stats.DHT22Success) succès, $($stats.DHT22Failures) échecs
  DS18B20: $($stats.DS18B20Readings) lectures, $($stats.DS18B20Success) succès, $($stats.DS18B20Failures) échecs
  HC-SR04: $($stats.HCSR04Readings) lectures, $($stats.HCSR04Success) succès, $($stats.HCSR04Failures) échecs

⚡ PERFORMANCE:
  Boot: $($stats.BootTime) ms
  WiFi connect: $($stats.WiFiConnectTime) ms
  NTP drift: $($stats.NTPDriftPPM) PPM
  Sensor read: $($stats.SensorReadTime) ms

💤 MODEM SLEEP:
  Activé: $(if ($stats.ModemSleepEnabled) { 'Oui' } else { 'Non' })
  DTIM: $(if ($stats.DTIMConfigured) { 'Oui' } else { 'Non' })
  Light sleep: $($stats.LightSleepDetected) détections

========================================
DÉTAILS - P1 ERREURS CRITIQUES
========================================
$($criticalLines -join "`n")

========================================
DÉTAILS - P2 PROBLÈMES IMPORTANTS
========================================
$($importantLines -join "`n")

========================================
DÉTAILS - P3 MÉMOIRE
========================================
$($memoryLines -join "`n")

========================================
DÉTAILS - RÉSEAU
========================================
$($networkLines -join "`n")

========================================
DÉTAILS - CAPTEURS
========================================
$($sensorLines -join "`n")

========================================
DÉTAILS - ERREURS GÉNÉRALES
========================================
$($errorLines | Select-Object -First 50 -join "`n")

========================================
DÉTAILS - WARNINGS GÉNÉRAUX
========================================
$($warningLines | Select-Object -First 50 -join "`n")

========================================
FIN DE L'ANALYSE
========================================
"@
    
    $analysisContent | Out-File -FilePath $analysisFile -Encoding UTF8
    Write-Host "Analyse détaillée sauvegardée: $analysisFile" -ForegroundColor Green
    
    # Retourner les statistiques pour le rapport
    return $stats
    
} else {
    Write-Host "ERREUR: Fichier de log non trouvé: $logFile" -ForegroundColor Red
    return $null
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
Write-Host "Prochaine étape: Génération du rapport markdown complet" -ForegroundColor Cyan
Write-Host ""

