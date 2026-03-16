# Script de flash, monitoring jusqu'au reboot et analyse complète - wroom-test
param(
    [string]$Port = "COM4",
    [int]$PostRebootDuration = 60,
    [int]$MaxWaitTime = 3600
)

# Version firmware depuis config.h
$configPath = Join-Path $PSScriptRoot "include\config.h"
$fwVersion = "?"
if (Test-Path $configPath) {
    $cfg = Get-Content $configPath -Raw
    if ($cfg -match 'VERSION\s*=\s*"(\d+\.\d+)"') { $fwVersion = "v$($matches[1])" }
}

Write-Host "=== FLASH ET MONITORING JUSQU'AU REBOOT - WROOM-TEST ===" -ForegroundColor Green
Write-Host "Version firmware: $fwVersion" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Durée post-reboot: $PostRebootDuration secondes" -ForegroundColor Yellow
Write-Host "Temps max d'attente: $MaxWaitTime secondes" -ForegroundColor Yellow
Write-Host ""

$logsDir = Join-Path $PSScriptRoot "logs"
if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir -Force | Out-Null }
$logFile = Join-Path $logsDir "flash_monitor_until_reboot_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$analysisFile = Join-Path $logsDir "flash_monitor_until_reboot_analysis_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').txt"

# =============================================================================
# ÉTAPE 1: FLASH DU FIRMWARE
# =============================================================================
Write-Host "=== ÉTAPE 1: FLASH DU FIRMWARE ===" -ForegroundColor Cyan
Write-Host ""

