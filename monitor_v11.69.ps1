# Script de monitoring ESP32 v11.69 - 15 minutes
# Capture les logs et analyse les corrections des commandes distantes

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_v11.69_15min_$timestamp.log"

Write-Host "=== MONITORING ESP32 v11.69 - 15 MINUTES ===" -ForegroundColor Green
Write-Host "Démarrage: $(Get-Date)" -ForegroundColor Yellow
Write-Host "Fichier de log: $logFile" -ForegroundColor Yellow
Write-Host ""

# Fonction pour capturer les logs
function Start-LogCapture {
    param(
        [string]$Port = "COM6",
        [int]$BaudRate = 115200,
        [string]$OutputFile = $logFile
    )
    
    try {
        Write-Host "Connexion au port $Port à $BaudRate baud..." -ForegroundColor Cyan
        
        # Utiliser pio device monitor pour capturer les logs
        $process = Start-Process -FilePath "pio" -ArgumentList @(
            "device", "monitor",
            "--environment", "wroom-test",
            "--port", $Port,
            "--baud", $BaudRate.ToString()
        ) -RedirectStandardOutput $OutputFile -RedirectStandardError "error_$OutputFile" -PassThru -NoNewWindow
        
        Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
        Write-Host "Appuyez sur Ctrl+C pour arrêter le monitoring" -ForegroundColor Yellow
        
        # Attendre 15 minutes (900 secondes)
        $startTime = Get-Date
        $duration = 900 # 15 minutes
        
        while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($duration)) {
            $elapsed = ((Get-Date) - $startTime).TotalSeconds
            $remaining = $duration - $elapsed
            $progress = ($elapsed / $duration) * 100
            
            Write-Progress -Activity "Monitoring ESP32 v11.69" -Status "Temps écoulé: $([math]::Round($elapsed, 0))s / Temps restant: $([math]::Round($remaining, 0))s" -PercentComplete $progress
            
            Start-Sleep -Seconds 5
            
            # Vérifier si le processus est toujours actif
            if ($process.HasExited) {
                Write-Host "Le processus de monitoring s'est arrêté inopinément" -ForegroundColor Red
                break
            }
        }
        
        Write-Progress -Activity "Monitoring ESP32 v11.69" -Completed
        Write-Host ""
        Write-Host "=== FIN DU MONITORING ===" -ForegroundColor Green
        Write-Host "Durée totale: $(((Get-Date) - $startTime).TotalMinutes) minutes" -ForegroundColor Yellow
        
        # Arrêter le processus
        if (-not $process.HasExited) {
            $process.Kill()
            Write-Host "Processus de monitoring arrêté" -ForegroundColor Yellow
        }
        
        # Analyser les logs capturés
        if (Test-Path $OutputFile) {
            $logSize = (Get-Item $OutputFile).Length
            Write-Host "Log capturé: $logSize bytes" -ForegroundColor Green
            Analyze-Logs -LogFile $OutputFile
        } else {
            Write-Host "Aucun log capturé" -ForegroundColor Red
        }
        
    } catch {
        Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Fonction d'analyse des logs
function Analyze-Logs {
    param([string]$LogFile)
    
    Write-Host ""
    Write-Host "=== ANALYSE DES LOGS v11.69 ===" -ForegroundColor Green
    
    if (-not (Test-Path $LogFile)) {
        Write-Host "Fichier de log non trouvé: $LogFile" -ForegroundColor Red
        return
    }
    
    $logs = Get-Content $LogFile -ErrorAction SilentlyContinue
    
    if ($logs.Count -eq 0) {
        Write-Host "Aucun contenu dans le fichier de log" -ForegroundColor Red
        return
    }
    
    Write-Host "Lignes de log analysées: $($logs.Count)" -ForegroundColor Yellow
    Write-Host ""
    
    # Analyse des corrections v11.69
    Write-Host "🔍 RECHERCHE DES CORRECTIONS v11.69:" -ForegroundColor Cyan
    
    # 1. Vérifier la désactivation de handleRemoteActuators
    $handleRemoteDisabled = $logs | Where-Object { $_ -match "handleRemoteActuators DÉSACTIVÉ" }
    if ($handleRemoteDisabled) {
        Write-Host "✅ handleRemoteActuators correctement désactivé" -ForegroundColor Green
        Write-Host "   Messages trouvés: $($handleRemoteDisabled.Count)" -ForegroundColor Gray
    } else {
        Write-Host "❌ handleRemoteActuators pas trouvé désactivé" -ForegroundColor Red
    }
    
    # 2. Vérifier les améliorations de logs de fin de cycle pompe
    $pumpLogs = $logs | Where-Object { $_ -match "Fin cycle.*notifiée au serveur" }
    if ($pumpLogs) {
        Write-Host "✅ Logs améliorés de fin de cycle pompe détectés" -ForegroundColor Green
        Write-Host "   Messages trouvés: $($pumpLogs.Count)" -ForegroundColor Gray
    } else {
        Write-Host "❌ Aucun log amélioré de fin de cycle pompe" -ForegroundColor Red
    }
    
    # 3. Vérifier les commandes GPIO
    $gpioCommands = $logs | Where-Object { $_ -match "GPIOParser.*GPIO.*:" }
    if ($gpioCommands) {
        Write-Host "✅ Commandes GPIO via GPIOParser détectées" -ForegroundColor Green
        Write-Host "   Messages trouvés: $($gpioCommands.Count)" -ForegroundColor Gray
    } else {
        Write-Host "❌ Aucune commande GPIO via GPIOParser" -ForegroundColor Red
    }
    
    Write-Host ""
    Write-Host "🔍 RECHERCHE D'ERREURS CRITIQUES:" -ForegroundColor Cyan
    
    # Erreurs critiques
    $criticalErrors = $logs | Where-Object { 
        $_ -match "Guru Meditation|Panic|Brownout|Core dump|Exception|Fatal|Error.*critical" 
    }
    if ($criticalErrors) {
        Write-Host "❌ ERREURS CRITIQUES DÉTECTÉES:" -ForegroundColor Red
        $criticalErrors | ForEach-Object { Write-Host "   $_" -ForegroundColor Red }
    } else {
        Write-Host "✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    
    # Warnings
    $warnings = $logs | Where-Object { $_ -match "WARNING|Warning|⚠️" }
    if ($warnings) {
        Write-Host "⚠️ WARNINGS DÉTECTÉS: $($warnings.Count)" -ForegroundColor Yellow
        $warnings | Select-Object -First 5 | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
    } else {
        Write-Host "✅ Aucun warning détecté" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "🔍 ANALYSE DE STABILITÉ:" -ForegroundColor Cyan
    
    # Uptime
    $uptime = $logs | Where-Object { $_ -match "uptime.*sec|Uptime.*:" }
    if ($uptime) {
        $latestUptime = $uptime | Select-Object -Last 1
        Write-Host "📊 Dernier uptime: $latestUptime" -ForegroundColor Green
    }
    
    # Mémoire
    $memory = $logs | Where-Object { $_ -match "Free heap.*bytes|heap.*bytes" }
    if ($memory) {
        $latestMemory = $memory | Select-Object -Last 1
        Write-Host "📊 Dernière mémoire: $latestMemory" -ForegroundColor Green
    }
    
    # WiFi
    $wifi = $logs | Where-Object { $_ -match "WiFi.*connected|RSSI.*dBm" }
    if ($wifi) {
        $latestWifi = $wifi | Select-Object -Last 1
        Write-Host "📊 Dernier état WiFi: $latestWifi" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "📋 RÉSUMÉ DE L'ANALYSE:" -ForegroundColor Green
    Write-Host "   - Fichier: $LogFile" -ForegroundColor Gray
    Write-Host "   - Lignes analysées: $($logs.Count)" -ForegroundColor Gray
    Write-Host "   - Erreurs critiques: $($criticalErrors.Count)" -ForegroundColor Gray
    Write-Host "   - Warnings: $($warnings.Count)" -ForegroundColor Gray
    Write-Host "   - Commandes GPIO: $($gpioCommands.Count)" -ForegroundColor Gray
}

# Démarrer le monitoring
Start-LogCapture
