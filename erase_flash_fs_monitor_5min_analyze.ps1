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
# -SkipBuild : sauter l'étape build (erase + flash + monitor + analyse uniquement).
#
# Quand -SkipBuild n'est pas utilisé, un clean est exécuté avant le build pour éviter les erreurs
# "No such file or directory" (.d / .sconsign) après libération du port COM (processus moniteur arrêtés).
#
# Sous Windows, le build utilise -j 1 (séquentiel) pour éviter les erreurs "No such file or directory"
# (fichiers .d/.o/tmp et chemins longs avec libs type ESP Mail Client).
#
# Si -Port est fourni, le script tente de liberer le port COM avant erase (arret des processus
# python / pio susceptibles de tenir le moniteur serie). Limite aux processus moniteur, pas de kill systeme.

param(
    [string]$Port = "",
    [string]$Environment = "wroom-test",
    [int]$DurationMinutes = 5,
    [switch]$SkipBuild = $false
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

function Release-ComPortIfNeeded {
    param([string]$Port = "")
    if (-not $Port) { return }
    # Tenter d'ouvrir le port : si succes, il est libre
    try {
        $sp = [System.IO.Ports.SerialPort]::new($Port, 115200)
        $sp.Open()
        $sp.Close()
        $sp.Dispose()
        Write-Host "Port $Port deja libre." -ForegroundColor Green
        return
    } catch {
        # Port occupe ou inaccessible : arreter les processus typiques du moniteur
    }
    Write-Host "Liberation du port $Port (arret processus moniteur)..." -ForegroundColor Yellow
    $killed = 0
    try {
        $procs = Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ProcessName -like "*python*" -or $_.ProcessName -like "*pio*" -or
            ($_.MainWindowTitle -and ($_.MainWindowTitle -like "*monitor*" -or $_.MainWindowTitle -like "*serial*"))
        }
        foreach ($p in $procs) {
            try {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
                Write-Host "  Arrete: $($p.ProcessName) (PID $($p.Id))" -ForegroundColor Gray
                $killed++
            } catch { }
        }
        if ($killed -gt 0) {
            Start-Sleep -Seconds 3
            Write-Host "Port $Port : $killed processus arrete(s)." -ForegroundColor Green
        } else {
            Write-Host "Aucun processus moniteur detecte. Verifiez que le port est libre." -ForegroundColor Yellow
        }
    } catch {
        Write-Host "Erreur liberation port: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

$durationSeconds = $DurationMinutes * 60
Write-Host "=== WORKFLOW: ERASE ALL / FLASH ALL / MONITOR ${DurationMinutes}MIN / ANALYSE ===" -ForegroundColor Green
Write-Host "Environnement: $Environment" -ForegroundColor Cyan
Write-Host "Monitoring: $DurationMinutes min ($durationSeconds s)" -ForegroundColor Cyan
if ($Port) {
    Write-Host "Port série: $Port" -ForegroundColor Cyan
    $env:PLATFORMIO_UPLOAD_PORT = $Port
}
Write-Host "Fermez tout moniteur série sur l'ESP32 avant de continuer." -ForegroundColor Yellow
if ($Port) {
    Release-ComPortIfNeeded -Port $Port
}
Write-Host ""

# 0. Build (tous environnements, comme wroom-test) — sauf si -SkipBuild
if (-not $SkipBuild) {
    Write-Host "0. Build $Environment..." -ForegroundColor Cyan
    # Clean avant build pour éviter corruption cache (.d / .sconsign) après libération du port COM.
    Write-Host "   Clean (évite erreurs .d / sconsign après libération port)..." -ForegroundColor Gray
    pio run -e $Environment -t clean 2>&1 | Out-Null
    # Sous Windows, build séquentiel (-j 1) pour éviter erreurs "No such file or directory" (fichiers .d/.o/tmp et chemins longs).
    $buildJobs = if ($env:OS -eq "Windows_NT") { "-j", "1" } else { @() }
    pio run -e $Environment @buildJobs
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
