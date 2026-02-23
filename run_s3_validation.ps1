# Build wroom-s3-test puis workflow erase + flash + monitor 1 min (même chaîne que wroom-test, pas de patches).
# Usage: .\run_s3_validation.ps1 [-Port COM7]
param([string]$Port = "COM7")

$ErrorActionPreference = "Stop"
$envName = "wroom-s3-test"

Write-Host "=== Validation S3 (build + erase + flash + monitor 1 min) ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "1. Build $envName..." -ForegroundColor Yellow
pio run -e $envName
if ($LASTEXITCODE -ne 0) { Write-Host "Build FAILED"; exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Write-Host ""

Write-Host "2. Workflow erase + flash + monitor 1 min (Port $Port)..." -ForegroundColor Yellow
.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment $envName -Port $Port -DurationMinutes 1 -SkipBuild
if ($LASTEXITCODE -ne 0) { Write-Host "Workflow FAILED"; exit $LASTEXITCODE }

# 3. Résumé du dernier log monitor
$log = Get-ChildItem -Filter "monitor_1min_*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($log) {
    $content = Get-Content $log.FullName -Raw
    $bootOk = $content -match "BOOT FFP5CS"
    $tg1Count = ([regex]::Matches($content, "TG1WDT_SYS_RST")).Count
    $tg0Count = ([regex]::Matches($content, "TG0WDT_SYS_RST")).Count
    if ($bootOk -and $tg1Count -eq 0 -and $tg0Count -eq 0) { Write-Host "  [OK] Boot FFP5CS vu, aucun WDT" -ForegroundColor Green }
    elseif ($bootOk -and $tg0Count -gt 0) { Write-Host "  [PARTIEL] Boot OK mais $tg0Count reset(s) TG0WDT" -ForegroundColor Yellow }
    elseif ($bootOk) { Write-Host "  [PARTIEL] Boot OK mais $tg1Count reset(s) TG1WDT" -ForegroundColor Yellow }
    elseif ($tg0Count -gt 0) { Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg0Count TG0WDT" -ForegroundColor Red }
    else { Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg1Count TG1WDT" -ForegroundColor Red }
    Write-Host "  Log: $($log.FullName)"
}

Write-Host ""
Write-Host "=== Fin validation ===" -ForegroundColor Cyan
