# Script de flash et monitoring - wroom-test
# Version: v11.131
# Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')

param(
    [string]$Port = "",
    [string]$DefaultPort = "COM4",
    [int]$MonitorDuration = 600,
    [string]$Environment = "wroom-test",
    [switch]$SkipUploadFS
)

$durationTag = if ($MonitorDuration % 60 -eq 0) { "{0}min" -f ($MonitorDuration / 60) } else { "{0}s" -f $MonitorDuration }

Write-Host "=== FLASH ET MONITORING $durationTag - WROOM-TEST ===" -ForegroundColor Green
Write-Host "Version firmware: v11.131" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

function Get-SerialPorts {
    try {
        $ports = Get-CimInstance Win32_SerialPort | Select-Object -ExpandProperty DeviceID
        if ($ports) { return $ports }
    } catch {
        # ignore
    }
    return @()
}

# Detection automatique du port COM (ou utiliser un port par defaut)
$comPort = $null

Write-Host "=== DETECTION DU PORT SERIE ===" -ForegroundColor Cyan
if ($Port) {
    $comPort = $Port
    Write-Host "[OK] Port force: $comPort" -ForegroundColor Green
}

try {
    if (-not $comPort) {
        # Essayer de detecter le port via PlatformIO
        $pioDevices = pio device list 2>&1 | Out-String
        $pioComs = @()
        if ($pioDevices -match "COM\d+") {
            $matches = [regex]::Matches($pioDevices, "COM\d+")
            if ($matches.Count -gt 0) {
                $pioComs = $matches | ForEach-Object { $_.Value } | Select-Object -Unique
            }
        }

        $systemComs = Get-SerialPorts
        $allComs = @($pioComs + $systemComs) | Where-Object { $_ } | Select-Object -Unique

        if ($allComs.Count -gt 0) {
            Write-Host "Ports detectes: $($allComs -join ', ')" -ForegroundColor Gray
        }

        if ($allComs.Count -eq 1) {
            $comPort = $allComs[0]
            Write-Host "[OK] Port detecte automatiquement: $comPort" -ForegroundColor Green
        } elseif ($allComs.Count -gt 1) {
            if ($DefaultPort -and ($allComs -contains $DefaultPort)) {
                $comPort = $DefaultPort
                Write-Host "[WARN] Plusieurs ports detectes, utilisation du port par defaut: $comPort" -ForegroundColor Yellow
            } elseif ($pioComs.Count -gt 0) {
                $comPort = $pioComs[0]
                Write-Host "[WARN] Plusieurs ports detectes, utilisation du premier port PIO: $comPort" -ForegroundColor Yellow
            }
        }
    }
} catch {
    Write-Host "[WARN] Impossible de detecter automatiquement le port" -ForegroundColor Yellow
}

if (-not $comPort) {
    $comPort = $DefaultPort
    Write-Host "[WARN] Utilisation du port par defaut: $comPort" -ForegroundColor Yellow
    Write-Host "   (Modifiez la variable `$DefaultPort si necessaire)" -ForegroundColor Gray
}

$logFile = "monitor_${durationTag}_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$analysisFile = "monitor_${durationTag}_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')_analysis.txt"

# =============================================================================
# Ã‰TAPE 1: FLASH DU FIRMWARE
# =============================================================================
Write-Host ""
Write-Host "=== ETAPE 1: FLASH DU FIRMWARE ===" -ForegroundColor Cyan
Write-Host ""

# Arreter tout processus qui pourrait bloquer le port COM
Write-Host "1.1. Verification des processus bloquant $comPort..." -ForegroundColor Yellow
try {
    $comProcesses = Get-Process | Where-Object { $_.ProcessName -like "*python*" -or 
                                               $_.ProcessName -like "*pio*" -or
                                               $_.MainWindowTitle -like "*monitor*" -or 
                                               $_.MainWindowTitle -like "*serial*" }
    if ($comProcesses) {
        Write-Host "Processus detectes: $($comProcesses.Count)" -ForegroundColor Yellow
        foreach ($proc in $comProcesses) {
            Write-Host "  - $($proc.ProcessName) (ID: $($proc.Id))" -ForegroundColor Yellow
            try {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                Write-Host "    [OK] Arrete" -ForegroundColor Green
            } catch {
                Write-Host "    [WARN] Impossible d'arreter" -ForegroundColor Red
            }
        }
    } else {
        Write-Host "Aucun processus bloquant detecte" -ForegroundColor Green
    }
} catch {
    Write-Host "Erreur lors de la verification des processus: $($_.Exception.Message)" -ForegroundColor Red
}

# Attendre un peu pour laisser le temps aux processus de se liberer
Write-Host "1.2. Attente de liberation du port (5 secondes)..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

