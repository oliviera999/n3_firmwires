# Workflow : Erase flash -> Flash firmware (et FS si projet concerné) -> Monitor N min -> Analyse optionnelle.
# Applicable à tous les firmwares (sauf ratata, LVGL_Widgets). À lancer depuis la racine de firmwires.
#
# Usage:
#   .\erase_flash_monitor.ps1 -Project n3pp4_2
#   .\erase_flash_monitor.ps1 -Project ffp5cs -Port COM4 -DurationMinutes 5
#   .\erase_flash_monitor.ps1 -Project ffp5cs -Environment wroom-prod -SkipUploadFs
#   .\erase_flash_monitor.ps1 -Project msp2_5 -DurationMinutes 10 -SkipBuild
#
# -SkipBuild : ne pas compiler (erase + flash + monitor uniquement).
# -SkipClean : ne pas lancer "pio run -t clean" avant le build.
# -SkipUploadFs : ne pas flasher le système de fichiers (LittleFS) ; utile pour ffp5cs wroom-prod.

param(
    [Parameter(Mandatory = $true)]
    [string]$Project,
    [string]$Environment = "",
    [string]$Port = "",
    [int]$DurationMinutes = 5,
    [switch]$SkipBuild = $false,
    [switch]$SkipClean = $false,
    [switch]$SkipUploadFs = $false
)

$ErrorActionPreference = "Stop"
$firmwiresRoot = $PSScriptRoot

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

$releaseScript = Join-Path $firmwiresRoot "scripts\Release-ComPort.ps1"
if (Test-Path $releaseScript) {
    . $releaseScript
}
if ($Port) {
    $env:PLATFORMIO_UPLOAD_PORT = $Port
    Release-ComPortIfNeeded -Port $Port
}

$durationSeconds = $DurationMinutes * 60
Write-Host "=== ERASE / FLASH / MONITOR ${DurationMinutes}MIN - $Project ===" -ForegroundColor Green
Write-Host "Projet: $Project" -ForegroundColor Cyan
Write-Host "Environnement: $Environment" -ForegroundColor Cyan
Write-Host "Monitoring: $DurationMinutes min ($durationSeconds s)" -ForegroundColor Cyan
if ($Port) { Write-Host "Port serie: $Port" -ForegroundColor Cyan }
Write-Host "Fermez tout moniteur serie sur l'ESP32 avant de continuer." -ForegroundColor Yellow
Write-Host ""

Set-Location $projectPath

# 0. Build (sauf si -SkipBuild)
if (-not $SkipBuild) {
    Write-Host "0. Build $Environment..." -ForegroundColor Cyan
    if (-not $SkipClean) {
        Write-Host "   Clean..." -ForegroundColor Gray
        & pio run -e $Environment -t clean
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $buildJobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
    Write-Host "   Build en cours..." -ForegroundColor Gray
    & pio run -e $Environment @buildJobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Write-Host ""
}

# 1. Erase (retry si port fourni)
$maxRetries = if ($Port) { 3 } else { 1 }
$eraseOk = $false
for ($r = 1; $r -le $maxRetries; $r++) {
    Write-Host "1. Erase complete flash..." -ForegroundColor Cyan
    if ($r -gt 1) { Write-Host "   Tentative $r/$maxRetries (attente 8s)..." -ForegroundColor Gray; Start-Sleep -Seconds 8 }
    & pio run -e $Environment --target erase
    if ($LASTEXITCODE -eq 0) { $eraseOk = $true; break }
    if ($Port -and $r -lt $maxRetries) { Write-Host "   Port occupe? Fermez le moniteur serie puis relance." -ForegroundColor Yellow }
}
if (-not $eraseOk) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 2. Upload firmware
Write-Host "2. Flash firmware ($Environment)..." -ForegroundColor Cyan
& pio run -e $Environment --target upload
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 3

# 3. Upload FS (uniquement ffp5cs, sauf wroom-prod sauf si -SkipUploadFs)
$doUploadFs = ($Project -eq "ffp5cs" -and $Environment -ne "wroom-prod" -and -not $SkipUploadFs)
if ($doUploadFs) {
    Write-Host "3. Flash LittleFS..." -ForegroundColor Cyan
    & pio run -e $Environment --target uploadfs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Start-Sleep -Seconds 2
} else {
    Write-Host "3. LittleFS/FS : ignore (projet sans FS ou -SkipUploadFs / wroom-prod)" -ForegroundColor Gray
}

# 4. Monitoring
Write-Host "4. Monitoring $DurationMinutes minutes..." -ForegroundColor Cyan
& (Join-Path $firmwiresRoot "monitor_Nmin.ps1") -Project $Project -Environment $Environment -Port $Port -DurationSeconds $durationSeconds

# 5. Analyse optionnelle (si le projet a analyze_log.ps1, ex. ffp5cs)
$latest = Get-ChildItem -Path $projectPath -Filter "monitor_*.log" -ErrorAction SilentlyContinue | Sort-Object CreationTime -Descending | Select-Object -First 1
$analyzeScript = Join-Path $projectPath "analyze_log.ps1"
if ($latest -and (Test-Path $analyzeScript)) {
    Write-Host "5. Analyse du log: $($latest.Name)" -ForegroundColor Cyan
    & $analyzeScript -logFile $latest.FullName
    $reportScript = Join-Path $projectPath "generate_diagnostic_report.ps1"
    if (Test-Path $reportScript) {
        Write-Host "6. Rapport diagnostic..." -ForegroundColor Cyan
        & $reportScript -LogFile $latest.FullName
    }
    Write-Host ""
    Write-Host "=== WORKFLOW TERMINE ===" -ForegroundColor Green
    Write-Host "Log: $($latest.FullName)" -ForegroundColor Gray
} else {
    Write-Host "5. Analyse : non disponible pour ce projet (ou aucun log)." -ForegroundColor Gray
    Write-Host "=== WORKFLOW TERMINE ===" -ForegroundColor Green
    if ($latest) { Write-Host "Log: $($latest.FullName)" -ForegroundColor Gray }
}
