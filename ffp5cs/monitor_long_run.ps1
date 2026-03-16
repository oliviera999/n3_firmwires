# Monitoring ESP32 longue durée (12h, 24h, 48h...) avec rotation par tranches et rapport agrégé.
# Utilise tools\monitor\monitor_unlimited.py avec --output-dir et --rotate-after-seconds.
# En fin de run, génère un rapport agrégé via aggregate_monitor_report.ps1.
#
# Usage:
#   .\monitor_long_run.ps1 -Port COM6 -DurationHours 24
#   .\monitor_long_run.ps1 -Port COM6 -DurationHours 12 -RotateHours 4
#   .\monitor_long_run.ps1 -Port COM6 -DurationHours 24 -WithEventsFile
#
# Prérequis: Python + pyserial (pip install pyserial). Port série obligatoire pour ce script.

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$DurationHours = 24,
    [int]$RotateHours = 1,
    [int]$BaudRate = 115200,
    [switch]$WithEventsFile = $false,
    [switch]$SkipAggregate = $false
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

$durationSeconds = $DurationHours * 3600
$rotateSeconds = $RotateHours * 3600
$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm"
$outputDirName = "monitor_long_${DurationHours}h_${timestamp}"
$logsDir = Join-Path $projectRoot "logs"
$outputDir = Join-Path $logsDir $outputDirName

# Vérifier Python + pyserial
$usePython = $false
if (Get-Command python -ErrorAction SilentlyContinue) {
    try {
        python -c "import serial" 2>&1 | Out-Null
        $usePython = $true
    } catch {
        # ignore
    }
}
if (-not $usePython) {
    Write-Host "Erreur: Python et pyserial sont requis pour monitor_long_run.ps1 (pip install pyserial)" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "tools\monitor\monitor_unlimited.py")) {
    Write-Host "Erreur: tools\monitor\monitor_unlimited.py introuvable" -ForegroundColor Red
    exit 1
}

Write-Host "=== MONITORING LONGUE DURÉE ESP32 ===" -ForegroundColor Green
Write-Host "Port: $Port" -ForegroundColor Cyan
Write-Host "Durée: $DurationHours h ($durationSeconds s)" -ForegroundColor Cyan
Write-Host "Rotation: toutes les $RotateHours h ($rotateSeconds s)" -ForegroundColor Cyan
Write-Host "Dossier de sortie: $outputDirName" -ForegroundColor Yellow
if ($WithEventsFile) {
    Write-Host "Fichier events: activé (lignes critiques)" -ForegroundColor Yellow
}
Write-Host ""

$eventsPath = $null
if ($WithEventsFile) {
    $eventsPath = Join-Path $outputDir "events.log"
}

# Créer le dossier de sortie (Python y écrit part_*.log et éventuellement events.log)
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

. (Join-Path $projectRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $Port -Baud $BaudRate

$pythonArgs = @(
    "tools\monitor\monitor_unlimited.py",
    "--port", $Port,
    "--baud", $BaudRate,
    "--duration", $durationSeconds,
    "--output-dir", $outputDir,
    "--rotate-after-seconds", $rotateSeconds
)
if ($eventsPath) {
    $pythonArgs += "--events-file"
    $pythonArgs += $eventsPath
}

Write-Host "Lancement du moniteur Python..." -ForegroundColor Cyan
python @pythonArgs
$monitorExit = $LASTEXITCODE

Write-Host ""
Write-Host "=== MONITORING TERMINÉ ===" -ForegroundColor Green
Write-Host "Dossier: $outputDir" -ForegroundColor Cyan
if ($monitorExit -ne 0) {
    Write-Host "Code sortie moniteur: $monitorExit" -ForegroundColor Yellow
}

# Rapport agrégé
if (-not $SkipAggregate) {
    $reportName = "RAPPORT_${DurationHours}h_${timestamp}.md"
    $reportPath = Join-Path $projectRoot $reportName
    if (Test-Path $outputDir) {
        $partLogs = Get-ChildItem -Path $outputDir -Filter "part_*.log" -ErrorAction SilentlyContinue | Sort-Object Name
        if ($partLogs.Count -gt 0) {
            Write-Host ""
            Write-Host "Génération du rapport agrégé: $reportName" -ForegroundColor Cyan
            & "$projectRoot\aggregate_monitor_report.ps1" -LogFolder $outputDir -OutputReport $reportPath
            if ($LASTEXITCODE -eq 0) {
                Write-Host "Rapport: $reportPath" -ForegroundColor Green
            }
        } else {
            Write-Host "Aucun fichier part_*.log dans $outputDir, rapport non généré." -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "Rapport agrégé ignoré (-SkipAggregate)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Fichiers de log: $outputDir" -ForegroundColor Gray
