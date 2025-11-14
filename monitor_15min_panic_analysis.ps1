# Script de monitoring de 15 minutes pour analyser les crashes PANIC ESP32 FFP5CS
# Capture les logs et analyse les causes des crashes

param(
    [string]$Port = "COM6",
    [int]$DurationMinutes = 15,
    [int]$BaudRate = 115200
)

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_15min_panic_$timestamp.log"
$analysisFile = "monitor_15min_panic_analysis_$timestamp.txt"

Write-Host "=== MONITORING PANIC ANALYSIS - 15 MINUTES ===" -ForegroundColor Green
Write-Host "Début: $(Get-Date)" -ForegroundColor Green
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Durée: $DurationMinutes minutes" -ForegroundColor Yellow
Write-Host "Log file: $logFile" -ForegroundColor Yellow
Write-Host "Analysis file: $analysisFile" -ForegroundColor Yellow
Write-Host "===============================================" -ForegroundColor Green
Write-Host ""

# Vérifier que Python est disponible
try {
    $pythonVersion = python --version 2>&1
    Write-Host "✅ Python détecté: $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "❌ Python non trouvé. Veuillez installer Python et pyserial" -ForegroundColor Red
    Write-Host "Installation: pip install pyserial" -ForegroundColor Yellow
    exit 1
}

