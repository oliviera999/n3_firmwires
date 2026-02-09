# Script de monitoring N minutes (défaut 5 min, ex. 30 min via -DurationSeconds 1800)
param(
    [string]$Port = "",
    [int]$DurationSeconds = 300
)

$timestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
# Tag dans le nom du fichier : 5min, 30min, 10min, etc.
if ($DurationSeconds -ge 3600) {
    $durationTag = "{0}h" -f [math]::Floor($DurationSeconds / 3600)
} elseif ($DurationSeconds % 60 -eq 0 -and $DurationSeconds -ge 60) {
    $durationTag = "{0}min" -f ($DurationSeconds / 60)
} else {
    $durationTag = "{0}s" -f $DurationSeconds
}
$logFile = "monitor_${durationTag}_${timestamp}.log"

Write-Host "=== MONITORING ESP32 - $durationTag ===" -ForegroundColor Green
Write-Host "Durée: $DurationSeconds secondes ($durationTag)" -ForegroundColor Cyan
Write-Host "Log: $logFile" -ForegroundColor Yellow
if ($Port) { Write-Host "Port: $Port" -ForegroundColor Yellow }
Write-Host ""

$usePython = $false
if ($Port -and (Test-Path "tools\monitor\monitor_unlimited.py")) {
    try {
        python --version 2>&1 | Out-Null
        python -c "import serial" 2>&1 | Out-Null
        $usePython = $true
    } catch {
        Write-Host "[WARN] Python/pyserial indisponible, utilisation de pio." -ForegroundColor Yellow
    }
}

if ($usePython -and $Port) {
    Write-Host "Lancement moniteur Python (port $Port)..." -ForegroundColor Cyan
    python tools\monitor\monitor_unlimited.py --port $Port --baud 115200 --duration $DurationSeconds --output $logFile
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[WARN] Script Python code sortie: $LASTEXITCODE" -ForegroundColor Yellow
    }
} else {
    $pioArgs = @("run", "--target", "monitor", "--environment", "wroom-test")
    if ($Port) { $pioArgs += @("--port", $Port) }
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList $pioArgs -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"

    $endTime = (Get-Date).AddSeconds($DurationSeconds)
    Write-Host "Monitoring démarré..." -ForegroundColor Green
    Write-Host "Appuyez sur Ctrl+C pour arrêter prématurément" -ForegroundColor Gray
    Write-Host ""

    $lastUpdate = 0
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        $progress = $DurationSeconds - $remaining
        $progressPercent = [math]::Round(($progress / $DurationSeconds) * 100)
        $elapsedMinutes = [math]::Floor($progress / 60)
        $elapsedSeconds = $progress % 60
        $remainingMinutes = [math]::Floor($remaining / 60)
        $remainingSeconds = $remaining % 60

        if ($progress -ge $lastUpdate + 30) {
            $timeElapsed = "$($elapsedMinutes.ToString('00')):$($elapsedSeconds.ToString('00'))"
            $timeRemaining = "$($remainingMinutes.ToString('00')):$($remainingSeconds.ToString('00'))"
            Write-Host "[$timeElapsed] Progression: $progressPercent% ($timeRemaining restant)" -ForegroundColor Cyan
            $lastUpdate = $progress
        }

        Start-Sleep -Seconds 1

        if ($monitorProcess.HasExited) {
            Write-Host ""
            Write-Host "Le processus de monitoring s'est arrêté prématurément" -ForegroundColor Red
            break
        }
    }

    if (-not $monitorProcess.HasExited) {
        Write-Host ""
        Write-Host "Arrêt du monitoring..." -ForegroundColor Yellow
        $monitorProcess.Kill()
        Start-Sleep -Seconds 2
    }
}

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Log: $logFile" -ForegroundColor Cyan
if (Test-Path "$logFile.errors") {
    Write-Host "Erreurs: $logFile.errors" -ForegroundColor Yellow
}

# Analyse du log avec monitor_summary.py si le script existe et que le log a du contenu
if (Test-Path $logFile) {
    $lineCount = (Get-Content $logFile -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    if ($lineCount -gt 0 -and (Test-Path "tools\monitor\monitor_summary.py")) {
        Write-Host ""
        Write-Host "=== ANALYSE DU LOG ===" -ForegroundColor Cyan
        python tools\monitor\monitor_summary.py $logFile
        if ($LASTEXITCODE -eq 0) {
            Write-Host ""
            Write-Host "Analyse terminée." -ForegroundColor Green
        }
    } else {
        Write-Host "Lignes capturées: $lineCount" -ForegroundColor Gray
    }
}
