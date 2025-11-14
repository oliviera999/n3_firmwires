# Script de monitoring 15 minutes ESP32 FFP5CS v11.116
# Capture et analyse automatique des logs

$port = "COM6"
$baudRate = 115200
$duration = 900 # 15 minutes en secondes
$logFile = "esp32_logs_v11.116_$(Get-Date -Format 'yyyyMMdd_HHmmss').txt"

Write-Host "=== MONITORING ESP32 FFP5CS v11.116 - 15 MINUTES ===" -ForegroundColor Green
Write-Host "Port: $port | Baud: $baudRate | Duration: $($duration/60) minutes"
Write-Host "Logs saved to: $logFile"
Write-Host "=================================================="
Write-Host ""

# Configuration du port série
$serial = New-Object System.IO.Ports.SerialPort $port, $baudRate
$serial.Open()

# Variables de monitoring
$startTime = Get-Date
$endTime = $startTime.AddSeconds($duration)
$lineCount = 0
$errors = @()
$warnings = @()
$heapUsage = @()
$sensorReadings = @()
$version = ""
$panics = 0
$reboots = 0
$wifiStatus = ""

# Capture des logs
Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Démarrage du monitoring..." -ForegroundColor Yellow

try {
    while ((Get-Date) -lt $endTime) {
        if ($serial.BytesToRead -gt 0) {
            $line = $serial.ReadLine()
            $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
            $logLine = "$timestamp | $line"
            
            # Écriture dans le fichier
            Add-Content -Path $logFile -Value $logLine
            
            # Affichage coloré selon le type
            if ($line -match "ERROR|PANIC|CRASH|ABORT") {
                Write-Host $line -ForegroundColor Red
                $errors += $logLine
                if ($line -match "PANIC|Guru Meditation") { $panics++ }
            }
            elseif ($line -match "WARN|WARNING") {
                Write-Host $line -ForegroundColor Yellow
                $warnings += $logLine
            }
            elseif ($line -match "Heap:") {
                Write-Host $line -ForegroundColor Cyan
                if ($line -match "Heap:\s+(\d+)") {
                    $heapUsage += [int]$Matches[1]
                }
            }
            elseif ($line -match "\[Sensor\]|\[AirSensor\]|\[SystemSensors\]") {
                Write-Host $line -ForegroundColor Green
                $sensorReadings += $logLine
            }
            elseif ($line -match "v11\.116|FFP5CS") {
                Write-Host $line -ForegroundColor Magenta
                if ($line -match "v([\d\.]+)") { $version = $Matches[1] }
            }
            elseif ($line -match "Reboot|Reset|Brownout") {
                Write-Host $line -ForegroundColor Red
                $reboots++
            }
            elseif ($line -match "WiFi|Connected|IP") {
                Write-Host $line -ForegroundColor Blue
                if ($line -match "WiFi connected|IP address") { $wifiStatus = "Connected" }
                if ($line -match "WiFi disconnected") { $wifiStatus = "Disconnected" }
            }
            else {
                Write-Host $line
            }
            
            $lineCount++
        }
        Start-Sleep -Milliseconds 10
    }
}
finally {
    $serial.Close()
}

# Analyse des résultats
Write-Host "`n`n=== ANALYSE DES RÉSULTATS ===" -ForegroundColor Green
Write-Host "Durée: $(((Get-Date) - $startTime).TotalMinutes.ToString('F1')) minutes"
Write-Host "Lignes capturées: $lineCount"
Write-Host "Version détectée: v$version"
Write-Host ""

# Analyse de stabilité
Write-Host "--- STABILITÉ ---" -ForegroundColor Cyan
Write-Host "Panics/Crashes: $panics" -ForegroundColor $(if($panics -gt 0){"Red"}else{"Green"})
Write-Host "Reboots: $reboots" -ForegroundColor $(if($reboots -gt 0){"Red"}else{"Green"})
Write-Host "Erreurs: $($errors.Count)" -ForegroundColor $(if($errors.Count -gt 0){"Yellow"}else{"Green"})
Write-Host "Warnings: $($warnings.Count)" -ForegroundColor $(if($warnings.Count -gt 5){"Yellow"}else{"Green"})
Write-Host "WiFi Status: $wifiStatus"

# Analyse mémoire
if ($heapUsage.Count -gt 0) {
    $avgHeap = ($heapUsage | Measure-Object -Average).Average
    $minHeap = ($heapUsage | Measure-Object -Minimum).Minimum
    $maxHeap = ($heapUsage | Measure-Object -Maximum).Maximum
    
    Write-Host ""
    Write-Host "--- MÉMOIRE HEAP ---" -ForegroundColor Cyan
    Write-Host "Moyenne: $([int]$avgHeap) bytes"
    Write-Host "Minimum: $minHeap bytes" -ForegroundColor $(if($minHeap -lt 50000){"Red"}else{"Green"})
    Write-Host "Maximum: $maxHeap bytes"
}

# Analyse capteurs
Write-Host ""
Write-Host "--- CAPTEURS ---" -ForegroundColor Cyan
Write-Host "Lectures enregistrées: $($sensorReadings.Count)"

# Top 5 erreurs
if ($errors.Count -gt 0) {
    Write-Host ""
    Write-Host "--- TOP ERREURS ---" -ForegroundColor Red
    $errors | Select-Object -First 5 | ForEach-Object { Write-Host $_ }
}

# Top 5 warnings
if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "--- TOP WARNINGS ---" -ForegroundColor Yellow
    $warnings | Select-Object -First 5 | ForEach-Object { Write-Host $_ }
}

# Verdict final
Write-Host ""
Write-Host "=== VERDICT ===" -ForegroundColor Magenta
if ($panics -eq 0 -and $reboots -eq 0 -and $errors.Count -lt 5) {
    Write-Host "✅ SYSTÈME STABLE - Version $version fonctionnelle" -ForegroundColor Green
} elseif ($panics -gt 0 -or $reboots -gt 0) {
    Write-Host "❌ SYSTÈME INSTABLE - Crashes/reboots détectés" -ForegroundColor Red
} else {
    Write-Host "⚠️ SYSTÈME PARTIELLEMENT STABLE - Erreurs mineures détectées" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Log complet sauvé dans: $logFile"
Write-Host "Monitoring terminé à $(Get-Date -Format 'HH:mm:ss')"
