# Monitoring série N minutes — applicable à tous les firmwares (sauf ratata, LVGL_Widgets).
# À lancer depuis la racine de firmwires.
#
# Usage:
#   .\monitor_Nmin.ps1 -Project n3pp4_2
#   .\monitor_Nmin.ps1 -Project ffp5cs -Port COM4 -DurationSeconds 600
#   .\monitor_Nmin.ps1 -Project msp2_5 -Environment esp32dev_test
#
# Projets supportés : n3pp4_2, msp2_5, uploadphotosserver (env msp1/n3pp/ffp3), uploadphotosserver_msp1, uploadphotosserver_n3pp_1_6_deppsleep, uploadphotosserver_ffp3_1_5_deppsleep, ffp5cs

param(
    [Parameter(Mandatory = $true)]
    [string]$Project,
    [string]$Environment = "",
    [string]$Port = "",
    [int]$DurationSeconds = 300
)

$ErrorActionPreference = "Stop"
$firmwiresRoot = $PSScriptRoot

# Projets autorisés (exclusion : ratata, LVGL_Widgets et sous-dossiers ffp5cs)
$allowedProjects = @(
    "n3pp4_2",
    "msp2_5",
    "uploadphotosserver",
    "uploadphotosserver_msp1",
    "uploadphotosserver_n3pp_1_6_deppsleep",
    "uploadphotosserver_ffp3_1_5_deppsleep",
    "ffp5cs"
)
if ($Project -notin $allowedProjects) {
    Write-Host "Projet non supporte: $Project" -ForegroundColor Red
    Write-Host "Projets autorises: $($allowedProjects -join ', ')" -ForegroundColor Yellow
    exit 1
}

$projectPath = Join-Path $firmwiresRoot $Project
if (-not (Test-Path $projectPath)) {
    Write-Host "Dossier projet introuvable: $projectPath" -ForegroundColor Red
    exit 1
}

# Environnement par défaut selon le projet
$defaultEnvs = @{
    "n3pp4_2" = "esp32dev"
    "msp2_5" = "esp32dev"
    "uploadphotosserver" = "msp1"
    "uploadphotosserver_msp1" = "esp32cam"
    "uploadphotosserver_n3pp_1_6_deppsleep" = "esp32cam"
    "uploadphotosserver_ffp3_1_5_deppsleep" = "esp32cam"
    "ffp5cs" = "wroom-test"
}
if ([string]::IsNullOrEmpty($Environment)) {
    $Environment = $defaultEnvs[$Project]
}

Set-Location $projectPath

$timestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
if ($DurationSeconds -ge 3600) {
    $durationTag = "{0}h" -f [math]::Floor($DurationSeconds / 3600)
} elseif ($DurationSeconds % 60 -eq 0 -and $DurationSeconds -ge 60) {
    $durationTag = "{0}min" -f ($DurationSeconds / 60)
} else {
    $durationTag = "{0}s" -f $DurationSeconds
}
$logFile = "monitor_${durationTag}_${timestamp}.log"

Write-Host "=== MONITORING ESP32 - $Project - $durationTag ===" -ForegroundColor Green
Write-Host "Projet: $Project" -ForegroundColor Cyan
Write-Host "Durée: $DurationSeconds secondes ($durationTag)" -ForegroundColor Cyan
Write-Host "Log: $logFile" -ForegroundColor Yellow
Write-Host "Env: $Environment" -ForegroundColor Yellow
if ($Port) { Write-Host "Port: $Port" -ForegroundColor Yellow }
Write-Host ""

$usePython = $false
$pythonMonitorScript = Join-Path $projectPath "tools\monitor\monitor_unlimited.py"
if ($Port -and (Test-Path $pythonMonitorScript)) {
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
    python $pythonMonitorScript --port $Port --baud 115200 --duration $DurationSeconds --output $logFile
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[WARN] Script Python code sortie: $LASTEXITCODE" -ForegroundColor Yellow
    }
} else {
    $pioArgs = @("run", "--target", "monitor", "--environment", $Environment)
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
Write-Host "Log: $projectPath\$logFile" -ForegroundColor Cyan
if (Test-Path "$logFile.errors") {
    Write-Host "Erreurs: $logFile.errors" -ForegroundColor Yellow
}

# Analyse du log avec monitor_summary.py si présent (ex. ffp5cs)
$monitorSummaryPy = Join-Path $projectPath "tools\monitor\monitor_summary.py"
if (Test-Path $logFile) {
    $lineCount = (Get-Content $logFile -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    if ($lineCount -gt 0 -and (Test-Path $monitorSummaryPy)) {
        Write-Host ""
        Write-Host "=== ANALYSE DU LOG ===" -ForegroundColor Cyan
        python $monitorSummaryPy $logFile
        if ($LASTEXITCODE -eq 0) {
            Write-Host ""
            Write-Host "Analyse terminée." -ForegroundColor Green
        }
    } else {
        Write-Host "Lignes capturées: $lineCount" -ForegroundColor Gray
    }
}
