# =============================================================================
# Analyse generique des logs de monitoring - Firmwares n3 IoT
# =============================================================================
# Detecte : crashes, watchdog, memoire, reseau, reboots, NVS.
# Utilise par monitor_Nmin.ps1 et erase_flash_monitor.ps1 pour n3pp4_2,
# msp2_5, uploadphotosserver (ffp5cs a son analyse dediee).
#
# Usage:
#   .\analyze_log_generic.ps1 -LogFile monitor_5min_2026-03-10.log
#   .\analyze_log_generic.ps1   # cherche le dernier monitor_*.log dans le repertoire courant
# =============================================================================

param(
    [string]$LogFile = ""
)

if ([string]::IsNullOrEmpty($LogFile)) {
    $candidates = @(Get-ChildItem -Filter "monitor_*.log" -ErrorAction SilentlyContinue)
    $latest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $LogFile = if ($latest) { $latest.FullName } else { $null }

    if (-not $LogFile) {
        Write-Host "Aucun fichier monitor_*.log trouve dans le repertoire courant." -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== ANALYSE GENERIQUE DU LOG ===" -ForegroundColor Green
Write-Host "Fichier: $LogFile" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "Fichier non trouve: $LogFile" -ForegroundColor Red
    exit 1
}

$lines = Get-Content $LogFile -ErrorAction SilentlyContinue
if (-not $lines) {
    Write-Host "Fichier vide ou illisible." -ForegroundColor Red
    exit 1
}

Write-Host "STATISTIQUES GENERALES" -ForegroundColor Yellow
Write-Host "  Lignes totales: $($lines.Count)" -ForegroundColor White
Write-Host "  Taille: $([math]::Round((Get-Item $LogFile).Length / 1KB, 2)) KB" -ForegroundColor White
Write-Host ""

# 1. CRASHES ET ERREURS CRITIQUES
Write-Host "CRASHES ET ERREURS CRITIQUES" -ForegroundColor Red
$crashes = $lines | Select-String -Pattern "Guru Meditation|panic|abort\(\)|Stack canary|CORRUPTED|Re-entered core dump" -CaseSensitive:$false
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
    Write-Host "  Nombre d'alertes memoire: $($heapLow.Count)" -ForegroundColor Yellow
    $heapLow | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun probleme memoire detecte" -ForegroundColor Green
}
Write-Host ""

# 4. PROBLEMES RESEAU
Write-Host "PROBLEMES RESEAU" -ForegroundColor Magenta
$networkIssues = $lines | Select-String -Pattern "WiFi.*fail|Connection.*fail|HTTP.*error|WebSocket.*error|TLS.*error|SSL.*error" -CaseSensitive:$false
if ($networkIssues) {
    Write-Host "  Nombre d'erreurs reseau: $($networkIssues.Count)" -ForegroundColor Yellow
    $networkIssues | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun probleme reseau detecte" -ForegroundColor Green
}
Write-Host ""

# 5. REBOOTS ET RESETS
Write-Host "REBOOTS ET RESETS" -ForegroundColor DarkYellow
$reboots = $lines | Select-String -Pattern "Rebooting|rst:|Reset reason|BOOT|=== BOOT" -CaseSensitive:$false
if ($reboots) {
    Write-Host "  Nombre de reboots: $($reboots.Count)" -ForegroundColor Yellow
    $reboots | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucun reboot detecte" -ForegroundColor Green
}
Write-Host ""

# 6. ERREURS NVS
Write-Host "ERREURS NVS" -ForegroundColor DarkCyan
$nvsErrors = $lines | Select-String -Pattern "NVS.*error|NVS.*fail|nvs_get.*fail|saveString.*fail" -CaseSensitive:$false
if ($nvsErrors) {
    Write-Host "  Nombre d'erreurs NVS: $($nvsErrors.Count)" -ForegroundColor Yellow
    $nvsErrors | Select-Object -First 10 | ForEach-Object { Write-Host "    - $($_.Line.Trim())" -ForegroundColor White }
} else {
    Write-Host "  Aucune erreur NVS detectee" -ForegroundColor Green
}
Write-Host ""

# RESUME FINAL
Write-Host "=== RESUME ===" -ForegroundColor Green
$summary = @{
    "Crashes"  = if ($crashes)       { "Oui ($($crashes.Count))" } else { "Aucun" }
    "Watchdog" = if ($watchdog)      { "Oui ($($watchdog.Count))" } else { "OK" }
    "Memoire"  = if ($heapLow)       { "Oui ($($heapLow.Count))" } else { "OK" }
    "Reseau"   = if ($networkIssues) { "Oui ($($networkIssues.Count))" } else { "OK" }
    "Reboots"  = if ($reboots)       { "Oui ($($reboots.Count))" } else { "Aucun" }
    "NVS"      = if ($nvsErrors)     { "Oui ($($nvsErrors.Count))" } else { "OK" }
}
$summary.GetEnumerator() | ForEach-Object {
    Write-Host "  $($_.Key): $($_.Value)" -ForegroundColor White
}
Write-Host ""
Write-Host "=== ANALYSE TERMINEE ===" -ForegroundColor Green
