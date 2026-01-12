# Script de flash et monitoring 30 minutes - wroom-test
# Version: v11.124
# Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')

Write-Host "=== FLASH ET MONITORING 30 MINUTES - WROOM-TEST ===" -ForegroundColor Green
Write-Host "Version firmware: v11.124" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$comPort = "COM4"
$monitorDuration = 1800  # 30 minutes en secondes
$logFile = "monitor_30min_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$analysisFile = "monitor_30min_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')_analysis.txt"

# =============================================================================
# ÉTAPE 1: FLASH DU FIRMWARE
# =============================================================================
Write-Host "=== ÉTAPE 1: FLASH DU FIRMWARE ===" -ForegroundColor Cyan
Write-Host ""

# Arrêter tout processus qui pourrait bloquer le port COM
Write-Host "1.1. Vérification des processus bloquant $comPort..." -ForegroundColor Yellow
try {
    $comProcesses = Get-Process | Where-Object { $_.ProcessName -like "*python*" -or 
                                               $_.ProcessName -like "*pio*" -or
                                               $_.MainWindowTitle -like "*monitor*" -or 
                                               $_.MainWindowTitle -like "*serial*" }
    if ($comProcesses) {
        Write-Host "Processus détectés: $($comProcesses.Count)" -ForegroundColor Yellow
        foreach ($proc in $comProcesses) {
            Write-Host "  - $($proc.ProcessName) (ID: $($proc.Id))" -ForegroundColor Yellow
            try {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                Write-Host "    ✓ Arrêté" -ForegroundColor Green
            } catch {
                Write-Host "    ⚠ Impossible d'arrêter" -ForegroundColor Red
            }
        }
    } else {
        Write-Host "Aucun processus bloquant détecté" -ForegroundColor Green
    }
} catch {
    Write-Host "Erreur lors de la vérification des processus: $($_.Exception.Message)" -ForegroundColor Red
}

# Attendre un peu pour laisser le temps aux processus de se libérer
Write-Host "1.2. Attente de libération du port (5 secondes)..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

# Compiler d'abord pour vérifier qu'il n'y a pas d'erreurs
Write-Host "1.3. Compilation du firmware wroom-test..." -ForegroundColor Yellow
$compileResult = pio run -e wroom-test
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "✅ Compilation réussie" -ForegroundColor Green

# Flash du firmware
Write-Host "1.4. Flash du firmware..." -ForegroundColor Yellow
$uploadResult = pio run -e wroom-test --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash firmware réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryResult = pio run -e wroom-test --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash firmware réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du système de fichiers
Write-Host "1.5. Flash du système de fichiers LittleFS..." -ForegroundColor Yellow
Start-Sleep -Seconds 3  # Petit délai entre les flashes
$uploadfsResult = pio run -e wroom-test --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash LittleFS réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryfsResult = pio run -e wroom-test --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash LittleFS réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash LittleFS" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "✅ FLASH COMPLET RÉUSSI !" -ForegroundColor Green
Write-Host ""

# Attendre que l'ESP32 redémarre
Write-Host "1.6. Attente du redémarrage de l'ESP32 (10 secondes)..." -ForegroundColor Yellow
Start-Sleep -Seconds 10

# =============================================================================
# ÉTAPE 2: MONITORING 30 MINUTES
# =============================================================================
Write-Host "=== ÉTAPE 2: MONITORING 30 MINUTES ===" -ForegroundColor Cyan
Write-Host "Durée: $monitorDuration secondes (30 minutes)" -ForegroundColor Yellow
Write-Host "Port: $comPort" -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

