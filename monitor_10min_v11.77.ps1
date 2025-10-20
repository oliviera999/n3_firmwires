# Script de monitoring de 10 minutes pour ESP32 FFP5CS v11.77
# Après effacement complet et flash firmware + SPIFFS
# Monitoring et analyse des logs

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_10min_v11.77_complete_flash_$timestamp.log"
$port = "COM6"
$baudRate = 115200

Write-Host "=== MONITORING ESP32 FFP5CS v11.77 - 10 MINUTES ===" -ForegroundColor Green
Write-Host "Début: $(Get-Date)" -ForegroundColor Green
Write-Host "Port: $port" -ForegroundColor Green
Write-Host "Log file: $logFile" -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Green

# Fonction pour capturer les logs série
function Start-SerialMonitoring {
    param(
        [string]$Port,
        [int]$BaudRate,
        [string]$LogFile,
        [int]$DurationMinutes
    )
    
    $durationSeconds = $DurationMinutes * 60
    $startTime = Get-Date
    
    Write-Host "Démarrage du monitoring série..." -ForegroundColor Yellow
    
    try {
        # Créer le fichier de log
        New-Item -Path $LogFile -ItemType File -Force | Out-Null
        
        # Utiliser pio device monitor pour capturer les logs
        $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $Port, "--baud", $BaudRate, "--filter", "time" -RedirectStandardOutput $LogFile -RedirectStandardError "error_$LogFile" -PassThru -NoNewWindow
        
        Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
        
        # Attendre la durée spécifiée et afficher le progrès
        for ($i = 1; $i -le $durationSeconds; $i++) {
            Start-Sleep -Seconds 1
            
            if ($i % 60 -eq 0) {  # Afficher le progrès chaque minute
                $minutes = $i / 60
                Write-Host "[$minutes/$DurationMinutes min] Monitoring en cours..." -ForegroundColor Cyan
                
                # Vérifier que le processus fonctionne toujours
                if ($process.HasExited) {
                    Write-Host "ATTENTION: Le processus de monitoring s'est arrêté prématurément!" -ForegroundColor Red
                    break
                }
            }
        }
        
        Write-Host "Arrêt du monitoring après $DurationMinutes minutes..." -ForegroundColor Yellow
        
        # Arrêter le processus
        if (!$process.HasExited) {
            $process.Kill()
            $process.WaitForExit(5000)
        }
        
        Write-Host "Monitoring terminé." -ForegroundColor Green
        
    } catch {
        Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Démarrer le monitoring
Start-SerialMonitoring -Port $port -BaudRate $baudRate -LogFile $logFile -DurationMinutes 10

Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Green
Write-Host "Log file: $logFile" -ForegroundColor Green

# Analyse du log
if (Test-Path $logFile) {
    Write-Host "`n=== ANALYSE DU LOG ===" -ForegroundColor Cyan
    
    $logContent = Get-Content $logFile
    
    # Statistiques générales
    $totalLines = $logContent.Count
    Write-Host "Total de lignes loggées: $totalLines" -ForegroundColor White
    
    # Recherche d'erreurs critiques
    $errors = $logContent | Where-Object { $_ -match "ERROR|❌|Panic|Guru|Brownout|Core dump|ASSERT|Fatal" }
    $warnings = $logContent | Where-Object { $_ -match "WARN|⚠️|Warning|Timeout|Failed" }
    $successes = $logContent | Where-Object { $_ -match "✓|✅|Success|OK|Connected" }
    
    Write-Host "`n=== RÉSULTATS DE L'ANALYSE ===" -ForegroundColor Yellow
    
    if ($errors.Count -gt 0) {
        Write-Host "🔴 ERREURS CRITIQUES DÉTECTÉES: $($errors.Count)" -ForegroundColor Red
        $errors | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    } else {
        Write-Host "✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    
    if ($warnings.Count -gt 0) {
        Write-Host "🟡 AVERTISSEMENTS: $($warnings.Count)" -ForegroundColor Yellow
        $warnings | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    } else {
        Write-Host "✅ Aucun avertissement détecté" -ForegroundColor Green
    }
    
    if ($successes.Count -gt 0) {
        Write-Host "✅ ÉVÉNEMENTS POSITIFS: $($successes.Count)" -ForegroundColor Green
    }
    
    # Recherche de patterns spécifiques
    $bootPattern = $logContent | Where-Object { $_ -match "Boot|Starting|Initializing" }
    $wifiPattern = $logContent | Where-Object { $_ -match "WiFi|WIFI" }
    $sensorPattern = $logContent | Where-Object { $_ -match "Sensor|Capteur" }
    
    Write-Host "`n=== STATISTIQUES PAR CATÉGORIE ===" -ForegroundColor Cyan
    Write-Host "Messages de boot: $($bootPattern.Count)"
    Write-Host "Messages WiFi: $($wifiPattern.Count)"
    Write-Host "Messages capteurs: $($sensorPattern.Count)"
    
    # Afficher les dernières lignes du log
    Write-Host "`n=== DERNIÈRES LIGNES DU LOG ===" -ForegroundColor Cyan
    $logContent | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
    
    # Créer un rapport d'analyse
    $reportFile = "RAPPORT_MONITORING_10min_v11.77_$timestamp.md"
    @"
# Rapport de Monitoring ESP32 FFP5CS v11.77 - 10 minutes

**Date:** $(Get-Date)  
**Durée:** 10 minutes  
**Log file:** $logFile  
**Port:** $port  

## Résumé

- **Total lignes loggées:** $totalLines
- **Erreurs critiques:** $($errors.Count)
- **Avertissements:** $($warnings.Count)
- **Événements positifs:** $($successes.Count)

## Détails par catégorie

- **Messages de boot:** $($bootPattern.Count)
- **Messages WiFi:** $($wifiPattern.Count)
- **Messages capteurs:** $($sensorPattern.Count)

## Erreurs critiques détectées

$(if ($errors.Count -gt 0) { $errors | ForEach-Object { "- $_" } } else { "Aucune erreur critique détectée." })

## Avertissements détectés

$(if ($warnings.Count -gt 0) { $warnings | Select-Object -First 10 | ForEach-Object { "- $_" } } else { "Aucun avertissement détecté." })

## Conclusion

$(if ($errors.Count -eq 0) { "✅ **SYSTÈME STABLE** - Aucune erreur critique détectée pendant le monitoring de 10 minutes." } else { "❌ **PROBLÈMES DÉTECTÉS** - $($errors.Count) erreur(s) critique(s) détectée(s)." })
"@ | Out-File -FilePath $reportFile -Encoding UTF8
    
    Write-Host "`n📋 Rapport détaillé créé: $reportFile" -ForegroundColor Green
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile" -ForegroundColor Red
}

Write-Host "`n=== ANALYSE TERMINÉE ===" -ForegroundColor Green




