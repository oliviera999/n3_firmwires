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
# -NoPrompt : ne pas attendre Entrée avant erase/flash (pour exécution non interactive, ex. Cursor/CI).
# -SkipBuild : sauter l'étape build (erase + flash + monitor + analyse uniquement).
# -SkipClean : ne pas lancer "pio run -t clean" avant le build (si le clean reste bloqué, utiliser ce flag).
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
    [switch]$NoPrompt = $false,
    [switch]$SkipBuild = $false,
    [switch]$SkipClean = $false
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

. (Join-Path $PSScriptRoot "scripts\Release-ComPort.ps1")

function Get-PioCliCommand {
    $pioCmd = Get-Command "pio" -ErrorAction SilentlyContinue
    if ($pioCmd) { return "pio" }
    $platformioCmd = Get-Command "platformio" -ErrorAction SilentlyContinue
    if ($platformioCmd) { return "platformio" }
    throw "Ni 'pio' ni 'platformio' ne sont disponibles dans le PATH."
}

function Get-DeclaredPioEnvs {
    param([string]$IniPath)
    if (-not (Test-Path -LiteralPath $IniPath)) { return @() }
    $lines = Get-Content -LiteralPath $IniPath -ErrorAction SilentlyContinue
    $envs = @()
    foreach ($line in $lines) {
        if ($line -match '^\[env:([^\]]+)\]') {
            $envs += $Matches[1]
        }
    }
    return $envs
}

function Test-ComPortExists {
    param([string]$PortName)
    if (-not $PortName) { return $true }
    try {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        return $ports -contains $PortName
    } catch {
        return $true
    }
}

$durationSeconds = $DurationMinutes * 60
$pioCli = Get-PioCliCommand
$declaredEnvs = Get-DeclaredPioEnvs -IniPath (Join-Path $projectRoot "platformio.ini")
if ($Environment -notin $declaredEnvs) {
    Write-Host "Erreur: environnement inconnu '$Environment' dans platformio.ini" -ForegroundColor Red
    exit 1
}
Write-Host "=== WORKFLOW: ERASE ALL / FLASH ALL / MONITOR ${DurationMinutes}MIN / ANALYSE ===" -ForegroundColor Green
Write-Host "Environnement: $Environment" -ForegroundColor Cyan
Write-Host "Monitoring: $DurationMinutes min ($durationSeconds s)" -ForegroundColor Cyan
if ($Port) {
    if (-not (Test-ComPortExists -PortName $Port)) {
        Write-Host "Erreur: port série '$Port' non détecté. Branche l'ESP puis relance." -ForegroundColor Red
        exit 1
    }
    Write-Host "Port série: $Port" -ForegroundColor Cyan
    $env:PLATFORMIO_UPLOAD_PORT = $Port
}
Write-Host "Fermez tout moniteur série sur l'ESP32 avant de continuer." -ForegroundColor Yellow
if ($Port) {
    Release-ComPortIfNeeded -Port $Port
}
Write-Host ""

# Attente mode download (erase/flash) : évite "Wrong boot mode" si la carte boote avant le run (sauf si -NoPrompt)
if (-not $NoPrompt) {
    Write-Host "Pour erase/flash: mettez l'ESP32 en mode download (BOOT enfoncé + EN/RST), puis appuyez sur Entrée..." -ForegroundColor Yellow
    $null = Read-Host
} else {
    Write-Host "Mode non interactif (-NoPrompt): mettez l'ESP32 en mode download avant le début de l'erase." -ForegroundColor Yellow
}
Write-Host ""

# 0. Build (tous environnements, comme wroom-test) — sauf si -SkipBuild
if (-not $SkipBuild) {
    Write-Host "0. Build $Environment..." -ForegroundColor Cyan
    if (-not $SkipClean) {
        Write-Host "   Clean..." -ForegroundColor Gray
        & $pioCli run -e $Environment -t clean
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } else {
        Write-Host "   Clean ignoré (-SkipClean)" -ForegroundColor Gray
    }
    # Sous Windows, build séquentiel (-j 1) pour éviter erreurs "No such file or directory" (fichiers .d/.o/tmp et chemins longs).
    $buildJobs = if ($env:OS -eq "Windows_NT") { "-j", "1" } else { @() }
    Write-Host "   Build en cours (plusieurs minutes avec -j 1)..." -ForegroundColor Gray
    & $pioCli run -e $Environment @buildJobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Write-Host ""
}

