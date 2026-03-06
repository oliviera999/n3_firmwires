# Build wroom-s3-test-psram puis workflow erase + flash + monitor N min + analyse.
# Usage: .\run_s3_psram_validation.ps1 [-Port COM7] [-DurationMinutes 2]
# Critère de succès: BOOT FFP5CS + setup done / init done, 0 TG1WDT/TG0WDT, 0 crash dans le rapport.
param(
    [string]$Port = "COM7",
    [int]$DurationMinutes = 2
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot
$envName = "wroom-s3-test-psram"

Write-Host "=== Validation S3 PSRAM (N16R8) - build + erase + flash + monitor ${DurationMinutes} min + analyse ===" -ForegroundColor Cyan
Write-Host "Environnement: $envName" -ForegroundColor Gray
Write-Host ""

Write-Host "1. Build $envName..." -ForegroundColor Yellow
pio run -e $envName
if ($LASTEXITCODE -ne 0) { Write-Host "Build FAILED"; exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Write-Host ""

Write-Host "2. Workflow erase + flash + monitor ${DurationMinutes} min (Port $Port)..." -ForegroundColor Yellow
.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment $envName -Port $Port -DurationMinutes $DurationMinutes -SkipBuild
if ($LASTEXITCODE -ne 0) { Write-Host "Workflow FAILED"; exit $LASTEXITCODE }
Write-Host ""

# 3. Dernier log monitor (monitor_2min_* ou monitor_5min_* selon DurationMinutes)
$logPattern = "monitor_${DurationMinutes}min_*.log"
$log = Get-ChildItem -Filter $logPattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) {
    $log = Get-ChildItem -Filter "monitor_*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
if ($log) {
    $content = Get-Content $log.FullName -Raw -ErrorAction SilentlyContinue
    if ($content) {
        $bootOk = $content -match "BOOT FFP5CS"
        $setupDone = ($content -match "setup done") -or ($content -match "init done")
        $tg1Count = ([regex]::Matches($content, "TG1WDT_SYS_RST")).Count
        $tg0Count = ([regex]::Matches($content, "TG0WDT_SYS_RST")).Count
        $appTasksDone = $content -match "AppTasks::start done"
        if ($bootOk -and $tg1Count -eq 0 -and $tg0Count -eq 0 -and $setupDone) {
            Write-Host "  [OK] Boot FFP5CS + setup/init done, aucun WDT" -ForegroundColor Green
        }
        elseif ($bootOk -and $tg1Count -eq 0 -and $tg0Count -eq 0) {
            Write-Host "  [OK] Boot FFP5CS vu, aucun WDT (setup done non detecte dans le log)" -ForegroundColor Green
        }
        elseif ($bootOk -and $tg0Count -gt 0) {
            Write-Host "  [PARTIEL] Boot OK mais $tg0Count reset(s) TG0WDT" -ForegroundColor Yellow
        }
        elseif ($bootOk) {
            Write-Host "  [PARTIEL] Boot OK mais $tg1Count reset(s) TG1WDT" -ForegroundColor Yellow
        }
        elseif ($tg0Count -gt 0) {
            Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg0Count TG0WDT" -ForegroundColor Red
        }
        else {
            Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg1Count TG1WDT" -ForegroundColor Red
        }
        Write-Host "  Log: $($log.FullName)"
    }
}

# 4. Dernier rapport diagnostic
$rapport = Get-ChildItem -Filter "rapport_diagnostic_complet_*.md" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($rapport) {
    Write-Host "  Rapport: $($rapport.FullName)" -ForegroundColor Gray
    $rapportContent = Get-Content $rapport.FullName -Raw -ErrorAction SilentlyContinue
    if ($rapportContent -match "Crashes: (\d+)") {
        $crashCount = [int]$Matches[1]
        if ($crashCount -eq 0) {
            Write-Host "  [OK] 0 crash dans le rapport" -ForegroundColor Green
        } else {
            Write-Host "  [ATTENTION] $crashCount crash(es) dans le rapport" -ForegroundColor Yellow
        }
    }
}

Write-Host ""
Write-Host "=== Fin validation PSRAM ===" -ForegroundColor Cyan