. (Join-Path $PSScriptRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $Port

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
# ÉTAPE 2: MONITORING JUSQU'AU REBOOT
# =============================================================================
Write-Host "=== ÉTAPE 2: MONITORING JUSQU'AU REBOOT ===" -ForegroundColor Cyan
Write-Host "En attente d'un reboot..." -ForegroundColor Yellow
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host ""

# Patterns de détection de reboot
$rebootPatterns = @(
    "=== BOOT FFP5CS",
    "rst:0x",
    "RST:",
    "reset reason",
    "Redémarrage",
    "Démarrage FFP5CS",
    "App start v",
    "Guru Meditation",
    "Panic",
    "Brownout",
    "Core dump"
)

$rebootDetected = $false
$rebootTime = $null
$startTime = Get-Date
$logContent = ""

Write-Host "📡 Démarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

# Créer un job pour capturer les logs
$logJob = Start-Job -ScriptBlock {
    param($port, $baud, $outputFile)
    
    $content = ""
    $fileStream = [System.IO.StreamWriter]::new($outputFile, $false, [System.Text.Encoding]::UTF8)
    
    try {
        $portObj = New-Object System.IO.Ports.SerialPort($port, $baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $portObj.ReadTimeout = 1000
        $portObj.Open()
        
        while ($true) {
            if ($portObj.BytesToRead -gt 0) {
                $line = $portObj.ReadLine()
                $content += $line + "`n"
                $fileStream.WriteLine($line)
                $fileStream.Flush()
                Write-Output $line
            }
            Start-Sleep -Milliseconds 50
        }
    } catch {
        Write-Error "Erreur port série: $_"
    } finally {
        if ($portObj -and $portObj.IsOpen) {
            $portObj.Close()
        }
        $fileStream.Close()
    }
} -ArgumentList $Port, 115200, $logFile

# Attendre la détection d'un reboot
$checkInterval = 2  # Vérifier toutes les 2 secondes

while (-not $rebootDetected) {
    Start-Sleep -Seconds $checkInterval
    
    # Vérifier si le job est encore actif
    if ($logJob.State -eq "Failed" -or $logJob.State -eq "Completed") {
        Write-Host "⚠️ Le job de monitoring s'est arrêté (État: $($logJob.State))" -ForegroundColor Yellow
        break
    }
    
    # Lire les nouvelles lignes du log
    $newLines = Receive-Job $logJob -ErrorAction SilentlyContinue
    if ($newLines) {
        foreach ($line in $newLines) {
            $logContent += $line + "`n"
            
            # Vérifier les patterns de reboot
            foreach ($pattern in $rebootPatterns) {
                if ($line -match $pattern) {
                    $rebootDetected = $true
                    $rebootTime = Get-Date
                    Write-Host ""
                    Write-Host "🔄 REBOOT DÉTECTÉ !" -ForegroundColor Red
                    Write-Host "Pattern détecté: $pattern" -ForegroundColor Yellow
                    Write-Host "Ligne: $($line.Trim())" -ForegroundColor Gray
                    Write-Host "Temps écoulé: $([math]::Round((($rebootTime - $startTime).TotalSeconds), 1)) secondes" -ForegroundColor Cyan
                    break
                }
            }
        }
    }
    
    # Vérifier le timeout
    $elapsed = (Get-Date) - $startTime
    if ($elapsed.TotalSeconds -gt $MaxWaitTime) {
        Write-Host ""
        Write-Host "⏱️ Temps maximum atteint ($MaxWaitTime secondes)" -ForegroundColor Yellow
        Write-Host "Aucun reboot détecté pendant cette période" -ForegroundColor Yellow
        break
    }
    
    # Afficher la progression
    $elapsedSeconds = [math]::Round($elapsed.TotalSeconds)
    if ($elapsedSeconds % 10 -eq 0 -and $elapsedSeconds -ne 0) {
        Write-Host "⏱️  Temps écoulé: ${elapsedSeconds}s (reste $([math]::Round($MaxWaitTime - $elapsedSeconds))s)" -ForegroundColor Gray
    }
}

# Si reboot détecté, continuer le monitoring post-reboot
if ($rebootDetected) {
    Write-Host ""
    Write-Host "📊 Continuation du monitoring post-reboot ($PostRebootDuration secondes)..." -ForegroundColor Cyan
    
    $postRebootStart = Get-Date
    $postRebootEnd = $postRebootStart.AddSeconds($PostRebootDuration)
    
    while ((Get-Date) -lt $postRebootEnd) {
        Start-Sleep -Seconds 1
        
        $newLines = Receive-Job $logJob -ErrorAction SilentlyContinue
        if ($newLines) {
            foreach ($line in $newLines) {
                $logContent += $line + "`n"
            }
        }
        
        $remaining = [math]::Round(($postRebootEnd - (Get-Date)).TotalSeconds)
        if ($remaining % 10 -eq 0 -and $remaining -gt 0) {
            Write-Host "⏱️  Post-reboot: ${remaining}s restantes" -ForegroundColor Gray
        }
    }
    
    Write-Host "✅ Monitoring post-reboot terminé" -ForegroundColor Green
}

# Arrêter le job
Write-Host ""
Write-Host "🛑 Arrêt du monitoring..." -ForegroundColor Yellow
Stop-Job $logJob -ErrorAction SilentlyContinue
Remove-Job $logJob -ErrorAction SilentlyContinue

# Lire les dernières lignes
$finalLines = Receive-Job $logJob -ErrorAction SilentlyContinue
if ($finalLines) {
    foreach ($line in $finalLines) {
        $logContent += $line + "`n"
    }
}

# Lire le fichier complet pour analyse
if (Test-Path $logFile) {
    $logLines = Get-Content $logFile -ErrorAction SilentlyContinue
    if (-not $logContent -and $logLines) {
        $logContent = $logLines -join "`n"
    }
}

Write-Host ""
Write-Host "=== ÉTAPE 3: ANALYSE COMPLÈTE DES LOGS ===" -ForegroundColor Green
Write-Host ""

# Appeler le script d'analyse (logique copiée de monitor_until_reboot.ps1)
if (-not $logLines -or $logLines.Count -eq 0) {
    Write-Host "❌ Aucun log capturé" -ForegroundColor Red
    exit 1
}

Write-Host "📊 Analyse de $($logLines.Count) lignes de log..." -ForegroundColor Cyan
Write-Host ""

# Initialiser le rapport d'analyse
$report = @"
========================================
RAPPORT D'ANALYSE - FLASH ET MONITORING JUSQU'AU REBOOT
========================================
Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Port: $Port
Temps jusqu'au reboot: $(if ($rebootDetected) { "$([math]::Round((($rebootTime - $startTime).TotalSeconds), 1)) secondes" } else { "Aucun reboot détecté" })
Durée post-reboot: $PostRebootDuration secondes
Fichier de log: $logFile
Version firmware: $fwVersion
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
        $lineNum = $match.LineNumber
        $contextStart = [Math]::Max(1, $lineNum - 5)
        $contextEnd = [Math]::Min($logLines.Count, $lineNum + 5)
        $context = $logLines[($contextStart-1)..($contextEnd-1)]
        $matchLine = $match.Line.Trim()
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
        $report += "   Contexte:`n"
        foreach ($ctxLine in $context) {
            $marker = if ($ctxLine -eq $match.Line) { ">>> " } else { "    " }
            $report += "   $marker$ctxLine`n"
        }
        $report += "`n"
    }
    if ($guruCount -gt 10) {
        $report += "   ... et $($guruCount - 10) autre(s) occurrence(s)`n"
    }
} else {
    $report += "   ✅ Aucun crash PANIC détecté`n"
}
$report += "`n"

# Brownout
$brownoutMatches = $logLines | Select-String -Pattern "Brownout|BROWNOUT|brownout detector" -AllMatches
$brownoutCount = $brownoutMatches.Count
$report += "2. BROWNOUT (Alimentation): $brownoutCount occurrence(s)`n"
if ($brownoutCount -gt 0) {
    $report += "   ⚠️ CRITIQUE: Problème d'alimentation détecté !`n"
    foreach ($match in $brownoutMatches | Select-Object -First 5) {
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
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
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
    }
} else {
    $report += "   ✅ Aucun core dump détecté`n"
}
$report += "`n"

# Stack overflow
$stackMatches = $logLines | Select-String -Pattern "stack overflow|Stack overflow|STACK|stack.*overflow" -AllMatches
$stackCount = $stackMatches.Count
$report += "4. STACK OVERFLOW: $stackCount occurrence(s)`n"
if ($stackCount -gt 0) {
    $report += "   ⚠️ CRITIQUE: Débordement de pile détecté !`n"
    foreach ($match in $stackMatches | Select-Object -First 5) {
        $lineNum = $match.LineNumber
        $contextStart = [Math]::Max(1, $lineNum - 3)
        $contextEnd = [Math]::Min($logLines.Count, $lineNum + 3)
        $context = $logLines[($contextStart-1)..($contextEnd-1)]
        $matchLine = $match.Line.Trim()
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
        $report += "   Contexte:`n"
        foreach ($ctxLine in $context) {
            $marker = if ($ctxLine -eq $match.Line) { ">>> " } else { "    " }
            $report += "   $marker$ctxLine`n"
        }
    }
} else {
    $report += "   ✅ Aucun stack overflow détecté`n"
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
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
    }
} else {
    $report += "   ✅ Aucun timeout watchdog détecté`n"
}
$report += "`n"

# Timeouts WiFi
$wifiTimeoutMatches = $logLines | Select-String -Pattern "WiFi.*timeout|WIFI.*TIMEOUT|WiFi.*fail|WiFi.*error" -AllMatches
$wifiTimeoutCount = $wifiTimeoutMatches.Count
$report += "2. TIMEOUTS WIFI: $wifiTimeoutCount occurrence(s)`n"
if ($wifiTimeoutCount -gt 0) {
    $report += "   ⚠️ ATTENTION: Timeouts WiFi détectés`n"
    foreach ($match in $wifiTimeoutMatches | Select-Object -First 5) {
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
    }
} else {
    $report += "   ✅ Aucun timeout WiFi détecté`n"
}
$report += "`n"

# Timeouts WebSocket
$wsTimeoutMatches = $logLines | Select-String -Pattern "WebSocket.*timeout|websocket.*TIMEOUT|WebSocket.*fail|WebSocket.*error" -AllMatches
$wsTimeoutCount = $wsTimeoutMatches.Count
$report += "3. TIMEOUTS WEBSOCKET: $wsTimeoutCount occurrence(s)`n"
if ($wsTimeoutCount -gt 0) {
    $report += "   ⚠️ ATTENTION: Timeouts WebSocket détectés`n"
    foreach ($match in $wsTimeoutMatches | Select-Object -First 5) {
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
    }
} else {
    $report += "   ✅ Aucun timeout WebSocket détecté`n"
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
    if ($line -match "free.*heap.*(\d+)|heap.*free.*(\d+)|Free.*heap.*(\d+)|Heap.*free.*(\d+)" -or 
        $line -match "(\d+).*bytes.*free|(\d+).*free.*heap") {
        $value = if ($matches[1]) { [int]$matches[1] } 
                 elseif ($matches[2]) { [int]$matches[2] } 
                 elseif ($matches[3]) { [int]$matches[3] } 
                 elseif ($matches[4]) { [int]$matches[4] }
                 else { 0 }
        if ($value -gt 0 -and $value -lt 1000000) {  # Filtrer les valeurs aberrantes
            $heapValues += $value
        }
    }
}