# 1. Erase (retry si port occupé)
$maxRetries = if ($Port) { 3 } else { 1 }
$eraseOk = $false
$eraseErr = ""
for ($r = 1; $r -le $maxRetries; $r++) {
    Write-Host "1. Erase complète flash..." -ForegroundColor Cyan
    if ($r -gt 1) { Write-Host "   Tentative $r/$maxRetries (attente 8s)..." -ForegroundColor Gray; Start-Sleep -Seconds 8 }
    $eraseErr = & $pioCli run -e $Environment --target erase 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) { $eraseOk = $true; break }
    if ($Port -and $r -lt $maxRetries) {
        if ($eraseErr -match "Wrong boot mode|download mode") {
            Write-Host "   ESP32 en mode normal: maintenez BOOT enfoncé, appuyez sur EN/RST, puis relancez." -ForegroundColor Yellow
        } else {
            Write-Host "   Port occupé? Fermez le moniteur série puis relance." -ForegroundColor Yellow
        }
    }
}
if (-not $eraseOk) {
    if ($eraseErr -match "Wrong boot mode|download mode") {
        Write-Host "" ; Write-Host "ASTUCE: Mettez l'ESP32 en mode download (maintenez BOOT, appuyez EN/RST), puis relancez ce script." -ForegroundColor Yellow
    }
    exit $LASTEXITCODE
}
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 2

# 2. Upload firmware
Write-Host "2. Flash firmware ($Environment)..." -ForegroundColor Cyan
& $pioCli run -e $Environment --target upload
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "   OK" -ForegroundColor Green
Start-Sleep -Seconds 3

# 3. Upload FS (sauf wroom-prod et wroom-beta : pas de serveur embarqué, inutile)
if ($Environment -ne "wroom-prod" -and $Environment -ne "wroom-beta") {
    Write-Host "3. Flash LittleFS..." -ForegroundColor Cyan
    & $pioCli run -e $Environment --target uploadfs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "   OK" -ForegroundColor Green
    Start-Sleep -Seconds 2
} else {
    Write-Host "3. LittleFS ignoré ($Environment = pas de serveur embarqué)" -ForegroundColor Gray
}

# 4. Monitoring N min (réutiliser le port d'upload si connu, pour éviter moniteur sur un autre COM)
$monitorPort = $Port
if (-not $monitorPort -and $env:PLATFORMIO_UPLOAD_PORT) { $monitorPort = $env:PLATFORMIO_UPLOAD_PORT }
Write-Host "4. Monitoring $DurationMinutes minutes..." -ForegroundColor Cyan
$monitorParams = @{ DurationSeconds = $durationSeconds; Environment = $Environment }
if ($monitorPort) { $monitorParams["Port"] = $monitorPort }
& "$projectRoot\monitor_5min.ps1" @monitorParams
# Fichier créé par cette exécution = le plus récent monitor_*.log dans logs/ (dossier dédié par firmware)
$logsDir = Join-Path $projectRoot "logs"
$latest = Get-ChildItem -Path $logsDir -Filter "monitor_*.log" -ErrorAction SilentlyContinue | Sort-Object CreationTime -Descending | Select-Object -First 1
$logFullPath = if ($latest) { $latest.FullName } else { $null }

# 5. Analyse du log (analyse_log + rapport diagnostic complet)
if ($logFullPath -and (Test-Path $logFullPath)) {
    $logFile = $latest.Name
    $analysisFileName = $logFile -replace '\.log$', '_analysis.txt'
    $analysisFullPath = Join-Path $logsDir $analysisFileName

    Write-Host "5. Analyse du log: $logFile" -ForegroundColor Cyan
    & "$projectRoot\analyze_log.ps1" -logFile $logFullPath
    Write-Host ""
    Write-Host "6. Rapport diagnostic complet (serveur distant, mails, exhaustive)..." -ForegroundColor Cyan
    & "$projectRoot\generate_diagnostic_report.ps1" -LogFile $logFullPath -LogsDir $logsDir
    Write-Host ""
    Write-Host "=== WORKFLOW TERMINÉ ===" -ForegroundColor Green
    Write-Host "Log:           $logFullPath" -ForegroundColor Gray
    Write-Host "Analyse:       $analysisFullPath" -ForegroundColor Gray
    Write-Host "Rapport MD:    $logsDir\rapport_diagnostic_complet_*.md (dernier créé)" -ForegroundColor Gray
} else {
    Write-Host "5. Aucun fichier log trouvé dans $logsDir pour l'analyse." -ForegroundColor Yellow
}