# Vérifier que pyserial est installé
try {
    python -c "import serial" 2>&1 | Out-Null
    Write-Host "✅ pyserial disponible" -ForegroundColor Green
} catch {
    Write-Host "❌ pyserial manquant. Installation: pip install pyserial" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== LANCEMENT DU MONITORING ===" -ForegroundColor Cyan

# Lancer le monitoring avec le script Python
$durationSeconds = $DurationMinutes * 60
try {
    $process = Start-Process -FilePath "python" -ArgumentList "tools/monitor/monitor_unlimited.py", "--port", $Port, "--baud", $BaudRate, "--duration", $durationSeconds, "--output", $logFile -PassThru -NoNewWindow
    
    Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
    Write-Host "Le monitoring va durer $DurationMinutes minutes..." -ForegroundColor Yellow
    Write-Host "Appuyez sur Ctrl+C pour arrêter prématurément" -ForegroundColor Cyan
    Write-Host ""
    
    # Afficher une barre de progression
    $startTime = Get-Date
    $totalSeconds = $durationSeconds
    $checkInterval = 10  # Vérifier toutes les 10 secondes
    
    while (-not $process.HasExited) {
        Start-Sleep -Seconds $checkInterval
        $elapsed = ((Get-Date) - $startTime).TotalSeconds
        $remaining = $totalSeconds - $elapsed
        $progress = [math]::Min(100, ($elapsed / $totalSeconds * 100))
        
        if ($remaining -gt 0) {
            $minutes = [math]::Floor($remaining / 60)
            $seconds = [math]::Floor($remaining % 60)
            Write-Progress -Activity "Monitoring ESP32 - Analyse PANIC" -Status "Temps restant: ${minutes}m ${seconds}s" -PercentComplete $progress
        }
    }
    
    Write-Progress -Activity "Monitoring ESP32 - Analyse PANIC" -Completed
    
    Write-Host "Monitoring terminé (Code de sortie: $($process.ExitCode))" -ForegroundColor Green
    
} catch {
    Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Green

# Analyser les logs capturés
if (Test-Path $logFile) {
    $logs = Get-Content $logFile -Raw
    $logLines = Get-Content $logFile
    
    Write-Host "Fichier de log: $logFile" -ForegroundColor Yellow
    Write-Host "Taille du fichier: $((Get-Item $logFile).Length) bytes" -ForegroundColor Yellow
    Write-Host "Nombre de lignes: $($logLines.Count)" -ForegroundColor Yellow
    Write-Host ""
    
    # Initialiser le rapport d'analyse
    $report = @"
========================================
RAPPORT D'ANALYSE DES CRASHES PANIC
========================================
Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Durée du monitoring: $DurationMinutes minutes
Fichier de log: $logFile
========================================

"@
    
    # ============================================
    # PRIORITÉ 1: ERREURS CRITIQUES
    # ============================================
    $report += "`n🔴 PRIORITÉ 1: ERREURS CRITIQUES`n"
    $report += "========================================`n`n"
    
    # Guru Meditation / Panic
    $guruMatches = $logLines | Select-String -Pattern "Guru Meditation|Guru.*Error|panic'ed|PANIC" -AllMatches
    $guruCount = $guruMatches.Count
    $report += "1. GURU MEDITATION / PANIC: $guruCount occurrence(s)`n"
    if ($guruCount -gt 0) {
        $report += "   ⚠️ CRITIQUE: Des crashes PANIC ont été détectés !`n"
        $report += "   Détails:`n"
        foreach ($match in $guruMatches | Select-Object -First 10) {
            $report += "   - $($match.Line.Trim())`n"
        }
        if ($guruCount -gt 10) {
            $report += "   ... et $($guruCount - 10) autre(s) occurrence(s)`n"
        }
    } else {
        $report += "   ✅ Aucun crash PANIC détecté pendant le monitoring`n"
    }
    $report += "`n"
    
    # Brownout
    $brownoutMatches = $logLines | Select-String -Pattern "Brownout|BROWNOUT|brownout detector" -AllMatches
    $brownoutCount = $brownoutMatches.Count
    $report += "2. BROWNOUT (Alimentation): $brownoutCount occurrence(s)`n"
    if ($brownoutCount -gt 0) {
        $report += "   ⚠️ CRITIQUE: Problème d'alimentation détecté !`n"
        foreach ($match in $brownoutMatches | Select-Object -First 5) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucun brownout détecté`n"
    }
    $report += "`n"
    
    # Core Dump
    $coredumpMatches = $logLines | Select-String -Pattern "Core dump|COREDUMP|coredump" -AllMatches
    $coredumpCount = $coredumpMatches.Count
    $report += "3. CORE DUMP: $coredumpCount occurrence(s)`n"
    if ($coredumpCount -gt 0) {
        $report += "   ⚠️ CRITIQUE: Core dump généré !`n"
        foreach ($match in $coredumpMatches | Select-Object -First 5) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucun core dump détecté`n"
    }
    $report += "`n"
    
    # ============================================
    # PRIORITÉ 2: WATCHDOG ET TIMEOUTS
    # ============================================
    $report += "`n🟡 PRIORITÉ 2: WATCHDOG ET TIMEOUTS`n"
    $report += "========================================`n`n"
    
    # Watchdog timeout
    $watchdogMatches = $logLines | Select-String -Pattern "watchdog|WDT|Watchdog|WDT timeout|task wdt|interrupt wdt" -AllMatches
    $watchdogCount = $watchdogMatches.Count
    $report += "1. WATCHDOG TIMEOUT: $watchdogCount occurrence(s)`n"
    if ($watchdogCount -gt 0) {
        $report += "   ⚠️ ATTENTION: Timeouts watchdog détectés`n"
        $report += "   Types détectés:`n"
        
        # Analyser les types de watchdog
        $taskWdt = ($watchdogMatches | Where-Object { $_.Line -match "task.*wdt|Task.*WDT" }).Count
        $intWdt = ($watchdogMatches | Where-Object { $_.Line -match "interrupt.*wdt|Interrupt.*WDT" }).Count
        $rtcWdt = ($watchdogMatches | Where-Object { $_.Line -match "RTC.*wdt|rtc.*wdt" }).Count
        
        if ($taskWdt -gt 0) {
            $report += "   - Task Watchdog: $taskWdt occurrence(s)`n"
        }
        if ($intWdt -gt 0) {
            $report += "   - Interrupt Watchdog: $intWdt occurrence(s)`n"
        }
        if ($rtcWdt -gt 0) {
            $report += "   - RTC Watchdog: $rtcWdt occurrence(s)`n"
        }
        
        $report += "   Exemples:`n"
        foreach ($match in $watchdogMatches | Select-Object -First 5) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucun timeout watchdog détecté`n"
    }
    $report += "`n"
    
    # Stack overflow
    $stackMatches = $logLines | Select-String -Pattern "stack overflow|Stack overflow|STACK|stack.*overflow" -AllMatches
    $stackCount = $stackMatches.Count
    $report += "2. STACK OVERFLOW: $stackCount occurrence(s)`n"
    if ($stackCount -gt 0) {
        $report += "   ⚠️ CRITIQUE: Débordement de pile détecté !`n"
        foreach ($match in $stackMatches | Select-Object -First 5) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucun stack overflow détecté`n"
    }
    $report += "`n"
    
    # ============================================
    # PRIORITÉ 3: MÉMOIRE
    # ============================================
    $report += "`n🟢 PRIORITÉ 3: MÉMOIRE (HEAP/STACK)`n"
    $report += "========================================`n`n"
    
    # Heap
    $heapMatches = $logLines | Select-String -Pattern "heap|Heap|free heap|Free heap|HEAP" -AllMatches
    $heapCount = $heapMatches.Count
    
    # Extraire les valeurs de heap libre
    $heapValues = @()
    foreach ($line in $logLines) {
        if ($line -match "free.*heap.*(\d+)|heap.*free.*(\d+)|Free.*heap.*(\d+)" -or $line -match "Heap.*free.*(\d+)") {
            $value = if ($matches[1]) { [int]$matches[1] } elseif ($matches[2]) { [int]$matches[2] } elseif ($matches[3]) { [int]$matches[3] } else { 0 }
            if ($value -gt 0) {
                $heapValues += $value
            }
        }
    }
    
    $report += "1. HEAP (Mémoire libre): $heapCount référence(s)`n"
    if ($heapValues.Count -gt 0) {
        $minHeap = ($heapValues | Measure-Object -Minimum).Minimum
        $maxHeap = ($heapValues | Measure-Object -Maximum).Maximum
        $avgHeap = [math]::Round(($heapValues | Measure-Object -Average).Average, 0)
        $report += "   - Minimum: $minHeap bytes`n"
        $report += "   - Maximum: $maxHeap bytes`n"
        $report += "   - Moyenne: $avgHeap bytes`n"
        
        if ($minHeap -lt 50000) {
            $report += "   ⚠️ ATTENTION: Heap minimum très faible (< 50KB) !`n"
        } elseif ($minHeap -lt 80000) {
            $report += "   ⚠️ ATTENTION: Heap minimum faible (< 80KB)`n"
        } else {
            $report += "   ✅ Heap dans des valeurs acceptables`n"
        }
    } else {
        $report += "   ℹ️ Aucune valeur de heap extraite`n"
    }
    $report += "`n"
    
    # Fragmentation
    $fragMatches = $logLines | Select-String -Pattern "fragmentation|Fragmentation|FRAGMENTATION" -AllMatches
    $fragCount = $fragMatches.Count
    $report += "2. FRAGMENTATION MÉMOIRE: $fragCount occurrence(s)`n"
    if ($fragCount -gt 0) {
        $report += "   ⚠️ ATTENTION: Fragmentation mémoire détectée`n"
        foreach ($match in $fragMatches | Select-Object -First 5) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucune fragmentation détectée dans les logs`n"
    }
    $report += "`n"
    
    # ============================================
    # PRIORITÉ 4: WIFI ET WEBSOCKET
    # ============================================
    $report += "`n⚪ PRIORITÉ 4: WIFI ET WEBSOCKET`n"
    $report += "========================================`n`n"
    
    # WiFi
    $wifiErrors = $logLines | Select-String -Pattern "WiFi.*error|WiFi.*fail|WiFi.*timeout|WIFI.*ERROR|WIFI.*FAIL" -AllMatches
    $wifiErrorCount = $wifiErrors.Count
    $wifiDisconnects = $logLines | Select-String -Pattern "WiFi.*disconnect|WiFi.*lost|WIFI.*DISCONNECT" -AllMatches
    $wifiDisconnectCount = $wifiDisconnects.Count
    
    $report += "1. WIFI: $wifiErrorCount erreur(s), $wifiDisconnectCount déconnexion(s)`n"
    if ($wifiErrorCount -gt 0 -or $wifiDisconnectCount -gt 0) {
        $report += "   ⚠️ ATTENTION: Problèmes WiFi détectés`n"
        if ($wifiErrorCount -gt 0) {
            $report += "   Erreurs:`n"
            foreach ($match in $wifiErrors | Select-Object -First 3) {
                $report += "   - $($match.Line.Trim())`n"
            }
        }
        if ($wifiDisconnectCount -gt 0) {
            $report += "   Déconnexions:`n"
            foreach ($match in $wifiDisconnects | Select-Object -First 3) {
                $report += "   - $($match.Line.Trim())`n"
            }
        }
    } else {
        $report += "   ✅ Pas de problème WiFi majeur détecté`n"
    }
    $report += "`n"
    
    # WebSocket
    $wsErrors = $logLines | Select-String -Pattern "WebSocket.*error|WebSocket.*fail|WebSocket.*timeout|websocket.*ERROR" -AllMatches
    $wsErrorCount = $wsErrors.Count
    $wsDisconnects = $logLines | Select-String -Pattern "WebSocket.*disconnect|WebSocket.*close|websocket.*DISCONNECT" -AllMatches
    $wsDisconnectCount = $wsDisconnects.Count
    
    $report += "2. WEBSOCKET: $wsErrorCount erreur(s), $wsDisconnectCount déconnexion(s)`n"
    if ($wsErrorCount -gt 0 -or $wsDisconnectCount -gt 0) {
        $report += "   ⚠️ ATTENTION: Problèmes WebSocket détectés`n"
        if ($wsErrorCount -gt 0) {
            $report += "   Erreurs:`n"
            foreach ($match in $wsErrors | Select-Object -First 3) {
                $report += "   - $($match.Line.Trim())`n"
            }
        }
    } else {
        $report += "   ✅ Pas de problème WebSocket majeur détecté`n"
    }
    $report += "`n"
    
    # ============================================
    # ANALYSE DES REDÉMARRAGES
    # ============================================
    $report += "`n🔄 REDÉMARRAGES DÉTECTÉS`n"
    $report += "========================================`n`n"
    
    $reboots = $logLines | Select-String -Pattern "rst:0x|RST:|reset reason|Redémarrage|redémarrage|boot|BOOT" -AllMatches
    $rebootCount = $reboots.Count
    $report += "Nombre de redémarrages détectés: $rebootCount`n"
    
    if ($rebootCount -gt 0) {
        $report += "   Détails des redémarrages:`n"
        foreach ($match in $reboots | Select-Object -First 10) {
            $report += "   - $($match.Line.Trim())`n"
        }
    } else {
        $report += "   ✅ Aucun redémarrage détecté pendant le monitoring`n"
    }
    $report += "`n"
    
    # ============================================
    # RÉSUMÉ ET RECOMMANDATIONS
    # ============================================
    $report += "`n📊 RÉSUMÉ ET RECOMMANDATIONS`n"
    $report += "========================================`n`n"
    
    $criticalIssues = 0
    $warnings = 0
    
    if ($guruCount -gt 0 -or $brownoutCount -gt 0 -or $coredumpCount -gt 0 -or $stackCount -gt 0) {
        $criticalIssues++
    }
    if ($watchdogCount -gt 0) {
        $warnings++
    }
    if ($minHeap -lt 50000 -and $heapValues.Count -gt 0) {
        $criticalIssues++
    } elseif ($minHeap -lt 80000 -and $heapValues.Count -gt 0) {
        $warnings++
    }
    if ($wifiErrorCount -gt 5 -or $wsErrorCount -gt 5) {
        $warnings++
    }
    
    $report += "Problèmes critiques détectés: $criticalIssues`n"
    $report += "Avertissements: $warnings`n"
    $report += "`n"
    
    if ($criticalIssues -gt 0) {
        $report += "🔴 ACTIONS IMMÉDIATES REQUISES:`n"
        $report += "========================================`n"
        
        if ($guruCount -gt 0) {
            $report += "1. Analyser les logs Guru Meditation pour identifier la tâche/core concerné`n"
            $report += "2. Vérifier les stack sizes des tâches FreeRTOS`n"
            $report += "3. Vérifier les timeouts watchdog (actuellement 300s)`n"
            $report += "4. Ajouter plus de esp_task_wdt_reset() dans les boucles longues`n"
        }
        
        if ($brownoutCount -gt 0) {
            $report += "5. Vérifier l'alimentation 5V (doit être stable)`n"
            $report += "6. Vérifier la consommation de courant`n"
        }
        
        if ($stackCount -gt 0) {
            $report += "7. Augmenter les stack sizes des tâches concernées`n"
            $report += "8. Réduire l'utilisation de la pile (variables locales)`n"
        }
        
        if ($minHeap -lt 50000 -and $heapValues.Count -gt 0) {
            $report += "9. Réduire l'utilisation de String Arduino (utiliser char[] à la place)`n"
            $report += "10. Vérifier les fuites mémoire (allocations non libérées)`n"
            $report += "11. Réduire la taille des buffers statiques si possible`n"
        }
        
        $report += "`n"
    } elseif ($warnings -gt 0) {
        $report += "🟡 ACTIONS RECOMMANDÉES:`n"
        $report += "========================================`n"
        
        if ($watchdogCount -gt 0) {
            $report += "1. Vérifier que esp_task_wdt_reset() est appelé régulièrement`n"
            $report += "2. Réduire la durée des opérations bloquantes`n"
        }
        
        if ($minHeap -lt 80000 -and $heapValues.Count -gt 0) {
            $report += "3. Monitorer l'évolution du heap sur plusieurs heures`n"
            $report += "4. Vérifier la fragmentation mémoire`n"
        }
        
        $report += "`n"
    } else {
        $report += "✅ AUCUN PROBLÈME MAJEUR DÉTECTÉ`n"
        $report += "Le système semble stable pendant cette période de monitoring.`n"
        $report += "`n"
        $report += "Recommandations générales:`n"
        $report += "- Continuer le monitoring sur une période plus longue (plusieurs heures)`n"
        $report += "- Vérifier les logs historiques pour identifier les patterns`n"
        $report += "`n"
    }
    
    # Sauvegarder le rapport
    $report | Out-File -FilePath $analysisFile -Encoding UTF8
    
    # Afficher le rapport à l'écran
    Write-Host $report
    
    Write-Host ""
    Write-Host "=== RAPPORT SAUVEGARDÉ ===" -ForegroundColor Green
    Write-Host "Fichier d'analyse: $analysisFile" -ForegroundColor Yellow
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Cyan


