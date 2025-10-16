# Script d'analyse des logs de monitoring v11.51
# Analyse complète des logs de 20 minutes

param(
    [string]$LogFile = "monitor_20min_v11.51_*.log"
)

Write-Host "=== ANALYSE DES LOGS DE MONITORING v11.51 ===" -ForegroundColor Green
Write-Host "Fichier de log: $LogFile" -ForegroundColor Yellow

# Trouver le fichier de log le plus récent
$LatestLog = Get-ChildItem -Path "." -Name $LogFile | Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $LatestLog) {
    Write-Host "ERREUR: Aucun fichier de log trouvé correspondant au pattern $LogFile" -ForegroundColor Red
    exit 1
}

Write-Host "Analyse du fichier: $LatestLog" -ForegroundColor Cyan

# Lire le contenu du log
$LogContent = Get-Content $LatestLog -Raw

# Statistiques générales
$TotalLines = ($LogContent -split "`n").Count
$LogSize = (Get-Item $LatestLog).Length

Write-Host "`n=== STATISTIQUES GÉNÉRALES ===" -ForegroundColor Green
Write-Host "Nombre total de lignes: $TotalLines"
Write-Host "Taille du fichier: $([math]::Round($LogSize/1KB, 2)) KB"

# Analyse des erreurs critiques
Write-Host "`n=== ANALYSE DES ERREURS CRITIQUES ===" -ForegroundColor Red

# Guru Meditation
$GuruMeditation = ($LogContent | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" -AllMatches).Matches.Count
if ($GuruMeditation -gt 0) {
    Write-Host "🔴 GURU MEDITATION/PANIC/BROWNOUT: $GuruMeditation occurrences" -ForegroundColor Red
    $LogContent | Select-String -Pattern "Guru Meditation|Panic|Brownout|Core dump" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Red
    }
} else {
    Write-Host "✅ Aucun Guru Meditation/Panic/Brownout détecté" -ForegroundColor Green
}

# Watchdog timeouts
$WatchdogTimeouts = ($LogContent | Select-String -Pattern "watchdog.*timeout|WDT.*timeout" -AllMatches).Matches.Count
if ($WatchdogTimeouts -gt 0) {
    Write-Host "🟡 WATCHDOG TIMEOUTS: $WatchdogTimeouts occurrences" -ForegroundColor Yellow
    $LogContent | Select-String -Pattern "watchdog.*timeout|WDT.*timeout" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Yellow
    }
} else {
    Write-Host "✅ Aucun timeout watchdog détecté" -ForegroundColor Green
}

# Erreurs WiFi
$WiFiErrors = ($LogContent | Select-String -Pattern "WiFi.*error|WiFi.*fail|WiFi.*timeout" -AllMatches).Matches.Count
if ($WiFiErrors -gt 0) {
    Write-Host "🟡 ERREURS WIFI: $WiFiErrors occurrences" -ForegroundColor Yellow
    $LogContent | Select-String -Pattern "WiFi.*error|WiFi.*fail|WiFi.*timeout" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Yellow
    }
} else {
    Write-Host "✅ Aucune erreur WiFi détectée" -ForegroundColor Green
}

# Erreurs WebSocket
$WebSocketErrors = ($LogContent | Select-String -Pattern "WebSocket.*error|WebSocket.*fail|WebSocket.*timeout" -AllMatches).Matches.Count
if ($WebSocketErrors -gt 0) {
    Write-Host "🟡 ERREURS WEBSOCKET: $WebSocketErrors occurrences" -ForegroundColor Yellow
    $LogContent | Select-String -Pattern "WebSocket.*error|WebSocket.*fail|WebSocket.*timeout" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Yellow
    }
} else {
    Write-Host "✅ Aucune erreur WebSocket détectée" -ForegroundColor Green
}

# Analyse de la mémoire
Write-Host "`n=== ANALYSE DE LA MÉMOIRE ===" -ForegroundColor Blue

# Utilisation mémoire
$MemoryUsage = $LogContent | Select-String -Pattern "heap.*free|memory.*free|RAM.*free" | Select-Object -Last 10
if ($MemoryUsage) {
    Write-Host "📊 Dernières utilisations mémoire:" -ForegroundColor Cyan
    $MemoryUsage | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Cyan
    }
} else {
    Write-Host "⚠️ Aucune information mémoire trouvée" -ForegroundColor Yellow
}

