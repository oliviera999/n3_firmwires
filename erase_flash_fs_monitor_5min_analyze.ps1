# Erase flash wroom-test -> flash firmware + FS -> monitoring 5 min -> analyse du log
# Fermer tout moniteur série (pio device monitor, etc.) avant de lancer.

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

Write-Host "=== ERASE + FLASH WROOM-TEST + MONITOR 5MIN + ANALYSE ===" -ForegroundColor Green
Write-Host "Fermez tout moniteur série sur COM avant de continuer." -ForegroundColor Yellow
Write-Host ""

# 1. Erase
Write-Host "1. Erase complète flash..." -ForegroundColor Cyan
pio run -e wroom-test --target erase
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 2. Upload firmware
Write-Host "2. Flash firmware wroom-test..." -ForegroundColor Cyan
pio run -e wroom-test --target upload
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 3

# 3. Upload FS
Write-Host "3. Flash LittleFS..." -ForegroundColor Cyan
pio run -e wroom-test --target uploadfs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 4. Monitoring 5 min
Write-Host "4. Monitoring 5 minutes..." -ForegroundColor Cyan
& "$projectRoot\monitor_5min.ps1" -DurationSeconds 300
$latest = Get-ChildItem -Path $projectRoot -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$logFile = if ($latest) { $latest.Name } else { $null }

# 5. Analyse du log (analyse_log + rapport diagnostic complet)
if ($logFile -and (Test-Path $logFile)) {
    $logFullPath = Join-Path $projectRoot $logFile
    Write-Host "5. Analyse du log: $logFile" -ForegroundColor Cyan
    & "$projectRoot\analyze_log.ps1" -logFile $logFullPath
    Write-Host ""
    Write-Host "6. Rapport diagnostic complet (serveur distant, mails, exhaustive)..." -ForegroundColor Cyan
    & "$projectRoot\generate_diagnostic_report.ps1" -LogFile $logFullPath
    Write-Host ""
    Write-Host "=== TERMINÉ ===" -ForegroundColor Green
    Write-Host "Log: $logFile" -ForegroundColor Gray
    Write-Host "Analyse: $($logFile -replace '\.log$','_analysis.txt')" -ForegroundColor Gray
    Write-Host "Rapport complet: rapport_diagnostic_complet_*.md" -ForegroundColor Gray
} else {
    Write-Host "5. Aucun fichier log trouvé pour l'analyse." -ForegroundColor Yellow
}
