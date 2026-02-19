# Workflow: Erase all -> Flash firmware (et FS sauf wroom-prod) -> Monitor N min -> Analyse
# Pour wroom-prod : erase + firmware uniquement (pas de LittleFS, pas de serveur embarqué).
# Fermer tout moniteur série (pio device monitor, etc.) avant de lancer.
#
# Usage:
#   .\erase_flash_fs_monitor_5min_analyze.ps1
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -Port COM4
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-prod -Port COM5
#
# Durée d'enregistrement du log :
#   Défaut 5 min :  .\erase_flash_fs_monitor_5min_analyze.ps1
#   30 min (recommandé avant release / déploiement hardware) :
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -DurationMinutes 30
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -DurationMinutes 30 -Port COM4
#
# S3 : le build (étape 0) prend 20–40 min. Si le firmware est déjà compilé, sauter le rebuild :
#   .\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM4 -SkipBuild

param(
    [string]$Port = "",
    [string]$Environment = "wroom-test",
    [int]$DurationMinutes = 5,
    [switch]$SkipBuild = $false
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

$durationSeconds = $DurationMinutes * 60
Write-Host "=== WORKFLOW: ERASE ALL / FLASH ALL / MONITOR ${DurationMinutes}MIN / ANALYSE ===" -ForegroundColor Green
Write-Host "Environnement: $Environment" -ForegroundColor Cyan
Write-Host "Monitoring: $DurationMinutes min ($durationSeconds s)" -ForegroundColor Cyan
if ($Port) {
    Write-Host "Port série: $Port" -ForegroundColor Cyan
    $env:PLATFORMIO_UPLOAD_PORT = $Port
}
Write-Host "Fermez tout moniteur série sur l'ESP32 avant de continuer." -ForegroundColor Yellow
Write-Host ""

# 0. Pour S3 (test et prod) : patches + fullclean + build (fix TG1WDT boot loop, libs recompilées)
# Si WinError 32 sur ArduinoJson/libdeps: fermer Cursor, PowerShell externe, .\tools\clean_s3_build.ps1 puis pio run.
# -SkipBuild : saute cette étape (erase + flash + monitor + analyse uniquement, build déjà fait).
if (($Environment -eq "wroom-s3-test" -or $Environment -eq "wroom-s3-prod") -and -not $SkipBuild) {
    Write-Host "0. Préparation S3 test/prod (patches + fullclean + build, fix TG1WDT)..." -ForegroundColor Yellow
    python tools/patch_espressif32_custom_sdkconfig_only.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/patch_arduino_early_init_variant.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/patch_arduino_diag_after_nvs.py
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    python tools/patch_arduino_libs_s3_wdt.py
    # Le patch peut échouer si le package n'est pas encore installé (sera appliqué lors du build)
    if ($LASTEXITCODE -ne 0) { 
        Write-Host "   WARN: patch_arduino_libs_s3_wdt échoué (package peut ne pas être installé, sera appliqué lors du build)" -ForegroundColor Yellow 
    }
    # Vérifier CONFIG_SPIRAM_BOOT_INIT (évite psramAddToHeap blocage S3)
    $sdkWdt = Join-Path $projectRoot "sdkconfig_s3_wdt.txt"
    if (-not (Select-String -Path $sdkWdt -Pattern "CONFIG_SPIRAM_BOOT_INIT=y" -Quiet)) {
        Write-Host "   WARN: CONFIG_SPIRAM_BOOT_INIT=y absent de sdkconfig_s3_wdt.txt" -ForegroundColor Red
        Write-Host "   Risque de blocage boot (psramAddToHeap dans initArduino)" -ForegroundColor Red
    }
    $pkgDirs = @(
        (Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32-libs\sdkconfig"),
        (Join-Path $projectRoot ".platformio\packages\framework-arduinoespressif32-libs\sdkconfig")
    )
    foreach ($sdk in $pkgDirs) {
        if (Test-Path $sdk) { Remove-Item $sdk -Force; Write-Host "   sdkconfig supprimé (force recompil libs)" -ForegroundColor Gray }
    }
    # Force-suppression build/libdeps S3 si fullclean échoue (fichiers verrouillés)
    & (Join-Path $projectRoot "tools\clean_s3_build.ps1")
    $prevErr = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    pio run -e $Environment -t fullclean 2>&1 | Out-Null
    $ErrorActionPreference = $prevErr
    Write-Host "   Build S3 (coredump désactivé)..." -ForegroundColor Gray
    pio run -e $Environment
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Write-Host ""
}

# 1. Erase (retry si port occupé)
$maxRetries = if ($Port) { 3 } else { 1 }
$eraseOk = $false
for ($r = 1; $r -le $maxRetries; $r++) {
    Write-Host "1. Erase complète flash..." -ForegroundColor Cyan
    if ($r -gt 1) { Write-Host "   Tentative $r/$maxRetries (attente 8s)..." -ForegroundColor Gray; Start-Sleep -Seconds 8 }
    pio run -e $Environment --target erase
    if ($LASTEXITCODE -eq 0) { $eraseOk = $true; break }
    if ($Port -and $r -lt $maxRetries) { Write-Host "   Port occupé? Fermez le moniteur série puis relance." -ForegroundColor Yellow }
}
if (-not $eraseOk) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 2. Upload firmware
Write-Host "2. Flash firmware ($Environment)..." -ForegroundColor Cyan
pio run -e $Environment --target upload
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 3

# 3. Upload FS (sauf wroom-prod : pas de serveur embarqué, inutile)
if ($Environment -ne "wroom-prod") {
    Write-Host "3. Flash LittleFS..." -ForegroundColor Cyan
    pio run -e $Environment --target uploadfs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Start-Sleep -Seconds 2
} else {
    Write-Host "3. LittleFS ignoré (wroom-prod = pas de serveur embarqué)" -ForegroundColor Gray
}

# 4. Monitoring N min (réutiliser le port d'upload si connu, pour éviter moniteur sur un autre COM)
$monitorPort = $Port
if (-not $monitorPort -and $env:PLATFORMIO_UPLOAD_PORT) { $monitorPort = $env:PLATFORMIO_UPLOAD_PORT }
Write-Host "4. Monitoring $DurationMinutes minutes..." -ForegroundColor Cyan
$monitorParams = @{ DurationSeconds = $durationSeconds }
if ($monitorPort) { $monitorParams["Port"] = $monitorPort }
& "$projectRoot\monitor_5min.ps1" @monitorParams
# Fichier créé par cette exécution = le plus récent monitor_*_*.log par date de création
$latest = Get-ChildItem -Path $projectRoot -Filter "monitor_*.log" -ErrorAction SilentlyContinue | Sort-Object CreationTime -Descending | Select-Object -First 1
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