# Analyse des capteurs
Write-Host "`n=== ANALYSE DES CAPTEURS ===" -ForegroundColor Magenta

# Erreurs capteurs
$SensorErrors = ($LogContent | Select-String -Pattern "sensor.*error|sensor.*fail|DHT.*error|DS18B20.*error" -AllMatches).Matches.Count
if ($SensorErrors -gt 0) {
    Write-Host "🟡 ERREURS CAPTEURS: $SensorErrors occurrences" -ForegroundColor Yellow
    $LogContent | Select-String -Pattern "sensor.*error|sensor.*fail|DHT.*error|DS18B20.*error" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Yellow
    }
} else {
    Write-Host "✅ Aucune erreur capteur détectée" -ForegroundColor Green
}

# Valeurs des capteurs (échantillon)
$SensorValues = $LogContent | Select-String -Pattern "temp.*=|humidity.*=|water.*level" | Select-Object -Last 5
if ($SensorValues) {
    Write-Host "📊 Dernières valeurs capteurs:" -ForegroundColor Cyan
    $SensorValues | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Cyan
    }
}

# Analyse des connexions réseau
Write-Host "`n=== ANALYSE RÉSEAU ===" -ForegroundColor DarkCyan

# Connexions HTTP
$HttpRequests = ($LogContent | Select-String -Pattern "HTTP.*POST|HTTP.*GET|HTTP.*200|HTTP.*error" -AllMatches).Matches.Count
Write-Host "📡 Requêtes HTTP: $HttpRequests occurrences"

# Connexions WebSocket
$WebSocketConnections = ($LogContent | Select-String -Pattern "WebSocket.*connect|WebSocket.*disconnect" -AllMatches).Matches.Count
Write-Host "🔌 Connexions WebSocket: $WebSocketConnections occurrences"

# Analyse du démarrage
Write-Host "`n=== ANALYSE DU DÉMARRAGE ===" -ForegroundColor DarkGreen

$StartupInfo = $LogContent | Select-String -Pattern "version|VERSION|FFP3CS4|ESP32.*start" | Select-Object -First 10
if ($StartupInfo) {
    Write-Host "🚀 Informations de démarrage:" -ForegroundColor Green
    $StartupInfo | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Green
    }
}

# Analyse des reboots
$Reboots = ($LogContent | Select-String -Pattern "rst.*reason|reset.*reason|boot.*reason" -AllMatches).Matches.Count
if ($Reboots -gt 0) {
    Write-Host "🔄 REBOOTS DÉTECTÉS: $Reboots occurrences" -ForegroundColor Red
    $LogContent | Select-String -Pattern "rst.*reason|reset.*reason|boot.*reason" | ForEach-Object {
        Write-Host "  - $($_.Line)" -ForegroundColor Red
    }
} else {
    Write-Host "✅ Aucun reboot détecté" -ForegroundColor Green
}

# Résumé final
Write-Host "`n=== RÉSUMÉ FINAL ===" -ForegroundColor White -BackgroundColor DarkBlue

$CriticalIssues = $GuruMeditation + $Reboots
$WarningIssues = $WatchdogTimeouts + $WiFiErrors + $WebSocketErrors + $SensorErrors

if ($CriticalIssues -eq 0) {
    Write-Host "✅ STABILITÉ: Aucun problème critique détecté" -ForegroundColor Green
} else {
    Write-Host "🔴 STABILITÉ: $CriticalIssues problème(s) critique(s) détecté(s)" -ForegroundColor Red
}

if ($WarningIssues -eq 0) {
    Write-Host "✅ FONCTIONNEMENT: Aucun avertissement détecté" -ForegroundColor Green
} else {
    Write-Host "🟡 FONCTIONNEMENT: $WarningIssues avertissement(s) détecté(s)" -ForegroundColor Yellow
}

Write-Host "📊 STATISTIQUES:" -ForegroundColor Cyan
Write-Host "  - Lignes analysées: $TotalLines"
Write-Host "  - Taille log: $([math]::Round($LogSize/1KB, 2)) KB"
Write-Host "  - Requêtes HTTP: $HttpRequests"
Write-Host "  - Connexions WebSocket: $WebSocketConnections"

Write-Host "`n=== FIN DE L'ANALYSE ===" -ForegroundColor Green
Write-Host "Analyse terminée le $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray

