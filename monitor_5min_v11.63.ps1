#!/usr/bin/env pwsh
# Script de monitoring 5 minutes - ESP32 v11.63
# Objectif: Validation simplification sync actionneurs (élimination crashes Stack canary)

param(
    [string]$Port = "COM6",
    [int]$Baud = 115200,
    [int]$Duration = 300  # 5 minutes
)

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_5min_v11.63_$timestamp.log"

Write-Host "=== MONITORING 5 MINUTES - ESP32 v11.63 ===" -ForegroundColor Cyan
Write-Host "Objectif: Validation simplification sync actionneurs (élimination crashes Stack canary)" -ForegroundColor Yellow
Write-Host "Démarrage: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Green
Write-Host "Port: $Port, Baud: $Baud, Durée: ${Duration}s" -ForegroundColor Green
Write-Host "Fichier de log: $logFile" -ForegroundColor Green

# Vérifier si le port existe
$ports = Get-WmiObject -Class Win32_SerialPort | Where-Object { $_.DeviceID -eq $Port }
if (-not $ports) {
    Write-Host "ERREUR: Port $Port non trouvé!" -ForegroundColor Red
    Write-Host "Ports disponibles:" -ForegroundColor Yellow
    Get-WmiObject -Class Win32_SerialPort | Select-Object DeviceID, Description | Format-Table
    exit 1
}

Write-Host "Port $Port trouvé: $($ports.Description)" -ForegroundColor Green

# Fonction de monitoring
function Start-Monitoring {
    param([string]$Port, [int]$Baud, [int]$Duration, [string]$LogFile)
    
    try {
        Write-Host "Démarrage du monitoring..." -ForegroundColor Green
        
        # Commande pio device monitor avec timeout
        $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $Port, "--baud", $Baud -NoNewWindow -PassThru -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"
        
        # Attendre la durée spécifiée
        $elapsed = 0
        while ($elapsed -lt $Duration -and -not $process.HasExited) {
            Start-Sleep -Seconds 10
            $elapsed += 10
            
            $progress = [math]::Round(($elapsed / $Duration) * 100, 1)
            Write-Host "Progression: $elapsed/$Duration secondes ($progress%)" -ForegroundColor Yellow
            
            # Vérifier si le processus est toujours actif
            if ($process.HasExited) {
                Write-Host "⚠️ Processus de monitoring terminé prématurément" -ForegroundColor Red
                break
            }
        }
        
        # Arrêter le processus
        if (-not $process.HasExited) {
            Write-Host "Arrêt du monitoring après $Duration secondes..." -ForegroundColor Yellow
            $process.Kill()
            $process.WaitForExit(5000)
        }
        
        Write-Host "Monitoring terminé" -ForegroundColor Green
        
    } catch {
        Write-Host "ERREUR pendant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

# Démarrer le monitoring
Start-Monitoring -Port $Port -Baud $Baud -Duration $Duration -LogFile $logFile

Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Cyan

# Analyser les logs pour les points critiques
if (Test-Path $logFile) {
    $logContent = Get-Content $logFile -Raw
    
    Write-Host "`n📊 STATISTIQUES GÉNÉRALES:" -ForegroundColor Yellow
    $lines = ($logContent -split "`n").Count
    Write-Host "Lignes de log: $lines"
    
    # Rechercher les erreurs critiques (PRIORITÉ ABSOLUE)
    $criticalErrors = @(
        "Guru Meditation",
        "Stack canary watchpoint",
        "Panic",
        "Brownout",
        "Core dump",
        "ASSERT",
        "abort()",
        "Fatal exception",
        "sync_.*_sto",
        "TCPIP core functionality"
    )
    
    Write-Host "`n🔴 ERREURS CRITIQUES (PRIORITÉ ABSOLUE):" -ForegroundColor Red
    $hasCriticalErrors = $false
    foreach ($error in $criticalErrors) {
        $matches = [regex]::Matches($logContent, $error, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($matches.Count -gt 0) {
            Write-Host "$error : $($matches.Count) occurrence(s)" -ForegroundColor Red
            $hasCriticalErrors = $true
        }
    }
    
    if (-not $hasCriticalErrors) {
        Write-Host "✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    
    # Rechercher les warnings
    $warnings = @(
        "WARNING",
        "WARN",
        "⚠️",
        "Timeout",
        "Failed",
        "Error",
        "Failed to"
    )
    
    Write-Host "`n🟡 WARNINGS ET ERREURS:" -ForegroundColor Yellow
    $hasWarnings = $false
    foreach ($warning in $warnings) {
        $matches = [regex]::Matches($logContent, $warning, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($matches.Count -gt 0) {
            Write-Host "$warning : $($matches.Count) occurrence(s)" -ForegroundColor Yellow
            $hasWarnings = $true
        }
    }
    
    if (-not $hasWarnings) {
        Write-Host "✅ Aucun warning détecté" -ForegroundColor Green
    }
    
    # Analyser la mémoire
    Write-Host "`n💾 ANALYSE MÉMOIRE:" -ForegroundColor Cyan
    
    # Heap libre
    $heapMatches = [regex]::Matches($logContent, "Heap libre: (\d+) bytes")
    if ($heapMatches.Count -gt 0) {
        $heapValues = $heapMatches | ForEach-Object { [int]$_.Groups[1].Value }
        $minHeap = ($heapValues | Measure-Object -Minimum).Minimum
        $maxHeap = ($heapValues | Measure-Object -Maximum).Maximum
        $avgHeap = [math]::Round(($heapValues | Measure-Object -Average).Average, 0)
        
        Write-Host "Heap libre - Min: $minHeap bytes, Max: $maxHeap bytes, Moy: $avgHeap bytes" -ForegroundColor White
        
        if ($minHeap -lt 100000) {
            Write-Host "⚠️ Heap minimum critique: $minHeap bytes" -ForegroundColor Red
        } elseif ($minHeap -lt 150000) {
            Write-Host "⚠️ Heap minimum faible: $minHeap bytes" -ForegroundColor Yellow
        } else {
            Write-Host "✅ Heap minimum acceptable: $minHeap bytes" -ForegroundColor Green
        }
    }
    
    # Stack libre
    $stackMatches = [regex]::Matches($logContent, "Stack libre: (\d+) bytes")
    if ($stackMatches.Count -gt 0) {
        $stackValues = $stackMatches | ForEach-Object { [int]$_.Groups[1].Value }
        $minStack = ($stackValues | Measure-Object -Minimum).Minimum
        Write-Host "Stack libre - Min: $minStack bytes" -ForegroundColor White
        
        if ($minStack -lt 10000) {
            Write-Host "⚠️ Stack minimum critique: $minStack bytes" -ForegroundColor Red
        } else {
            Write-Host "✅ Stack minimum acceptable: $minStack bytes" -ForegroundColor Green
        }
    }
    
    # Analyser les simplifications v11.63
    Write-Host "`n🚀 SIMPLIFICATIONS v11.63:" -ForegroundColor Magenta
    
    # Système de marqueur
    $markerMatches = [regex]::Matches($logContent, "Sync serveur marquée|markForSync")
    if ($markerMatches.Count -gt 0) {
        Write-Host "✅ Système de marqueur détecté: $($markerMatches.Count) occurrence(s)" -ForegroundColor Green
    } else {
        Write-Host "⚠️ Système de marqueur non détecté" -ForegroundColor Yellow
    }
    
    # Sync différée
    $syncMatches = [regex]::Matches($logContent, "Sync serveur réussie.*action manuelle locale")
    if ($syncMatches.Count -gt 0) {
        Write-Host "✅ Sync différée fonctionnelle: $($syncMatches.Count) occurrence(s)" -ForegroundColor Green
    } else {
        Write-Host "⚠️ Sync différée non détectée" -ForegroundColor Yellow
    }
    
    # Absence de tâches FreeRTOS pour sync
    $taskMatches = [regex]::Matches($logContent, "syncActuatorStateAsync|Task.*sync")
    if ($taskMatches.Count -eq 0) {
        Write-Host "✅ Aucune tâche FreeRTOS de sync détectée (CORRECT)" -ForegroundColor Green
    } else {
        Write-Host "⚠️ Tâches FreeRTOS de sync encore présentes: $($taskMatches.Count) occurrence(s)" -ForegroundColor Red
    }
    
    # WiFi et WebSocket
    Write-Host "`n🌐 CONNECTIVITÉ:" -ForegroundColor Cyan
    
    $wifiMatches = [regex]::Matches($logContent, "WiFi connecté|WiFi connect")
    if ($wifiMatches.Count -gt 0) {
        Write-Host "✅ WiFi connecté détecté: $($wifiMatches.Count) occurrence(s)" -ForegroundColor Green
    }
    
    $websocketMatches = [regex]::Matches($logContent, "WebSocket connecté|WebSocket connect")
    if ($websocketMatches.Count -gt 0) {
        Write-Host "✅ WebSocket connecté détecté: $($websocketMatches.Count) occurrence(s)" -ForegroundColor Green
    }
    
    # Capteurs
    Write-Host "`n🌡️ CAPTEURS:" -ForegroundColor Cyan
    
    $sensorMatches = [regex]::Matches($logContent, "Température|Humidité|Niveau")
    if ($sensorMatches.Count -gt 0) {
        Write-Host "✅ Lecture capteurs détectée: $($sensorMatches.Count) occurrence(s)" -ForegroundColor Green
    }
    
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile" -ForegroundColor Red
}

Write-Host "`n=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "Fichier de log: $logFile" -ForegroundColor White
Write-Host "Durée: $Duration secondes" -ForegroundColor White
Write-Host "Version: v11.63 (Simplification sync actionneurs)" -ForegroundColor White

if ($hasCriticalErrors) {
    Write-Host "🔴 RÉSULTAT: ERREURS CRITIQUES DÉTECTÉES" -ForegroundColor Red
    Write-Host "Action recommandée: Analyser les logs et corriger les problèmes" -ForegroundColor Red
} elseif ($hasWarnings) {
    Write-Host "🟡 RÉSULTAT: WARNINGS DÉTECTÉS" -ForegroundColor Yellow
    Write-Host "Action recommandée: Surveiller et analyser les warnings" -ForegroundColor Yellow
} else {
    Write-Host "✅ RÉSULTAT: STABLE" -ForegroundColor Green
    Write-Host "Action recommandée: Monitoring 15 minutes puis surveillance 24h" -ForegroundColor Green
}

Write-Host "`n=== FIN DU MONITORING ===" -ForegroundColor Cyan