$report += "1. HEAP (Mémoire libre): $heapCount référence(s)`n"
if ($heapValues.Count -gt 0) {
    $minHeap = ($heapValues | Measure-Object -Minimum).Minimum
    $maxHeap = ($heapValues | Measure-Object -Maximum).Maximum
    $avgHeap = [math]::Round(($heapValues | Measure-Object -Average).Average, 0)
    $report += "   - Minimum: $minHeap bytes ($([math]::Round($minHeap/1024, 1)) KB)`n"
    $report += "   - Maximum: $maxHeap bytes ($([math]::Round($maxHeap/1024, 1)) KB)`n"
    $report += "   - Moyenne: $avgHeap bytes ($([math]::Round($avgHeap/1024, 1)) KB)`n"
    $report += "   - Échantillons: $($heapValues.Count)`n"
    
    if ($minHeap -lt 50000) {
        $report += "   ⚠️ CRITIQUE: Heap minimum très faible (< 50KB) !`n"
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
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
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
$wifiConnects = $logLines | Select-String -Pattern "WiFi.*connecté|WiFi.*connected|WiFi connected|WIFI.*CONNECTED" -AllMatches
$wifiConnectCount = $wifiConnects.Count

$report += "1. WIFI: $wifiErrorCount erreur(s), $wifiDisconnectCount déconnexion(s), $wifiConnectCount connexion(s)`n"
if ($wifiErrorCount -gt 0 -or $wifiDisconnectCount -gt 0) {
    $report += "   ⚠️ ATTENTION: Problèmes WiFi détectés`n"
    if ($wifiErrorCount -gt 0) {
        $report += "   Erreurs:`n"
        foreach ($match in $wifiErrors | Select-Object -First 3) {
            $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
        }
    }
    if ($wifiDisconnectCount -gt 0) {
        $report += "   Déconnexions:`n"
        foreach ($match in $wifiDisconnects | Select-Object -First 3) {
            $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
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
            $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
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

$reboots = $logLines | Select-String -Pattern "rst:0x|RST:|reset reason|Redémarrage|redémarrage|boot|BOOT|=== BOOT" -AllMatches
$rebootCount = $reboots.Count
$report += "Nombre de redémarrages détectés: $rebootCount`n"

if ($rebootCount -gt 0) {
    $report += "   Détails des redémarrages:`n"
    foreach ($match in $reboots | Select-Object -First 10) {
        $matchLine = $match.Line.Trim()
        $lineNum = $match.LineNumber
        $report += "   Ligne ${lineNum}: ${matchLine}`n"
    }
    
    # Analyser les causes de reboot
    $report += "`n   Analyse des causes:`n"
    
    # Reset reasons
    $resetReasons = $logLines | Select-String -Pattern "rst:0x(\d+)" -AllMatches
    if ($resetReasons.Count -gt 0) {
        $report += "   Codes de reset détectés:`n"
        foreach ($match in $resetReasons) {
            $code = $match.Matches[0].Groups[1].Value
            $reason = switch ($code) {
                "1" { "POWERON_RESET" }
                "3" { "SW_RESET" }
                "4" { "OWDT_RESET" }
                "5" { "DEEPSLEEP_RESET" }
                "6" { "SDIO_RESET" }
                "7" { "TG0WDT_SYS_RESET" }
                "8" { "TG1WDT_SYS_RESET" }
                "9" { "RTCWDT_SYS_RESET" }
                "10" { "INTRUSION_RESET" }
                "11" { "TGWDT_CPU_RESET" }
                "12" { "SW_CPU_RESET" }
                "13" { "RTCWDT_CPU_RESET" }
                "14" { "EXT_CPU_RESET" }
                "15" { "RTCWDT_BROWN_OUT_RESET" }
                "16" { "RTCWDT_RTC_RESET" }
                default { "UNKNOWN" }
            }
            $report += "     - rst:0x$code = $reason`n"
        }
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
$minHeap = if ($heapValues.Count -gt 0) { ($heapValues | Measure-Object -Minimum).Minimum } else { 999999 }
if ($minHeap -lt 50000) {
    $criticalIssues++
} elseif ($minHeap -lt 80000) {
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
        $report += "3. Vérifier les timeouts watchdog (actuellement 60s)`n"
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

# Statistiques générales
$report += "`n📈 STATISTIQUES GÉNÉRALES`n"
$report += "========================================`n"
$report += "Total lignes loggées: $($logLines.Count)`n"
$report += "Taille du fichier: $((Get-Item $logFile).Length) bytes`n"
$report += "Durée totale: $([math]::Round((((Get-Date) - $startTime).TotalSeconds), 1)) secondes`n"
$report += "`n"

# Sauvegarder le rapport
$report | Out-File -FilePath $analysisFile -Encoding UTF8

# Afficher le rapport à l'écran
Write-Host $report

Write-Host ""
Write-Host "=== RAPPORT SAUVEGARDÉ ===" -ForegroundColor Green
Write-Host "Fichier d'analyse: $analysisFile" -ForegroundColor Yellow
Write-Host "Fichier de log: $logFile" -ForegroundColor Yellow
Write-Host ""
Write-Host "=== PROCESSUS TERMINÉ ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date)" -ForegroundColor Cyan
