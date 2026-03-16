# Script d'analyse exhaustive des logs de monitoring
param(
    [string]$LogFile = ""
)

if ([string]::IsNullOrEmpty($LogFile)) {
    # Trouver le dernier fichier de log : d'abord dans logs/, puis racine
    $logsDir = Join-Path $PSScriptRoot "logs"
    $candidates = @()
    if (Test-Path $logsDir) {
        $candidates = @(Get-ChildItem -Path $logsDir -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $logsDir -Filter "monitor_*min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $logsDir -Filter "monitor_wroom_test_*.log" -ErrorAction SilentlyContinue)
    }
    if ($candidates.Count -eq 0) {
        $candidates = @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_wroom_test_*.log" -ErrorAction SilentlyContinue)
    }
    $latest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $LogFile = if ($latest) { $latest.FullName } else { $null }

    if (-not $LogFile) {
        Write-Host "Aucun fichier de log trouve" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== ANALYSE EXHAUSTIVE DU LOG ===" -ForegroundColor Green
Write-Host "Fichier: $LogFile" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "Fichier non trouve: $LogFile" -ForegroundColor Red
    exit 1
}

$content = Get-Content $LogFile -Raw
$lines = Get-Content $LogFile

Write-Host "STATISTIQUES GENERALES" -ForegroundColor Yellow
Write-Host "  Lignes totales: $($lines.Count)" -ForegroundColor White
Write-Host "  Taille: $([math]::Round((Get-Item $LogFile).Length / 1KB, 2)) KB" -ForegroundColor White
Write-Host ""

# 1. CRASHES ET ERREURS CRITIQUES
Write-Host "CRASHES ET ERREURS CRITIQUES" -ForegroundColor Red
# v11.172: Exclure diag_hasPanic (lecture NVS normale, pas un crash)
$crashes = $lines | Select-String -Pattern "Guru Meditation|panic|abort\(\)|Stack canary|CORRUPTED|Re-entered core dump" -CaseSensitive:$false | Where-Object { $_.Line -notmatch "diag_hasPanic" }
if ($crashes) {
    Write-Host "  Nombre de crashes: $($crashes.Count)" -ForegroundColor Red
    $crashes | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor Yellow }
} else {
    Write-Host "  Aucun crash detecte" -ForegroundColor Green
}
Write-Host ""

# 2. WATCHDOG ET TIMEOUTS
Write-Host "WATCHDOG ET TIMEOUTS" -ForegroundColor Yellow
$watchdog = $lines | Select-String -Pattern "Watchdog|timeout|TIMEOUT|WDT" -CaseSensitive:$false
if ($watchdog) {
    Write-Host "  Nombre d'occurrences: $($watchdog.Count)" -ForegroundColor Yellow
    $watchdog | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun probleme watchdog detecte" -ForegroundColor Green
}
Write-Host ""

# 3. PROBLEMES MEMOIRE
Write-Host "PROBLEMES MEMOIRE" -ForegroundColor Cyan
$heapLow = $lines | Select-String -Pattern "Heap.*low|heap.*critique|heap.*faible|Fragmentation|stack.*overflow|Stack canary" -CaseSensitive:$false
if ($heapLow) {
    Write-Host "  Nombre d'alertes mémoire: $($heapLow.Count)" -ForegroundColor Yellow
    $heapLow | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun probleme memoire detecte" -ForegroundColor Green
}

# Heap minimum
$heapMin = $lines | Select-String -Pattern "minHeap|min.*heap|Minimum.*Free" -CaseSensitive:$false
if ($heapMin) {
    Write-Host "  Heap minimum detecte:" -ForegroundColor Cyan
    $heapMin | Select-Object -First 5 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
}
Write-Host ""

# 4. PROBLEMES RESEAU
Write-Host "PROBLEMES RESEAU" -ForegroundColor Magenta
$networkIssues = $lines | Select-String -Pattern "WiFi.*fail|Connection.*fail|HTTP.*error|WebSocket.*error|TLS.*error|SSL.*error" -CaseSensitive:$false
if ($networkIssues) {
    Write-Host "  Nombre d'erreurs réseau: $($networkIssues.Count)" -ForegroundColor Yellow
    $networkIssues | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun probleme reseau detecte" -ForegroundColor Green
}
Write-Host ""

# 5. DATAQUEUE ET ROTATION
Write-Host "DATAQUEUE ET ROTATION" -ForegroundColor Blue
$dataQueue = $lines | Select-String -Pattern "DataQueue|Queue.*pleine|rotation" -CaseSensitive:$false
if ($dataQueue) {
    Write-Host "  Nombre d'occurrences: $($dataQueue.Count)" -ForegroundColor Cyan
    $dataQueue | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucune activite DataQueue detectee" -ForegroundColor Gray
}
Write-Host ""

# 6. REBOOTS ET RESETS
Write-Host "REBOOTS ET RESETS" -ForegroundColor DarkYellow
$reboots = $lines | Select-String -Pattern "Rebooting|rst:|Reset reason|BOOT|=== BOOT" -CaseSensitive:$false
if ($reboots) {
    Write-Host "  Nombre de reboots: $($reboots.Count)" -ForegroundColor Yellow
    $reboots | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun reboot detecte" -ForegroundColor Green
}
Write-Host ""

# 7. VERSION ET BUILD INFO
Write-Host "VERSION ET BUILD INFO" -ForegroundColor Green
$version = $lines | Select-String -Pattern "v11\.|Version|Compile Date" -CaseSensitive:$false
if ($version) {
    $version | Select-Object -First 5 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
}
Write-Host ""

# 8. ERREURS NVS
Write-Host "ERREURS NVS" -ForegroundColor DarkCyan
$nvsErrors = $lines | Select-String -Pattern "NVS.*error|NVS.*fail|nvs_get.*fail|saveString.*fail" -CaseSensitive:$false
if ($nvsErrors) {
    Write-Host "  Nombre d'erreurs NVS: $($nvsErrors.Count)" -ForegroundColor Yellow
    $nvsErrors | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucune erreur NVS detectee" -ForegroundColor Green
}
Write-Host ""

# 9. SEQUENCE DE DEMARRAGE
Write-Host "SEQUENCE DE DEMARRAGE" -ForegroundColor DarkGreen
$bootSequence = $lines | Select-String -Pattern "BOOT|Initialisation|Setup|begin\(\)" -CaseSensitive:$false | Select-Object -First 20
if ($bootSequence) {
    Write-Host "  Premieres etapes de demarrage:" -ForegroundColor Cyan
    $bootSequence | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
}
Write-Host ""

# 10. RESUME FINAL (ASCII pour eviter ParseError encodage)
Write-Host "=== RESUME FINAL ===" -ForegroundColor Green
$summary = @{
    "Crashes"  = if ($crashes)      { "Oui " + $crashes.Count + " crash(es)" } else { "Aucun crash" }
    "Watchdog" = if ($watchdog)     { "Oui " + $watchdog.Count + " alerte(s)" } else { "OK" }
    "Memoire"  = if ($heapLow)      { "Oui " + $heapLow.Count + " alerte(s)" } else { "OK" }
    "Reseau"   = if ($networkIssues) { "Oui " + $networkIssues.Count + " erreur(s)" } else { "OK" }
    "DataQueue" = if ($dataQueue)  { [string]$dataQueue.Count + " evenement(s)" } else { "Aucun evenement" }
    "Reboots"  = if ($reboots)     { "Oui " + $reboots.Count + " reboot(s)" } else { "Aucun reboot" }
    "NVS"      = if ($nvsErrors)   { "Oui " + $nvsErrors.Count + " erreur(s)" } else { "OK" }
}

$summary.GetEnumerator() | ForEach-Object {
    Write-Host "  $($_.Key): $($_.Value)" -ForegroundColor White
}

Write-Host ""
Write-Host '=== ANALYSE TERMINEE ===' -ForegroundColor Green