# Compiler d'abord pour verifier qu'il n'y a pas d'erreurs
Write-Host "1.3. Compilation du firmware $Environment..." -ForegroundColor Yellow
$compileResult = pio run -e $Environment
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
    Write-Host "[OK] Compilation reussie" -ForegroundColor Green

# Flash du firmware
Write-Host "1.4. Flash du firmware..." -ForegroundColor Yellow
$uploadResult = pio run -e $Environment --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] Flash firmware reussi !" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et rÃ©essayer
    Start-Sleep -Seconds 10
    $retryResult = pio run -e $Environment --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Flash firmware reussi au 2eme essai !" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Echec definitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du systeme de fichiers
if (-not $SkipUploadFS) {
    Write-Host "1.5. Flash du systeme de fichiers LittleFS..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3  # Petit delai entre les flashes
    $uploadfsResult = pio run -e $Environment --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Flash LittleFS reussi !" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
        Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
        
        # Attendre plus longtemps et reessayer
        Start-Sleep -Seconds 10
        $retryfsResult = pio run -e $Environment --target uploadfs
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[OK] Flash LittleFS reussi au 2eme essai !" -ForegroundColor Green
        } else {
            Write-Host "[ERROR] Echec definitif du flash LittleFS" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
} else {
    Write-Host "1.5. Flash LittleFS ignore (SkipUploadFS)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[OK] FLASH COMPLET REUSSI !" -ForegroundColor Green
Write-Host ""

# Attendre que l'ESP32 redemarre
Write-Host "1.6. Attente du redemarrage de l'ESP32 (10 secondes)..." -ForegroundColor Yellow
Start-Sleep -Seconds 10

# =============================================================================
# ETAPE 2: MONITORING
# =============================================================================
Write-Host "=== ETAPE 2: MONITORING ===" -ForegroundColor Cyan
Write-Host "Duree: $MonitorDuration secondes ($durationTag)" -ForegroundColor Yellow
Write-Host "Port: $comPort" -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

Write-Host "Demarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    # Verifier si le script Python de monitoring est disponible
    $pythonMonitorScript = "tools\monitor\monitor_unlimited.py"
    $usePythonMonitor = Test-Path $pythonMonitorScript
    
    if ($usePythonMonitor) {
        Write-Host "Utilisation du script Python de monitoring..." -ForegroundColor Cyan
        $pythonOk = $true
        try {
            python --version 2>&1 | Out-Null
            python -c "import serial" 2>&1 | Out-Null
        } catch {
            $pythonOk = $false
            Write-Host "[WARN] Python/pyserial indisponible, fallback PlatformIO." -ForegroundColor Yellow
        }

        if ($pythonOk) {
            # Utiliser le script Python pour un monitoring plus fiable
            $pythonArgs = @(
                $pythonMonitorScript
                "--port", $comPort
                "--baud", "115200"
                "--duration", $MonitorDuration.ToString()
                "--output", $logFile
            )
            
            Write-Host "Lancement: python $($pythonArgs -join ' ')" -ForegroundColor Gray
            python $pythonArgs
            
            if ($LASTEXITCODE -ne 0) {
                Write-Host "[WARN] Le script Python a renvoye un code d'erreur: $LASTEXITCODE" -ForegroundColor Yellow
            }
        } else {
            $usePythonMonitor = $false
        }
    }
    
    if (-not $usePythonMonitor) {
        Write-Host "Script Python non trouve, utilisation de PlatformIO directement..." -ForegroundColor Yellow
        Write-Host "[WARN] Note: Le monitoring PlatformIO peut etre limite en duree" -ForegroundColor Yellow
        
        # Creer le fichier de log
        $logStream = [System.IO.StreamWriter]::new($logFile, $false, [System.Text.Encoding]::UTF8)
        $logStream.WriteLine("# Monitoring ESP32 wroom-test - $durationTag")
        $logStream.WriteLine("# Demarrage: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
        $logStream.WriteLine("# Port: $comPort")
        $logStream.WriteLine("# Duree: $MonitorDuration secondes")
        $logStream.WriteLine("")
        $logStream.Flush()
        
        # Lancer le monitoring avec PlatformIO en arriere-plan
        $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", $Environment, "--port", $comPort, "--baud", "115200", "--filter", "time") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
        
        # Timer avec affichage de progression
        $endTime = (Get-Date).AddSeconds($MonitorDuration)
        $lastProgressUpdate = 0
        
        Write-Host "Monitoring en cours... (appuyez sur Ctrl+C pour arreter prematurement)" -ForegroundColor Gray
        Write-Host ""
        
        while ((Get-Date) -lt $endTime) {
            $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
            $progress = $MonitorDuration - $remaining
            $progressPercent = [math]::Round(($progress / $MonitorDuration) * 100)
            $elapsedMinutes = [math]::Floor($progress / 60)
            $elapsedSeconds = $progress % 60
            $remainingMinutes = [math]::Floor($remaining / 60)
            $remainingSeconds = $remaining % 60
            
            # Mise a jour toutes les 30 secondes
            if ($progress -ge $lastProgressUpdate + 30) {
                Write-Host "[$($elapsedMinutes.ToString('00')):$($elapsedSeconds.ToString('00'))] Progression: $progressPercent% ($($remainingMinutes.ToString('00')):$($remainingSeconds.ToString('00')) restantes)" -ForegroundColor Cyan
                $lastProgressUpdate = $progress
            }
            
            Start-Sleep -Seconds 1
            
            # Verifier que le processus est encore en vie
            if ($monitorProcess.HasExited) {
                Write-Host "`n[WARN] Le processus de monitoring s'est arrete prematurement" -ForegroundColor Red
                break
            }
        }
        
        # ArrÃªter le monitoring
        Write-Host "`nArret du monitoring..." -ForegroundColor Yellow
        if (-not $monitorProcess.HasExited) {
            $monitorProcess.Kill()
            Start-Sleep -Seconds 2
        }
        $logStream.Close()
    }
    
    Write-Host "`n[OK] Monitoring termine !" -ForegroundColor Green
    Write-Host ""
    
} catch {
    Write-Host "[ERROR] Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# =============================================================================
# Ã‰TAPE 3: ANALYSE DU LOG
# =============================================================================
Write-Host "=== ETAPE 3: ANALYSE DU LOG ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $logFile) {
    Write-Host "Analyse du log en cours..." -ForegroundColor Yellow
    $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
    
    if ($logContent) {
        # Initialiser le rapport d'analyse
        $analysis = @"
=== ANALYSE DU LOG - MONITORING $durationTag ===
Version firmware: v11.131
Date analyse: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $logFile
Duree monitoring: $durationTag ($MonitorDuration secondes)

"@
        
        # Statistiques generales
        $lines = Get-Content $logFile -ErrorAction SilentlyContinue
        $lineCount = if ($lines) { $lines.Count } else { 0 }
        $fileSize = (Get-Item $logFile).Length / 1KB
        
        $analysis += @"
=== STATISTIQUES GENERALES ===
- Total lignes loggees: $lineCount
- Taille du fichier: $([math]::Round($fileSize, 2)) KB
- Duree effective: $(if ($lineCount -gt 0) { "OK" } else { "PROBLEME - Log vide ou erreur" })

"@
        
        # Patterns critiques (PRIORITE 1)
        Write-Host "Recherche des erreurs critiques (PRIORITE 1)..." -ForegroundColor Yellow
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
                $criticalIssues += "  [ERROR] $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  [ERROR] $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Red
                
                # Extraire les lignes contenant l'erreur
                $errorLines = Get-Content $logFile | Select-String -Pattern $check.Pattern -CaseSensitive:$false
                if ($errorLines) {
                $analysis += "`nDetails $($check.Name):`n"
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
            $analysis += "[OK] Aucune erreur critique detectee`n"
            Write-Host "  [OK] Aucune erreur critique detectee" -ForegroundColor Green
        } else {
            $analysis += "`n[ERROR] ERREURS CRITIQUES DETECTEES:`n" + ($criticalIssues -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Patterns warnings (PRIORITE 2)
        Write-Host "Recherche des warnings (PRIORITE 2)..." -ForegroundColor Yellow
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
                $warnings += "  [WARN] $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  [WARN] $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Yellow
            }
        }
        
        if ($warnings.Count -eq 0) {
            $analysis += "[OK] Aucun warning detecte`n"
            Write-Host "  [OK] Aucun warning detecte" -ForegroundColor Green
        } else {
            $analysis += "`n[WARN] WARNINGS DETECTES:`n" + ($warnings -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Patterns de succes (PRIORITE 3)
        Write-Host "Recherche des indicateurs de succes (PRIORITE 3)..." -ForegroundColor Yellow
        $successPatterns = @(
            @{ Pattern = "Demarrage FFP5CS v11|App start v11|BOOT FFP5CS v11"; Name = "Demarrage OK"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "Init done|Initialization complete|Setup.*complete"; Name = "Initialisation OK"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "WiFi.*connecte|WiFi.*connected|WiFi connected|Connected to.*SSID"; Name = "WiFi Connecte"; Type = "SUCCESS"; Color = "Green" },
            @{ Pattern = "/api/status|/api/sensors|HTTP.*200"; Name = "API Repond"; Type = "INFO"; Color = "Cyan" }
        )
        
        $successIndicators = @()
        foreach ($check in $successPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $successIndicators += "  [OK] $($check.Name): $($matches.Count) occurrence(s)"
                Write-Host "  [OK] $($check.Name): $($matches.Count) occurrence(s)" -ForegroundColor Green
            }
        }
        
        if ($successIndicators.Count -gt 0) {
            $analysis += "[OK] INDICATEURS DE SUCCES:`n" + ($successIndicators -join "`n") + "`n"
        }
        
        $analysis += "`n"
        
        # Analyse memoire (PRIORITE 3)
        Write-Host "Analyse de l'utilisation memoire..." -ForegroundColor Yellow
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
                $analysis += "=== ANALYSE MEMOIRE ===`n"
                $analysis += "  - Messages memoire trouves: $($heapMatches.Count)`n"
                $analysis += "  - Exemples de valeurs:`n"
                foreach ($val in ($heapValues | Select-Object -First 5)) {
                    $analysis += "    - $val`n"
                }
                Write-Host "  [OK] Messages memoire detectes: $($heapMatches.Count)" -ForegroundColor Green
            }
        } else {
            Write-Host "  [WARN] Aucun message memoire detecte" -ForegroundColor Yellow
        }
        
        $analysis += "`n"
        
        # Recherche des reboots
        Write-Host "Analyse des reboots..." -ForegroundColor Yellow
        $rebootMatches = [regex]::Matches($logContent, "BOOT FFP5CS|App start|Demarrage FFP5CS|rst:0x|RTC reset", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $rebootCount = $rebootMatches.Count
        if ($rebootCount -gt 1) {
            $analysis += "[WARN] REBOOTS DETECTES:`n"
            $analysis += "  - Nombre de reboots detectes: $rebootCount`n"
            Write-Host "  [WARN] Nombre de reboots detectes: $rebootCount" -ForegroundColor Yellow
        } else {
            $analysis += "[OK] Aucun reboot non prevu detecte`n"
            Write-Host "  [OK] Aucun reboot non prevu detecte" -ForegroundColor Green
        }
        
        $analysis += "`n"
        
        # Resume final
        $analysis += "=== RESUME FINAL ===`n"
        if ($criticalIssues.Count -eq 0 -and $warnings.Count -eq 0) {
            $analysis += "[OK] SYSTEME STABLE - Aucune erreur critique ni warning detecte`n"
            Write-Host "[OK] SYSTEME STABLE - Aucune erreur critique ni warning detecte" -ForegroundColor Green
        } elseif ($criticalIssues.Count -gt 0) {
            $analysis += "[ERROR] SYSTEME INSTABLE - Erreurs critiques detectees`n"
            Write-Host "[ERROR] SYSTEME INSTABLE - Erreurs critiques detectees" -ForegroundColor Red
        } elseif ($warnings.Count -gt 0) {
            $analysis += "[WARN] SYSTEME A SURVEILLER - Warnings detectes`n"
            Write-Host "[WARN] SYSTEME A SURVEILLER - Warnings detectes" -ForegroundColor Yellow
        }
        
        $analysis += "`n=== FIN DE L'ANALYSE ===`n"
        
        # Sauvegarder l'analyse
        $analysis | Out-File -FilePath $analysisFile -Encoding UTF8
        Write-Host ""
        Write-Host "Analyse complete sauvegardee: $analysisFile" -ForegroundColor Cyan
        
        # Afficher le rÃ©sumÃ©
        Write-Host ""
        Write-Host "=== RESUME ===" -ForegroundColor Cyan
        Write-Host "Log complet: $logFile" -ForegroundColor Gray
        Write-Host "Analyse detaillee: $analysisFile" -ForegroundColor Gray
        Write-Host "Total lignes: $lineCount" -ForegroundColor Gray
                $tailleMsg = "Taille log: " + [math]::Round($fileSize, 2) + " KB"; Write-Host $tailleMsg -ForegroundColor Gray
        
    } else {
        Write-Host "[WARN] Le fichier de log est vide" -ForegroundColor Yellow
        "[ERROR] ERREUR: Le fichier de log est vide ou n'a pas pu etre lu" | Out-File -FilePath $analysisFile -Encoding UTF8
    }
} else {
    Write-Host "[WARN] Aucun fichier de log genere" -ForegroundColor Yellow
    "[ERROR] ERREUR: Aucun fichier de log genere" | Out-File -FilePath $analysisFile -Encoding UTF8
}

Write-Host ""
Write-Host "=== PROCESSUS TERMINE ===" -ForegroundColor Green
Write-Host "Verifiez les fichiers pour l'analyse detaillee:" -ForegroundColor Cyan
$logPathMsg = "  - Log: " + $logFile
$analysisPathMsg = "  - Analyse: " + $analysisFile
Write-Host $logPathMsg -ForegroundColor Gray
Write-Host $analysisPathMsg -ForegroundColor Gray