Write-Host "📋 Démarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    # Lancement du monitoring verbeux avec redirection vers fichier ET console
    # Options: --verbose pour verbosité maximale, pas de limite de taille de buffer
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", "wroom-test", "--port", $comPort, "--verbose") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    # Timer de 30 minutes
    $endTime = (Get-Date).AddSeconds($monitorDuration)
    $lastProgressUpdate = 0
    
    Write-Host "Monitoring en cours... (appuyez sur Ctrl+C pour arrêter prématurément)" -ForegroundColor Gray
    Write-Host ""
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = $monitorDuration - $remaining
        $progressPercent = [math]::Round(($progress / $monitorDuration) * 100)
        $elapsedMinutes = [math]::Floor($progress / 60)
        $elapsedSeconds = $progress % 60
        
        # Mise à jour toutes les 30 secondes
        if ($progress -ge $lastProgressUpdate + 30) {
            Write-Host "[$elapsedMinutes:$($elapsedSeconds.ToString('00'))] Progression: $progressPercent% ($remaining s restantes)" -ForegroundColor Cyan
            $lastProgressUpdate = $progress
        }
        
        Start-Sleep -Seconds 1
        
        # Vérifier que le processus est encore en vie
        if ($monitorProcess.HasExited) {
            Write-Host "`n⚠️ Le processus de monitoring s'est arrêté prématurément" -ForegroundColor Red
            break
        }
    }
    
    # Arrêter le monitoring
    if (-not $monitorProcess.HasExited) {
        Write-Host "`n🛑 Arrêt du monitoring..." -ForegroundColor Yellow
        $monitorProcess.Kill()
        Start-Sleep -Seconds 2
    }
    
    Write-Host "`n✅ Monitoring terminé !" -ForegroundColor Green
    Write-Host ""
    
} catch {
    Write-Host "❌ Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# =============================================================================
# ÉTAPE 3: ANALYSE DU LOG
# =============================================================================
Write-Host "=== ÉTAPE 3: ANALYSE DU LOG ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $logFile) {
    Write-Host "📊 Analyse du log en cours..." -ForegroundColor Yellow
    $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
    
    if ($logContent) {
        # Initialiser le rapport d'analyse
        $analysis = @"
=== ANALYSE DU LOG - MONITORING 30 MINUTES ===
Version firmware: v11.124
Date analyse: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $logFile
Durée monitoring: 30 minutes (1800 secondes)

"@
        
        # Statistiques générales
        $lines = Get-Content $logFile -ErrorAction SilentlyContinue
        $lineCount = if ($lines) { $lines.Count } else { 0 }
        $fileSize = (Get-Item $logFile).Length / 1KB
        
        $analysis += @"
=== STATISTIQUES GÉNÉRALES ===
- Total lignes loggées: $lineCount
- Taille du fichier: $([math]::Round($fileSize, 2)) KB
- Durée effective: $(if ($lineCount -gt 0) { "OK" } else { "PROBLÈME - Log vide ou erreur" })

"@
        
        # Patterns critiques (PRIORITÉ 1)
        Write-Host "Recherche des erreurs critiques (PRIORITÉ 1)..." -ForegroundColor Yellow
        $criticalPatterns = @(
            @{ Pattern = "Guru Meditation"; Name = "Guru Meditation"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "Panic(?!.*diag_hasPanic)"; Name = "Panic"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "Brownout"; Name = "Brownout"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "Core dump(?!.*No core dump partition)"; Name = "Core Dump"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "assert.*failed|ASSERT.*FAILED"; Name = "Assert Failed"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "Stack overflow|STACK OVERFLOW"; Name = "Stack Overflow"; Type = "CRITIQUE"; Color = "Red" },
            @{ Pattern = "Abort\(\)|abort\(\)"; Name = "Abort"; Type = "CRITIQUE"; Color = "Red" }
        )
        
        $criticalIssues = @()
        foreach ($check in $criticalPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $criticalIssues += "  ❌ $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  ❌ $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Red
                
                # Extraire les lignes contenant l'erreur
                $errorLines = Get-Content $logFile | Select-String -Pattern $check.Pattern -CaseSensitive:$false
                if ($errorLines) {
                    $analysis += "`nDétails $($check.Name):`n"
                    foreach ($line in ($errorLines | Select-Object -First 10)) {
                        $analysis += "  - $line`n"
                    }
                    if ($errorLines.Count -gt 10) {
                        $analysis += "  ... et $($errorLines.Count - 10) autre(s) occurrence(s)`n"
                    }
                }
            }
        }
        
        if ($criticalIssues.Count -eq 0) {
            $analysis += "✅ Aucune erreur critique détectée`n"
            Write-Host "  ✅ Aucune erreur critique détectée" -ForegroundColor Green
        } else {
            $analysis += "`n❌ ERREURS CRITIQUES DÉTECTÉES:`n" + ($criticalIssues -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Patterns warnings (PRIORITÉ 2)
        Write-Host "Recherche des warnings (PRIORITÉ 2)..." -ForegroundColor Yellow
        $warningPatterns = @(
            @{ Pattern = "Watchdog.*timeout|TWDT.*timeout"; Name = "Watchdog Timeout"; Type = "WARNING"; Color = "Yellow" },
            @{ Pattern = "WiFi.*timeout|WIFI.*TIMEOUT"; Name = "WiFi Timeout"; Type = "WARNING"; Color = "Yellow" },
            @{ Pattern = "WebSocket.*disconnect|websocket.*error"; Name = "WebSocket Error"; Type = "WARNING"; Color = "Yellow" },
            @{ Pattern = "heap.*corrupt|memory.*corrupt"; Name = "Memory Corruption"; Type = "WARNING"; Color = "Yellow" },
            @{ Pattern = "task.*overflow|Task.*overflow"; Name = "Task Overflow"; Type = "WARNING"; Color = "Yellow" },
            @{ Pattern = "Free heap.*low|heap.*low|Low.*heap"; Name = "Low Heap Memory"; Type = "WARNING"; Color = "Yellow" }
        )
        
        $warnings = @()
        foreach ($check in $warningPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $warnings += "  ⚠️ $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  ⚠️ $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Yellow
            }
        }
        
        if ($warnings.Count -eq 0) {
            $analysis += "✅ Aucun warning détecté`n"
            Write-Host "  ✅ Aucun warning détecté" -ForegroundColor Green
        } else {
            $analysis += "`n⚠️ WARNINGS DÉTECTÉS:`n" + ($warnings -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Patterns de succès (PRIORITÉ 3)
        Write-Host "Recherche des indicateurs de succès (PRIORITÉ 3)..." -ForegroundColor Yellow
        $successPatterns = @(
            @{ Pattern = "Démarrage FFP5CS v11|App start v11|BOOT FFP5CS v11"; Name = "Démarrage OK"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "Init done|Initialization complete|Setup.*complete"; Name = "Initialisation OK"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "WiFi.*connecté|WiFi.*connected|WiFi connected|Connected to.*SSID"; Name = "WiFi Connecté"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "/api/status|/api/sensors|HTTP.*200"; Name = "API Répond"; Type = "INFO"; Color = "Cyan" }
        )
        
        $successIndicators = @()
        foreach ($check in $successPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $successIndicators += "  ✅ $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  ✅ $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Green
            }
        }
        
        if ($successIndicators.Count -gt 0) {
            $analysis += "✅ INDICATEURS DE SUCCÈS:`n" + ($successIndicators -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Analyse mémoire (PRIORITÉ 3)
        Write-Host "Analyse de l'utilisation mémoire..." -ForegroundColor Yellow
        $heapMatches = [regex]::Matches($logContent, "heap.*free|Free heap|Free.*heap|heap.*available", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($heapMatches.Count -gt 0) {
            # Extraire les valeurs de heap
            $heapValues = @()
            $heapLines = Get-Content $logFile | Select-String -Pattern "heap.*free|Free heap|Free.*heap|heap.*available" -CaseSensitive:$false
            foreach ($line in $heapLines) {
                if ($line -match "(\d+)\s*(bytes|KB|MB)") {
                    $heapValues += $line
                }
            }
            
            if ($heapValues.Count -gt 0) {
                $analysis += "📊 ANALYSE MÉMOIRE:`n"
                $analysis += "  - Messages mémoire trouvés: $($heapMatches.Count)`n"
                $analysis += "  - Exemples de valeurs:`n"
                foreach ($val in ($heapValues | Select-Object -First 5)) {
                    $analysis += "    - $val`n"
                }
                Write-Host "  ✅ Messages mémoire détectés: $($heapMatches.Count)" -ForegroundColor Green
            }
        } else {
            Write-Host "  ⚠️ Aucun message mémoire détecté" -ForegroundColor Yellow
        }
        
        $analysis += "`n"
        
        # Recherche des reboots
        Write-Host "Analyse des reboots..." -ForegroundColor Yellow
        $rebootMatches = [regex]::Matches($logContent, "BOOT FFP5CS|App start|Démarrage FFP5CS|rst:0x|RTC reset", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $rebootCount = $rebootMatches.Count
        if ($rebootCount -gt 1) {
            $analysis += "⚠️ REBOOTS DÉTECTÉS:`n"
            $analysis += "  - Nombre de reboots détectés: $rebootCount`n"
            Write-Host "  ⚠️ Nombre de reboots détectés: $rebootCount" -ForegroundColor Yellow
        } else {
            $analysis += "✅ Aucun reboot non prévu détecté`n"
            Write-Host "  ✅ Aucun reboot non prévu détecté" -ForegroundColor Green
        }
        
        $analysis += "`n"
        
        # Résumé final
        $analysis += "=== RÉSUMÉ FINAL ===`n"
        if ($criticalIssues.Count -eq 0 -and $warnings.Count -eq 0) {
            $analysis += "✅ SYSTÈME STABLE - Aucune erreur critique ni warning détecté`n"
            Write-Host "✅ SYSTÈME STABLE - Aucune erreur critique ni warning détecté" -ForegroundColor Green
        } elseif ($criticalIssues.Count -gt 0) {
            $analysis += "❌ SYSTÈME INSTABLE - Erreurs critiques détectées`n"
            Write-Host "❌ SYSTÈME INSTABLE - Erreurs critiques détectées" -ForegroundColor Red
        } elseif ($warnings.Count -gt 0) {
            $analysis += "⚠️ SYSTÈME À SURVEILLER - Warnings détectés`n"
            Write-Host "⚠️ SYSTÈME À SURVEILLER - Warnings détectés" -ForegroundColor Yellow
        }
        
        $analysis += "`n=== FIN DE L'ANALYSE ===`n"
        
        # Sauvegarder l'analyse
        $analysis | Out-File -FilePath $analysisFile -Encoding UTF8
        Write-Host ""
        Write-Host "📁 Analyse complète sauvegardée: $analysisFile" -ForegroundColor Cyan
        
        # Afficher le résumé
        Write-Host ""
        Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
        Write-Host "📁 Log complet: $logFile" -ForegroundColor Gray
        Write-Host "📁 Analyse détaillée: $analysisFile" -ForegroundColor Gray
        Write-Host "📊 Total lignes: $lineCount" -ForegroundColor Gray
        Write-Host "📈 Taille log: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
        
    } else {
        Write-Host "⚠️ Le fichier de log est vide" -ForegroundColor Yellow
        "❌ ERREUR: Le fichier de log est vide ou n'a pas pu être lu" | Out-File -FilePath $analysisFile -Encoding UTF8
    }
} else {
    Write-Host "⚠️ Aucun fichier de log généré" -ForegroundColor Yellow
    "❌ ERREUR: Aucun fichier de log généré" | Out-File -FilePath $analysisFile -Encoding UTF8
}

Write-Host ""
Write-Host "=== PROCESSUS TERMINÉ ===" -ForegroundColor Green
Write-Host "Vérifiez les fichiers pour l'analyse détaillée:" -ForegroundColor Cyan
Write-Host "  - Log: $logFile" -ForegroundColor Gray
Write-Host "  - Analyse: $analysisFile" -ForegroundColor Gray

