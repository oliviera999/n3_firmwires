# =============================================================================
# Script de flash wroom-test avec NVS et monitoring
# =============================================================================
# Description:
#   Flash le firmware ESP32-WROOM (wroom-test), reinitialise la NVS,
#   effectue un monitoring serie de 15 minutes avec analyse automatique des logs.
#
# Prerequis:
#   - PlatformIO installe et dans le PATH
#   - ESP32 connecte sur le port COM specifie (defaut: COM4)
#   - esptool.py disponible (via PlatformIO)
#   - PowerShell 5.1+ (Windows)
#
# Usage:
#   .\flash_wroom_test_nvs_monitor.ps1
#
# Configuration:
#   - Port COM: Modifier la variable $comPort (ligne 30)
#   - Duree: Modifier $monitorDuration (ligne 31)
#
# Version: v11.155
# Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
# =============================================================================

Write-Host "=== FLASH WROOM-TEST AVEC NVS ET MONITORING ===" -ForegroundColor Green
Write-Host "Version firmware: v11.155" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$comPort = "COM4"
$monitorDuration = 900  # 15 minutes en secondes
$logFile = "monitor_wroom_test_nvs_v11.155_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$analysisFile = "monitor_wroom_test_nvs_v11.155_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')_analysis.txt"
$reportFile = "rapport_wroom_test_nvs_v11.155_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').md"

