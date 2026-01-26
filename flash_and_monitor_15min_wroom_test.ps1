# =============================================================================
# Script de flash et monitoring 15 minutes - wroom-test
# =============================================================================
# Description:
#   Flash le firmware ESP32-WROOM (wroom-test) et effectue un monitoring
#   série de 15 minutes avec analyse automatique des logs.
#
# Prérequis:
#   - PlatformIO installé et dans le PATH
#   - ESP32 connecté sur le port COM spécifié (défaut: COM4)
#   - PowerShell 5.1+ (Windows)
#
# Usage:
#   .\flash_and_monitor_15min_wroom_test.ps1
#
# Configuration:
#   - Port COM: Modifier la variable $comPort (ligne 10)
#   - Durée: Modifier $monitorDuration (ligne 11)
#
# Version: v11.156 (post-corrections)
# Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
# =============================================================================

Write-Host "=== FLASH ET MONITORING 15 MINUTES - WROOM-TEST ===" -ForegroundColor Green
Write-Host "Version firmware: v11.156 (post-corrections)" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$comPort = "COM4"
$monitorDuration = 900  # 15 minutes en secondes
$logFile = "monitor_15min_v11.156_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$analysisFile = "monitor_15min_v11.156_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')_analysis.txt"

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
Start-Sleep -Seconds 3
$uploadfsResult = pio run -e wroom-test --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash LittleFS réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash LittleFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
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
# ÉTAPE 2: MONITORING 15 MINUTES
# =============================================================================
Write-Host "=== ÉTAPE 2: MONITORING 15 MINUTES ===" -ForegroundColor Cyan
Write-Host "Durée: $monitorDuration secondes (15 minutes)" -ForegroundColor Yellow
Write-Host "Port: $comPort" -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

