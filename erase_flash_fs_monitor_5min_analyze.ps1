# Workflow: Erase all -> Flash all (firmware + FS) -> Monitor 5 min -> Analyse des résultats
# Fermer tout moniteur série (pio device monitor, etc.) avant de lancer.
#
# Usage:
#   .\erase_flash_fs_monitor_5min_analyze.ps1
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -Port COM4

param(
    [string]$Port = "",
    [string]$Environment = "wroom-test"
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

Write-Host "=== WORKFLOW: ERASE ALL / FLASH ALL / MONITOR 5MIN / ANALYSE ===" -ForegroundColor Green
Write-Host "Environnement: $Environment" -ForegroundColor Cyan
if ($Port) { Write-Host "Port série: $Port" -ForegroundColor Cyan }
Write-Host "Fermez tout moniteur série sur l'ESP32 avant de continuer." -ForegroundColor Yellow
Write-Host ""

# 1. Erase
Write-Host "1. Erase complète flash..." -ForegroundColor Cyan
pio run -e $Environment --target erase
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 2. Upload firmware
Write-Host "2. Flash firmware ($Environment)..." -ForegroundColor Cyan
if ($Port) { $env:PLATFORMIO_UPLOAD_PORT = $Port }
pio run -e $Environment --target upload
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 3

# 3. Upload FS
Write-Host "3. Flash LittleFS..." -ForegroundColor Cyan
pio run -e $Environment --target uploadfs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 4. Monitoring 5 min
Write-Host "4. Monitoring 5 minutes..." -ForegroundColor Cyan
$monitorParams = @{ DurationSeconds = 300 }
if ($Port) { $monitorParams["Port"] = $Port }
& "$projectRoot\monitor_5min.ps1" @monitorParams
# Fichier créé par cette exécution = le plus récent par date de création
$latest = Get-ChildItem -Path $projectRoot -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue | Sort-Object CreationTime -Descending | Select-Object -First 1
$logFile = if ($latest) { $latest.Name } else { $null }

# 5. Analyse du log (analyse_log + rapport diagnostic complet)
if ($logFile -and (Test-Path $logFile)) {
    $logFullPath = Join-Path $projectRoot $logFile
    $analysisFileName = $logFile -replace '\.log$', '_analysis.txt'
    $analysisFullPath = Join-Path $projectRoot $analysisFileName

    Write-Host "5. Analyse du log: $logFile" -ForegroundColor Cyan
    & "$projectRoot\analyze_log.ps1" -logFile $logFullPath
    Write-Host ""
    Write-Host "6. Rapport diagnostic complet (serveur distant, mails, exhaustive)..." -ForegroundColor Cyan
    & "$projectRoot\generate_diagnostic_report.ps1" -LogFile $logFullPath
    Write-Host ""
    Write-Host "=== WORKFLOW TERMINÉ ===" -ForegroundColor Green
    Write-Host "Log:           $logFullPath" -ForegroundColor Gray
    Write-Host "Analyse:       $analysisFullPath" -ForegroundColor Gray
    Write-Host "Rapport MD:    rapport_diagnostic_complet_*.md (dernier créé)" -ForegroundColor Gray
} else {
    Write-Host "5. Aucun fichier log trouvé pour l'analyse." -ForegroundColor Yellow
}