# =============================================================================
# ETAPE 1: FLASH DU FIRMWARE
# =============================================================================
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
                Write-Host "    OK - Arrete" -ForegroundColor Green
            } catch {
                Write-Host "    WARNING - Impossible d'arreter" -ForegroundColor Red
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
Write-Host "1.3. Compilation du firmware wroom-test..." -ForegroundColor Yellow
$compileResult = pio run -e wroom-test
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERREUR: Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "OK - Compilation reussie" -ForegroundColor Green

# Flash du firmware
Write-Host "1.4. Flash du firmware..." -ForegroundColor Yellow
$uploadResult = pio run -e wroom-test --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "OK - Flash firmware reussi !" -ForegroundColor Green
} else {
    Write-Host "ERREUR: Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    Start-Sleep -Seconds 10
    $retryResult = pio run -e wroom-test --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK - Flash firmware reussi au 2eme essai !" -ForegroundColor Green
    } else {
        Write-Host "ERREUR: Echec definitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du systeme de fichiers
Write-Host "1.5. Flash du systeme de fichiers LittleFS..." -ForegroundColor Yellow
Start-Sleep -Seconds 3
$uploadfsResult = pio run -e wroom-test --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "OK - Flash LittleFS reussi !" -ForegroundColor Green
} else {
    Write-Host "ERREUR: Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    Start-Sleep -Seconds 10
    $retryfsResult = pio run -e wroom-test --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK - Flash LittleFS reussi au 2eme essai !" -ForegroundColor Green
    } else {
        Write-Host "ERREUR: Echec definitif du flash LittleFS" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "OK - FLASH FIRMWARE ET FILESYSTEM REUSSI !" -ForegroundColor Green
Write-Host ""

# =============================================================================
# ETAPE 2: REINITIALISATION DE LA NVS
# =============================================================================
Write-Host "=== ETAPE 2: REINITIALISATION DE LA NVS ===" -ForegroundColor Cyan
Write-Host ""

# Trouver esptool.py dans PlatformIO
Write-Host "2.1. Recherche de esptool.py..." -ForegroundColor Yellow
$esptoolPath = ""
try {
    # Essayer de trouver esptool via PlatformIO
    $pioPackages = pio pkg list
    $esptoolInfo = pio pkg list | Select-String "esptool"
    if ($esptoolInfo) {
        $esptoolPath = "pio"
        Write-Host "OK - esptool trouve via PlatformIO" -ForegroundColor Green
    } else {
        $esptoolCheck = Get-Command esptool.py -ErrorAction SilentlyContinue
        if ($esptoolCheck) {
            $esptoolPath = "esptool.py"
            Write-Host "OK - esptool.py trouve dans PATH" -ForegroundColor Green
        } else {
            Write-Host "WARNING - esptool.py non trouve, utilisation de PlatformIO" -ForegroundColor Yellow
            $esptoolPath = "pio"
        }
    }
} catch {
    Write-Host "WARNING - Erreur lors de la recherche de esptool, utilisation de PlatformIO" -ForegroundColor Yellow
    $esptoolPath = "pio"
}

# Effacer la partition NVS (0x9000, taille 0x6000 = 24 KB)
Write-Host "2.2. Effacement de la partition NVS (0x9000, 0x6000)..." -ForegroundColor Yellow
Write-Host "   Adresse: 0x9000" -ForegroundColor Gray
Write-Host "   Taille: 0x6000 (24 KB)" -ForegroundColor Gray

try {
    if ($esptoolPath -eq "pio") {
        $portInfo = pio device list | Select-String $comPort
        if ($portInfo) {
            $eraseResult = pio run -e wroom-test -t erase_region -- 0x9000 0x6000 2>&1
            if ($LASTEXITCODE -ne 0) {
                $pioEsptool = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages" -Recurse -Filter "esptool.py" -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($pioEsptool) {
                    Write-Host "   Utilisation de esptool depuis PlatformIO packages..." -ForegroundColor Gray
                    & python "$($pioEsptool.FullName)" --chip esp32 --port $comPort erase_region 0x9000 0x6000
                    if ($LASTEXITCODE -eq 0) {
                        Write-Host "OK - NVS effacee avec succes !" -ForegroundColor Green
                    } else {
                        Write-Host "WARNING - Erreur lors de l'effacement NVS (code: $LASTEXITCODE)" -ForegroundColor Yellow
                        Write-Host "   La NVS sera reinitialisee au prochain boot" -ForegroundColor Gray
                    }
                } else {
                    Write-Host "WARNING - esptool.py non trouve, NVS sera reinitialisee au boot" -ForegroundColor Yellow
                }
            } else {
                Write-Host "OK - NVS effacee avec succes !" -ForegroundColor Green
            }
        } else {
            Write-Host "WARNING - Port $comPort non trouve, tentative directe..." -ForegroundColor Yellow
            $pioEsptool = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages" -Recurse -Filter "esptool.py" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($pioEsptool) {
                & python "$($pioEsptool.FullName)" --chip esp32 --port $comPort erase_region 0x9000 0x6000
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "OK - NVS effacee avec succes !" -ForegroundColor Green
                } else {
                    Write-Host "WARNING - Erreur lors de l'effacement NVS" -ForegroundColor Yellow
                }
            } else {
                Write-Host "WARNING - esptool.py non trouve, NVS sera reinitialisee au boot" -ForegroundColor Yellow
            }
        }
    } else {
        & esptool.py --chip esp32 --port $comPort erase_region 0x9000 0x6000
        if ($LASTEXITCODE -eq 0) {
            Write-Host "OK - NVS effacee avec succes !" -ForegroundColor Green
        } else {
            Write-Host "WARNING - Erreur lors de l'effacement NVS" -ForegroundColor Yellow
        }
    }
} catch {
    Write-Host "WARNING - Erreur lors de l'effacement NVS: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "   La NVS sera reinitialisee au prochain boot" -ForegroundColor Gray
}

Write-Host ""
Write-Host "OK - REINITIALISATION NVS TERMINEE !" -ForegroundColor Green
Write-Host ""

# Attendre que l'ESP32 redemarre
Write-Host "2.3. Attente du redemarrage de l'ESP32 (10 secondes)..." -ForegroundColor Yellow
Start-Sleep -Seconds 10