Write-Host "📋 Démarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", "wroom-test", "--monitor-port", $comPort) -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
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
        $remainingMinutes = [math]::Floor($remaining / 60)
        $remainingSeconds = $remaining % 60
        
        if ($progress -ge $lastProgressUpdate + 30) {
            $timeElapsed = "$($elapsedMinutes.ToString('00')):$($elapsedSeconds.ToString('00'))"
            $timeRemaining = "$($remainingMinutes.ToString('00')):$($remainingSeconds.ToString('00'))"
            Write-Host "[$timeElapsed] Progression: $progressPercent% ($timeRemaining restantes)" -ForegroundColor Cyan
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
    Write-Host "OK: Monitoring termine !" -ForegroundColor Green
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
    Write-Host "Analyse du log en cours..." -ForegroundColor Yellow
    $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
    
    if ($logContent) {
        $analysis = @"
=== ANALYSE DU LOG - MONITORING 15 MINUTES ===
Version firmware: v11.156 (post-corrections)
Date analyse: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $logFile
Durée monitoring: 15 minutes (900 secondes)

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
        
        # Vérification des corrections apportées (v11.156)
        Write-Host "Recherche des indicateurs des corrections v11.156..." -ForegroundColor Yellow
        
        # 1. Parsing JSON GET corrigé
        $jsonParseSuccess = ([regex]::Matches($logContent, "JSON parsed successfully|Nettoyé préfixe chunked", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $jsonParseErrors = ([regex]::Matches($logContent, "JSON parse error", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 2. Envois POST
        $postSends = ([regex]::Matches($logContent, "\[PR\]|POST.*sent|\[Sync\].*envoi POST", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $postDiagnostic = ([regex]::Matches($logContent, "\[Sync\] Diagnostic|\[PR\] === DÉBUT POSTRAW", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 3. Monitoring mémoire
        $heapChecks = ([regex]::Matches($logContent, "Heap.*libre|Heap.*minimum|\[autoTask\].*Heap|CRITICAL.*Heap", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $lowHeapWarnings = ([regex]::Matches($logContent, "CRITICAL.*Heap|WARN.*Heap.*faible|heap.*low", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 4. DHT22 désactivation
        $dht22Disabled = ([regex]::Matches($logContent, "DHT22 désactivé|sensorDisabled", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        $dht22Failures = ([regex]::Matches($logContent, "AirSensor.*échec|DHT.*non détecté", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        # 5. Queue capteurs (problème identifié mais non corrigé)
        $queueFullErrors = ([regex]::Matches($logContent, "Queue pleine|queue.*full", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
        
        $analysis += @"
=== VERIFICATION DES CORRECTIONS v11.156 ===

1. Parsing JSON GET:
   - Parsing réussis: $jsonParseSuccess
   - Erreurs de parsing: $jsonParseErrors
   - $(if ($jsonParseErrors -eq 0) { "✅ OK - Aucune erreur de parsing" } else { "⚠️ WARNING - Erreurs de parsing encore présentes" })

2. Envois POST serveur distant:
   - Envois POST détectés: $postSends
   - Logs de diagnostic: $postDiagnostic
   - $(if ($postSends -gt 0) { "✅ OK - Envois POST effectués" } else { "❌ PROBLÈME - Aucun envoi POST détecté" })

3. Monitoring mémoire:
   - Vérifications heap: $heapChecks
   - Alertes mémoire faible: $lowHeapWarnings
   - $(if ($lowHeapWarnings -eq 0) { "✅ OK - Aucune alerte mémoire" } else { "⚠️ WARNING - Alertes mémoire détectées" })

4. DHT22 gestion échecs:
   - Désactivation détectée: $dht22Disabled
   - Échecs DHT22: $dht22Failures
   - $(if ($dht22Disabled -gt 0) { "✅ OK - Désactivation automatique fonctionnelle" } elseif ($dht22Failures -lt 10) { "✅ OK - Pas assez d'échecs pour désactivation" } else { "⚠️ WARNING - Beaucoup d'échecs mais pas de désactivation" })

5. Queue capteurs (non corrigée - exclue du plan):
   - Erreurs queue pleine: $queueFullErrors
   - $(if ($queueFullErrors -eq 0) { "✅ OK - Aucune erreur de queue" } else { "⚠️ ATTENDU - $queueFullErrors erreurs (queue non agrandie)" })

"@
        
        # Patterns critiques
        Write-Host "Recherche des erreurs critiques..." -ForegroundColor Yellow
        $criticalPatterns = @(
            @{ Pattern = "Guru Meditation"; Name = "Guru Meditation" },
            @{ Pattern = "Panic(?!.*diag_hasPanic)"; Name = "Panic" },
            @{ Pattern = "Brownout"; Name = "Brownout" },
            @{ Pattern = "Stack overflow|STACK OVERFLOW"; Name = "Stack Overflow" }
        )
        
        $criticalIssues = @()
        foreach ($check in $criticalPatterns) {
            $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($matches.Count -gt 0) {
                $criticalIssues += "  ❌ $($check.Name): $($matches.Count) occurrence(s)"
            }
        }
        
        if ($criticalIssues.Count -eq 0) {
            $analysis += "✅ Aucune erreur critique détectée`n`n"
        } else {
            $analysis += "`n❌ ERREURS CRITIQUES DÉTECTÉES:`n" + ($criticalIssues -join "`n") + "`n`n"
        }
        
        # Résumé final
        $analysis += "=== RÉSUMÉ FINAL ===`n"
        if ($criticalIssues.Count -eq 0 -and $postSends -gt 0 -and $jsonParseErrors -eq 0) {
            $analysis += "✅ SYSTÈME STABLE - Corrections v11.156 efficaces`n"
            Write-Host "✅ SYSTÈME STABLE - Corrections v11.156 efficaces" -ForegroundColor Green
        } elseif ($criticalIssues.Count -gt 0) {
            $analysis += "❌ SYSTÈME INSTABLE - Erreurs critiques détectées`n"
            Write-Host "❌ SYSTÈME INSTABLE - Erreurs critiques détectées" -ForegroundColor Red
        } else {
            $analysis += "⚠️ SYSTÈME À SURVEILLER - Certaines corrections nécessitent vérification`n"
            Write-Host "⚠️ SYSTÈME À SURVEILLER - Certaines corrections nécessitent vérification" -ForegroundColor Yellow
        }
        
        $analysis += "`n=== FIN DE L'ANALYSE ===`n"
        
        # Sauvegarder l'analyse
        $analysis | Out-File -FilePath $analysisFile -Encoding UTF8
        Write-Host ""
        Write-Host "Analyse complete sauvegardee: $analysisFile" -ForegroundColor Cyan
        
        # Afficher un résumé
        Write-Host ""
        Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
        Write-Host "Log complet: $logFile" -ForegroundColor Gray
        Write-Host "Analyse detaillee: $analysisFile" -ForegroundColor Gray
        Write-Host "Total lignes: $lineCount" -ForegroundColor Gray
        Write-Host "Taille log: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
        
    } else {
        Write-Host "Le fichier de log est vide" -ForegroundColor Yellow
        "❌ ERREUR: Le fichier de log est vide ou n'a pas pu être lu" | Out-File -FilePath $analysisFile -Encoding UTF8
    }
} else {
    Write-Host "Aucun fichier de log genere" -ForegroundColor Yellow
    "❌ ERREUR: Aucun fichier de log généré" | Out-File -FilePath $analysisFile -Encoding UTF8
}

Write-Host ""
Write-Host "=== PROCESSUS TERMINÉ ===" -ForegroundColor Green
Write-Host "Vérifiez les fichiers pour l'analyse détaillée:" -ForegroundColor Cyan
Write-Host "  - Log: $logFile" -ForegroundColor Gray
Write-Host "  - Analyse: $analysisFile" -ForegroundColor Gray
