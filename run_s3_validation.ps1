# Validation boot S3 (TG1WDT): patch + build + workflow 1 min
# Usage: .\run_s3_validation.ps1 [-Port COM7]
param([string]$Port = "COM7")

$ErrorActionPreference = "Stop"
$envName = "wroom-s3-test"

Write-Host "=== Validation S3 (patch + build + erase + flash + monitor 1 min) ===" -ForegroundColor Cyan
Write-Host ""

# 1. Patch plateforme (custom_sdkconfig sans espidf) + diagnostic
Write-Host "1. Patch plateforme..." -ForegroundColor Yellow
python tools/patch_espressif32_custom_sdkconfig_only.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 1b. Patch core Arduino : earlyInitVariant() au début de initArduino() (TG0WDT)
Write-Host "1b. Patch early init variant (core Arduino)..." -ForegroundColor Yellow
python tools/patch_arduino_early_init_variant.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 1c. Patch core Arduino : ffp5cs_diag_after_nvs() après nvs_flash_init (diagnostic boot)
Write-Host "1c. Patch diagnostic after NVS (core Arduino)..." -ForegroundColor Yellow
python tools/patch_arduino_diag_after_nvs.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 2. Patch sdkconfig source du package Arduino (optionnel, pour futurs rebuilds)
Write-Host "2. Patch sdkconfig package Arduino S3..." -ForegroundColor Yellow
python tools/patch_arduino_libs_s3_wdt.py

# 2b. Supprimer sdkconfig racine package (forcer call_compile_libs = recompil libs IWDT)
$pkgDirs = @(
    (Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32-libs\sdkconfig"),
    (Join-Path $PSScriptRoot ".platformio\packages\framework-arduinoespressif32-libs\sdkconfig")
)
$removed = $false
foreach ($sdk in $pkgDirs) {
    if (Test-Path $sdk) {
        Remove-Item $sdk -Force
        Write-Host "   sdkconfig supprimé: $sdk" -ForegroundColor Gray
        $removed = $true
    }
}
if (-not $removed) { Write-Host "   sdkconfig racine absent (OK)" -ForegroundColor Gray }

# 2c. Fullclean pour forcer rebuild complet (incl. libs Arduino)
# Non bloquant : si Accès refusé (fichier verrouillé par IDE), on continue
Write-Host "2c. Fullclean..." -ForegroundColor Yellow
$prevErr = $ErrorActionPreference
$ErrorActionPreference = "SilentlyContinue"
pio run -e $envName -t fullclean 2>&1 | Out-Null
$ErrorActionPreference = $prevErr

# 3. Build (capture pour vérifier "*** Compile Arduino IDF libs ***")
# Si LockFileTimeoutError : fermer Cursor/IDE, lancer depuis terminal externe
# ErrorAction Continue : éviter que les warnings compilateur (stderr) fassent échouer le script
Write-Host "3. Build $envName (sortie dans build_s3_validation.txt)..." -ForegroundColor Yellow
$prevErrAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
pio run -e $envName 2>&1 | Tee-Object -FilePath build_s3_validation.txt
$buildExit = $LASTEXITCODE
$ErrorActionPreference = $prevErrAction
if ($buildExit -ne 0) { Write-Host "Build FAILED"; exit $buildExit }

# Vérifier si la lib Arduino a été recompilée (correctif IWDT pris en compte)
$buildLog = Get-Content build_s3_validation.txt -Raw
if ($buildLog -match "Compile Arduino IDF libs") { Write-Host "  [OK] Lib Arduino recompilee (IWDT 300s)" -ForegroundColor Green }
elseif ($buildLog -match "FFP5CS S3:") { Write-Host "  [INFO] Diagnostic present; chercher 'Compile Arduino IDF' dans build_s3_validation.txt" -ForegroundColor Yellow }
else { Write-Host "  [ATTENTION] Aucun message 'Compile Arduino IDF' - boot loop possible" -ForegroundColor Yellow }

# 4. Workflow erase + flash + monitor 1 min
Write-Host "4. Workflow erase + flash + monitor 1 min (Port $Port)..." -ForegroundColor Yellow
.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment $envName -Port $Port -DurationMinutes 1
if ($LASTEXITCODE -ne 0) { Write-Host "Workflow FAILED"; exit $LASTEXITCODE }

# 5. Vérifier le dernier log monitor (BOOT FFP5CS, TG1WDT, TG0WDT)
$log = Get-ChildItem -Filter "monitor_1min_*.log" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($log) {
    $content = Get-Content $log.FullName -Raw
    $bootOk = $content -match "BOOT FFP5CS"
    $tg1Count = ([regex]::Matches($content, "TG1WDT_SYS_RST")).Count
    $tg0Count = ([regex]::Matches($content, "TG0WDT_SYS_RST")).Count
    if ($bootOk -and $tg1Count -eq 0 -and $tg0Count -eq 0) { Write-Host "  [OK] Boot FFP5CS vu, aucun WDT" -ForegroundColor Green }
    elseif ($bootOk -and $tg0Count -gt 0) { Write-Host "  [PARTIEL] Boot OK mais $tg0Count reset(s) TG0WDT" -ForegroundColor Yellow }
    elseif ($bootOk) { Write-Host "  [PARTIEL] Boot OK mais $tg1Count reset(s) TG1WDT" -ForegroundColor Yellow }
    elseif ($tg0Count -gt 0) { Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg0Count TG0WDT (task WDT) - boot loop" -ForegroundColor Red }
    else { Write-Host "  [ECHEC] Pas de 'BOOT FFP5CS', $tg1Count TG1WDT - boot loop" -ForegroundColor Red }
    Write-Host "  Log: $($log.FullName)"
}

Write-Host ""
Write-Host "=== Fin validation ===" -ForegroundColor Cyan