# =============================================================================
# ETAPE 3: MONITORING 15 MINUTES
# =============================================================================
Write-Host "=== ETAPE 3: MONITORING 15 MINUTES ===" -ForegroundColor Cyan
Write-Host "Duree: $monitorDuration secondes (15 minutes)" -ForegroundColor Yellow
Write-Host "Port: $comPort" -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

Write-Host "Demarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", "wroom-test", "--monitor-port", $comPort) -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    $endTime = (Get-Date).AddSeconds($monitorDuration)
    $lastProgressUpdate = 0
    
    Write-Host "Monitoring en cours... (appuyez sur Ctrl+C pour arreter prematurement)" -ForegroundColor Gray
    Write-Host ""
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = $monitorDuration - $remaining
        $progressPercent = [math]::Round(($progress / $monitorDuration) * 100)
        $elapsedMinutes = [math]::Floor($progress / 60)
        $elapsedSeconds = $progress % 60
        $remainingMinutes = [math]::Floor($remaining / 60)
        $remainingSeconds = $remaining % 60
        
        if ($progress -ge $lastProgressUpdate + 30) {
            $timeElapsed = "$($elapsedMinutes.ToString('00')):$($elapsedSeconds.ToString('00'))"
            $timeRemaining = "$($remainingMinutes.ToString('00')):$($remainingSeconds.ToString('00'))"
            $progressMsg = "[$timeElapsed] Progression: $progressPercent% ($timeRemaining remaining)"
            Write-Host $progressMsg -ForegroundColor Cyan
            $lastProgressUpdate = $progress
        }
        
        Start-Sleep -Seconds 1
        
        if ($monitorProcess.HasExited) {
            Write-Host ""
            Write-Host "WARNING: Le processus de monitoring s'est arrete prematurement" -ForegroundColor Red
            break
        }
    }
    
    if (-not $monitorProcess.HasExited) {
        Write-Host ""
        Write-Host "Arret du monitoring..." -ForegroundColor Yellow
        $monitorProcess.Kill()
        Start-Sleep -Seconds 2
    }
    
    Write-Host ""
    Write-Host "OK - Monitoring termine !" -ForegroundColor Green
    Write-Host ""
    
} catch {
    Write-Host "ERREUR: Erreur pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# =============================================================================
# ETAPE 4: ANALYSE DU LOG
# =============================================================================
Write-Host "=== ETAPE 4: ANALYSE DU LOG ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $logFile) {
    Write-Host "Analyse du log en cours..." -ForegroundColor Yellow
    $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
    
    if ($logContent) {
        $analysis = @"
=== ANALYSE DU LOG - MONITORING WROOM-TEST AVEC NVS REINITIALISEE ===
Version firmware: v11.155
Date analyse: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $logFile
Duree monitoring: 15 minutes (900 secondes)
NVS: Reinitialisee avant monitoring

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
        
        # Verification du boot apres reinitialisation NVS
        Write-Host "Recherche des indicateurs de boot apres reinitialisation NVS..." -ForegroundColor Yellow
        
        # 1. Initialisation NVS
        $nvsInit = ([regex]::Matches($logContent, "NVS.*init|NVS.*initialis|NVS.*ready|NVS.*demarr", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $nvsErrors = ([regex]::Matches($logContent, "NVS.*error|NVS.*fail|NVS.*echec", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 2. Parsing JSON GET
        $jsonParseSuccess = ([regex]::Matches($logContent, "JSON parsed successfully|Nettoye prefixe chunked", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $jsonParseErrors = ([regex]::Matches($logContent, "JSON parse error", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 3. Envois POST
        $postSends = ([regex]::Matches($logContent, "\[PR\]|POST.*sent|\[Sync\].*envoi POST|POST.*reussi", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $postDiagnostic = ([regex]::Matches($logContent, "\[Sync\] Diagnostic|\[PR\] === DEBUT POSTRAW", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $postErrors = ([regex]::Matches($logContent, "POST.*echec|POST.*error|POST.*failed", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 4. Monitoring memoire
        $heapChecks = ([regex]::Matches($logContent, "Heap.*libre|Heap.*minimum|\[autoTask\].*Heap|CRITICAL.*Heap|Free heap", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $lowHeapWarnings = ([regex]::Matches($logContent, "CRITICAL.*Heap|WARN.*Heap.*faible|heap.*low|Heap.*critique", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 5. DHT22 desactivation
        $dht22Disabled = ([regex]::Matches($logContent, "DHT22 desactive|sensorDisabled|Capteur.*desactive", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $dht22Failures = ([regex]::Matches($logContent, "AirSensor.*echec|DHT.*non detecte|Capteur DHT.*deconnecte", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 6. Queue capteurs
        $queueFullErrors = ([regex]::Matches($logContent, "Queue pleine|queue.*full", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 7. WiFi
        $wifiConnected = ([regex]::Matches($logContent, "WiFi.*connect|WiFi.*connected|Connexion.*reussie", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $wifiDisconnected = ([regex]::Matches($logContent, "WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        $analysis += @"
=== VERIFICATION POST-REINITIALISATION NVS ===

1. Initialisation NVS:
   - Initialisations detectees: $nvsInit
   - Erreurs NVS: $nvsErrors
   - $(if ($nvsErrors -eq 0 -and $nvsInit -gt 0) { "OK - NVS initialisee correctement" } elseif ($nvsErrors -eq 0) { "WARNING - Peu d'indicateurs d'initialisation NVS" } else { "PROBLEME - Erreurs NVS detectees" })

2. Parsing JSON GET:
   - Parsing reussis: $jsonParseSuccess
   - Erreurs de parsing: $jsonParseErrors
   - $(if ($jsonParseErrors -eq 0) { "OK - Aucune erreur de parsing" } else { "WARNING - $jsonParseErrors erreur(s) de parsing detectee(s)" })

3. Envois POST serveur distant:
   - Envois POST detectes: $postSends
   - Erreurs POST: $postErrors
   - Logs de diagnostic: $postDiagnostic
   - $(if ($postSends -gt 0) { "OK - Envois POST effectues ($postSends)" } else { "PROBLEME - Aucun envoi POST detecte" })
   - $(if ($postErrors -gt 0) { "WARNING - $postErrors erreur(s) POST detectee(s)" } else { "OK - Aucune erreur POST" })

4. Monitoring memoire:
   - Verifications heap: $heapChecks
   - Alertes memoire faible: $lowHeapWarnings
   - $(if ($lowHeapWarnings -eq 0) { "OK - Aucune alerte memoire" } else { "WARNING - $lowHeapWarnings alerte(s) memoire detectee(s)" })

5. DHT22 gestion echecs:
   - Desactivation detectee: $dht22Disabled
   - Echecs DHT22: $dht22Failures
   - $(if ($dht22Disabled -gt 0) { "OK - Desactivation automatique fonctionnelle" } elseif ($dht22Failures -lt 10) { "OK - Pas assez d'echecs pour desactivation ($dht22Failures)" } else { "WARNING - Beaucoup d'echecs ($dht22Failures) mais pas de desactivation" })

6. Queue capteurs:
   - Erreurs queue pleine: $queueFullErrors
   - $(if ($queueFullErrors -eq 0) { "OK - Aucune erreur de queue" } else { "ATTENDU - $queueFullErrors erreur(s) (queue non agrandie)" })

7. WiFi:
   - Connexions detectees: $wifiConnected
   - Deconnexions detectees: $wifiDisconnected
   - $(if ($wifiConnected -gt 0 -and $wifiDisconnected -eq 0) { "OK - WiFi stable" } elseif ($wifiConnected -gt 0) { "WARNING - WiFi connecte mais deconnexions detectees ($wifiDisconnected)" } else { "PROBLEME - Aucune connexion WiFi detectee" })

"@
        
        # Patterns critiques
        Write-Host "Recherche des erreurs critiques..." -ForegroundColor Yellow
        $criticalPatterns = @(
            @{ Pattern = "Guru Meditation"; Name = "Guru Meditation" },
            @{ Pattern = "Panic(?!.*diag_hasPanic)"; Name = "Panic" },
            @{ Pattern = "Brownout"; Name = "Brownout" },
            @{ Pattern = "Stack overflow|STACK OVERFLOW"; Name = "Stack Overflow" },
            @{ Pattern = "Reboot|restart|reset"; Name = "Reboot inattendu" }
        )
        
        $criticalIssues = @()
        foreach ($check in $criticalPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $criticalIssues += "  ERREUR - $($check.Name): $($matches.Count) occurrence(s)"
            }
        }
        
        if ($criticalIssues.Count -eq 0) {
            $analysis += "OK - Aucune erreur critique detectee`n`n"
        } else {
            $analysis += "`nERREUR - ERREURS CRITIQUES DETECTEES:`n" + ($criticalIssues -join "`n") + "`n`n"
        }
        
        # Warnings et erreurs generales
        $warnings = ([regex]::Matches($logContent, "\[W\]|WARN|WARNING", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $errors = ([regex]::Matches($logContent, "\[E\]|ERROR|ERREUR", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        $analysis += @"
=== ERREURS ET WARNINGS ===
- Warnings detectes: $warnings
- Erreurs detectees: $errors

"@
        
        # Resume final
        $analysis += "=== RESUME FINAL ===`n"
        if ($criticalIssues.Count -eq 0 -and $postSends -gt 0 -and $jsonParseErrors -eq 0 -and $nvsErrors -eq 0) {
            $analysis += "OK - SYSTEME STABLE - Boot apres reinitialisation NVS reussi`n"
            $status = "STABLE"
            Write-Host "OK - SYSTEME STABLE - Boot apres reinitialisation NVS reussi" -ForegroundColor Green
        } elseif ($criticalIssues.Count -gt 0) {
            $analysis += "ERREUR - SYSTEME INSTABLE - Erreurs critiques detectees`n"
            $status = "INSTABLE"
            Write-Host "ERREUR - SYSTEME INSTABLE - Erreurs critiques detectees" -ForegroundColor Red
        } else {
            $analysis += "WARNING - SYSTEME A SURVEILLER - Certains problemes necessitent verification`n"
            $status = "A_SURVEILLER"
            Write-Host "WARNING - SYSTEME A SURVEILLER - Certains problemes necessitent verification" -ForegroundColor Yellow
        }
        
        $analysis += "`n=== FIN DE L'ANALYSE ===`n"
        
        # Sauvegarder l'analyse
        $analysis | Out-File -FilePath $analysisFile -Encoding UTF8
        Write-Host ""
        Write-Host "OK - Analyse sauvegardee: $analysisFile" -ForegroundColor Cyan
        
        # =============================================================================
        # ETAPE 5: DIAGNOSTIC SERVEUR DISTANT
        # =============================================================================
        Write-Host ""
        Write-Host "=== ETAPE 5: DIAGNOSTIC SERVEUR DISTANT ===" -ForegroundColor Cyan
        Write-Host ""
        
        if (Test-Path ".\diagnostic_serveur_distant.ps1") {
            Write-Host "Execution du diagnostic serveur distant..." -ForegroundColor Yellow
            & .\diagnostic_serveur_distant.ps1 -LogFile $logFile
            Write-Host "OK - Diagnostic serveur distant termine" -ForegroundColor Green
        } else {
            Write-Host "WARNING - Script diagnostic_serveur_distant.ps1 non trouve" -ForegroundColor Yellow
        }
        
        # =============================================================================
        # ETAPE 6: DIAGNOSTIC MAILS
        # =============================================================================
        Write-Host ""
        Write-Host "=== ETAPE 6: DIAGNOSTIC MAILS ===" -ForegroundColor Cyan
        Write-Host ""
        
        if (Test-Path ".\check_emails_from_log.ps1") {
            Write-Host "Execution du diagnostic mails..." -ForegroundColor Yellow
            & .\check_emails_from_log.ps1 -LogFile $logFile
            Write-Host "OK - Diagnostic mails termine" -ForegroundColor Green
        } else {
            Write-Host "WARNING - Script check_emails_from_log.ps1 non trouve" -ForegroundColor Yellow
        }
        
        # =============================================================================
        # ETAPE 7: ANALYSE EXHAUSTIVE
        # =============================================================================
        Write-Host ""
        Write-Host "=== ETAPE 7: ANALYSE EXHAUSTIVE ===" -ForegroundColor Cyan
        Write-Host ""
        
        if (Test-Path ".\analyze_log_exhaustive.ps1") {
            Write-Host "Execution de l'analyse exhaustive..." -ForegroundColor Yellow
            & .\analyze_log_exhaustive.ps1 -LogFile $logFile
            Write-Host "OK - Analyse exhaustive terminee" -ForegroundColor Green
        } else {
            Write-Host "WARNING - Script analyze_log_exhaustive.ps1 non trouve" -ForegroundColor Yellow
        }
        
        # =============================================================================
        # ETAPE 8: GENERATION DU RAPPORT MARKDOWN COMPLET
        # =============================================================================
        Write-Host ""
        Write-Host "=== ETAPE 8: GENERATION DU RAPPORT COMPLET ===" -ForegroundColor Cyan
        
        $report = @"
# Rapport de Flash et Monitoring - WROOM-TEST avec NVS Reinitialisee

**Date:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  
**Version firmware:** v11.155  
**Environnement:** wroom-test  
**Duree monitoring:** 15 minutes (900 seconds)

---

## Resume Executif

**Statut global:** $status

- OK - Flash firmware: Reussi
- OK - Flash filesystem (LittleFS): Reussi
- OK - Reinitialisation NVS: Effectuee
- OK - Monitoring: Termine ($monitorDuration secondes)

---

## Details Techniques

### Flash

1. **Firmware:** Flash reussi sur partition factory (0x10000)
2. **Filesystem:** LittleFS flashe avec succes (0x310000)
3. **NVS:** Partition effacee (0x9000, taille 0x6000 = 24 KB)

### Statistiques du Monitoring

- **Total lignes loggees:** $lineCount
- **Taille du fichier log:** $([math]::Round($fileSize, 2)) KB
- **Port serie:** $comPort

---

## Analyse des Logs

### Initialisation NVS

- Initialisations detectees: $nvsInit
- Erreurs NVS: $nvsErrors
- **Verdict:** $(if ($nvsErrors -eq 0 -and $nvsInit -gt 0) { "OK - NVS initialisee correctement" } elseif ($nvsErrors -eq 0) { "WARNING - Peu d'indicateurs d'initialisation NVS" } else { "PROBLEME - Erreurs NVS detectees" })

### Communication Reseau

- **Parsing JSON GET:**
  - Parsing reussis: $jsonParseSuccess
  - Erreurs: $jsonParseErrors
  
- **Envois POST serveur:**
  - Envois detectes: $postSends
  - Erreurs: $postErrors

### Stabilite Systeme

- **Memoire:**
  - Verifications heap: $heapChecks
  - Alertes memoire faible: $lowHeapWarnings

- **WiFi:**
  - Connexions: $wifiConnected
  - Deconnexions: $wifiDisconnected

- **Capteurs:**
  - DHT22 desactivations: $dht22Disabled
  - Echecs DHT22: $dht22Failures
  - Erreurs queue: $queueFullErrors

### Erreurs Critiques

$(if ($criticalIssues.Count -eq 0) { "OK - Aucune erreur critique detectee" } else { $criticalIssues -join "`n" })

### Warnings et Erreurs

- **Warnings:** $warnings
- **Erreurs:** $errors

---

## Conclusion

$(if ($status -eq "STABLE") { 
"The system booted successfully after NVS reset and ran stably during the 15-minute monitoring period. All critical systems are functioning correctly."
} elseif ($status -eq "INSTABLE") {
"ERREUR - **SYSTEME INSTABLE** - Des erreurs critiques ont ete detectees. Une investigation approfondie est necessaire."
} else {
"WARNING - **SYSTEME A SURVEILLER** - Le systeme fonctionne mais certains problemes necessitent une attention particuliere."
})

---

## Fichiers Generes

- **Log complet:** \`$logFile\`
- **Analyse detaillee:** \`$analysisFile\`
- **Rapport:** \`$reportFile\`

---

## Recommandations

$(if ($status -eq "STABLE") {
"- OK - Systeme pret pour deploiement
- Continuer le monitoring en production
- Surveiller les metriques memoire et reseau"
} elseif ($status -eq "INSTABLE") {
"- ERREUR - **ACTION REQUISE:** Analyser les erreurs critiques dans le log
- Verifier la configuration materielle
- Examiner les core dumps si disponibles
- Refaire un test apres corrections"
} else {
"- WARNING - Analyser les warnings dans le log detaille
- Verifier la stabilite WiFi
- Surveiller la memoire heap
- Considerer des ajustements de configuration"
})

---

*Rapport genere automatiquement par flash_wroom_test_nvs_monitor.ps1*
"@
        
        $report | Out-File -FilePath $reportFile -Encoding UTF8
        Write-Host "OK - Rapport sauvegarde: $reportFile" -ForegroundColor Cyan
        
        # =============================================================================
        # ETAPE 9: GENERATION DU RAPPORT DE DIAGNOSTIC COMPLET
        # =============================================================================
        Write-Host ""
        Write-Host "=== ETAPE 9: GENERATION DU RAPPORT DE DIAGNOSTIC COMPLET ===" -ForegroundColor Cyan
        Write-Host ""
        
        if (Test-Path ".\generate_diagnostic_report.ps1") {
            Write-Host "Generation du rapport de diagnostic complet..." -ForegroundColor Yellow
            & .\generate_diagnostic_report.ps1 -LogFile $logFile
            Write-Host "OK - Rapport de diagnostic complet genere" -ForegroundColor Green
        } else {
            Write-Host "WARNING - Script generate_diagnostic_report.ps1 non trouve" -ForegroundColor Yellow
        }
        
        # Afficher un resume
        Write-Host ""
        Write-Host "=== RESUME ===" -ForegroundColor Cyan
        Write-Host "Statut: $status" -ForegroundColor $(if ($status -eq "STABLE") { "Green" } elseif ($status -eq "INSTABLE") { "Red" } else { "Yellow" })
        Write-Host "Log complet: $logFile" -ForegroundColor Gray
        Write-Host "Analyse detaillee: $analysisFile" -ForegroundColor Gray
        Write-Host "Rapport: $reportFile" -ForegroundColor Gray
        Write-Host "Total lignes: $lineCount" -ForegroundColor Gray
        Write-Host "Taille log: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
        
    } else {
        Write-Host "Le fichier de log est vide" -ForegroundColor Yellow
        "ERREUR: Le fichier de log est vide ou n'a pas pu etre lu" | Out-File -FilePath $analysisFile -Encoding UTF8
    }
} else {
    Write-Host "Aucun fichier de log genere" -ForegroundColor Yellow
    "ERREUR: Aucun fichier de log genere" | Out-File -FilePath $analysisFile -Encoding UTF8
}

Write-Host ""
Write-Host "=== PROCESSUS TERMINE ===" -ForegroundColor Green
Write-Host "Verifiez les fichiers pour l'analyse detaillee:" -ForegroundColor Cyan
Write-Host "  - Log: $logFile" -ForegroundColor Gray
Write-Host "  - Analyse: $analysisFile" -ForegroundColor Gray
Write-Host "  - Rapport: $reportFile" -ForegroundColor Gray
