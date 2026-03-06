# Agrège les métriques de plusieurs logs de monitoring (tranches) et produit un rapport unique.
# Réutilise les mêmes critères que analyze_log.ps1, en lecture flux pour limiter la mémoire.
#
# Usage:
#   .\aggregate_monitor_report.ps1 -LogFolder monitor_long_24h_2025-02-28_12-00 -OutputReport RAPPORT_24h_2025-02-28.md
#   .\aggregate_monitor_report.ps1 -LogFiles part_001.log,part_002.log -OutputReport RAPPORT.md

param(
    [string]$LogFolder = "",
    [string[]]$LogFiles = @(),
    [string]$OutputReport = ""
)

$ErrorActionPreference = "Stop"

function Get-CountsFromLogFile {
    param([string]$Path)
    $c = @{
        LineCount = 0
        JsonParseSuccess = 0
        JsonParseErrors = 0
        PostSends = 0
        PostDiagnostic = 0
        PostErrors = 0
        HeapChecks = 0
        LowHeapWarnings = 0
        Dht22Disabled = 0
        Dht22Failures = 0
        QueueFullErrors = 0
        BranchData = 0
        BranchTimeout = 0
        NetRpcTimeouts = 0
        WifiDisconnect = 0
        WifiConnect = 0
        GetOutputsState = 0
        SyncEnvoiPost = 0
        OtaCheckRequested = 0
        BootCount = 0
        SwCpuResetCount = 0
        PowerOnCount = 0
        WdtCount = 0
        GuruMeditation = 0
        Panic = 0
        Brownout = 0
        StackOverflow = 0
        UnwantedReboots = 0
        Warnings = 0
        Errors = 0
        FwVersion = ""
    }
    if (-not (Test-Path $Path)) { return $c }
    Get-Content $Path -Encoding UTF8 -ErrorAction SilentlyContinue | ForEach-Object {
        $line = $_
        $c.LineCount++
        if ($line -match 'BOOT FFP5CS (v\d+\.\d+)' -and -not $c.FwVersion) { $c.FwVersion = $matches[1] }
        if ($line -match '\[GPIOParser\].*PARSING.*SERVEUR|\[HTTP\] Utilisation cache NVS') { $c.JsonParseSuccess++ }
        if ($line -match '\[HTTP\] JSON parse error|JSON parse error') { $c.JsonParseErrors++ }
        if ($line -match '\[PR\]|POST.*sent|\[Sync\].*envoi POST|\[HTTP\].*POST|POST.*réussi') { $c.PostSends++ }
        if ($line -match '\[Sync\] Diagnostic|\[PR\] === DÉBUT POSTRAW') { $c.PostDiagnostic++ }
        if ($line -match 'POST.*échec|POST.*error|POST.*failed') { $c.PostErrors++ }
        if ($line -match 'Heap.*libre|Heap.*minimum|\[autoTask\].*Heap|CRITICAL.*Heap|Free heap') { $c.HeapChecks++ }
        if ($line -match 'CRITICAL.*Heap|WARN.*Heap.*faible|heap.*low|Heap.*critique') { $c.LowHeapWarnings++ }
        if ($line -match 'DHT22 désactivé|sensorDisabled|Capteur.*désactivé') { $c.Dht22Disabled++ }
        if ($line -match 'AirSensor.*échec|DHT.*non détecté|Capteur DHT.*déconnecté') { $c.Dht22Failures++ }
        if ($line -match 'Queue pleine|queue.*full') { $c.QueueFullErrors++ }
        if ($line -match '"branch"\s*:\s*"data"') { $c.BranchData++ }
        if ($line -match '"branch"\s*:\s*"timeout"') { $c.BranchTimeout++ }
        if ($line -match '\[netRPC\]\s*Timeout') { $c.NetRpcTimeouts++ }
        if ($line -match 'WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue|\[HTTP\] WiFi (déconnecté|perdu)') { $c.WifiDisconnect++ }
        if ($line -match '\[WiFi\]\s*OK|WiFi.*connect.*OK|Connexion.*reussie|\[Event\] WiFi.*connect') { $c.WifiConnect++ }
        if ($line -match 'outputs/state:\s*code=') { $c.GetOutputsState++ }
        if ($line -match '\[Sync\].*envoi POST') { $c.SyncEnvoiPost++ }
        if ($line -match 'triggerOtaCheck|demande vérification OTA|Demande distante recherche OTA') { $c.OtaCheckRequested++ }
        if ($line -match 'rst:0x[0-9a-f]+?\s*\(') { $c.BootCount++ }
        if ($line -match 'rst:0xc\s*\(SW_CPU_RESET\)') { $c.SwCpuResetCount++ }
        if ($line -match 'rst:0x1\s*\(POWERON_RESET\)') { $c.PowerOnCount++ }
        if ($line -match 'rst:0x[0-9a-f]+.*(Watchdog|wdt|WDT)') { $c.WdtCount++ }
        if ($line -match 'Guru Meditation') { $c.GuruMeditation++ }
        if ($line -match "panic'ed|panic\s*\(") { $c.Panic++ }
        if ($line -match 'Brownout') { $c.Brownout++ }
        if ($line -match 'Stack overflow|STACK OVERFLOW') { $c.StackOverflow++ }
        if ($line -match '\[W\]|WARN|WARNING') { $c.Warnings++ }
        if ($line -match '\[E\]|ERROR|ERREUR') { $c.Errors++ }
    }
    $c.RebootsAfterFirst = if ($c.BootCount -gt 0) { $c.BootCount - 1 } else { 0 }
    $c.UnwantedReboots = $c.RebootsAfterFirst
    if ($c.SwCpuResetCount -gt 0 -and $c.UnwantedReboots -eq 0) { $c.UnwantedReboots = $c.SwCpuResetCount }
    return $c
}

function Add-Counts {
    param($Sum, $Delta)
    $keys = $Sum.Keys | Where-Object { $_ -notin @('FwVersion') }
    foreach ($k in $keys) {
        if ($Sum[$k] -is [int] -and $Delta[$k] -is [int]) {
            $Sum[$k] += $Delta[$k]
        }
    }
    if (-not $Sum.FwVersion -and $Delta.FwVersion) { $Sum.FwVersion = $Delta.FwVersion }
}

# Résoudre la liste des fichiers
$files = @()
if ($LogFolder) {
    $folderPath = $LogFolder
    if (-not [System.IO.Path]::IsPathRooted($folderPath)) {
        $folderPath = Join-Path $PSScriptRoot $LogFolder
    }
    if (Test-Path $folderPath) {
        $files = Get-ChildItem -Path $folderPath -Filter "part_*.log" -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { $_.FullName }
    }
}
if ($LogFiles.Count -gt 0) {
    $files = @()
    foreach ($f in $LogFiles) {
        $p = $f
        if (-not [System.IO.Path]::IsPathRooted($p)) { $p = Join-Path $PSScriptRoot $p }
        if (Test-Path $p) { $files += (Resolve-Path $p).Path }
    }
}

if ($files.Count -eq 0) {
    Write-Host "Aucun fichier log trouvé (LogFolder ou LogFiles)." -ForegroundColor Red
    exit 1
}

if (-not $OutputReport) {
    $OutputReport = Join-Path $PSScriptRoot "RAPPORT_agregé_$(Get-Date -Format 'yyyy-MM-dd_HH-mm').md"
}

Write-Host "=== RAPPORT AGRÉGÉ ===" -ForegroundColor Cyan
Write-Host "Fichiers: $($files.Count)" -ForegroundColor Yellow
Write-Host "Sortie: $OutputReport" -ForegroundColor Yellow

$sum = @{
    LineCount = 0
    JsonParseSuccess = 0
    JsonParseErrors = 0
    PostSends = 0
    PostDiagnostic = 0
    PostErrors = 0
    HeapChecks = 0
    LowHeapWarnings = 0
    Dht22Disabled = 0
    Dht22Failures = 0
    QueueFullErrors = 0
    BranchData = 0
    BranchTimeout = 0
    NetRpcTimeouts = 0
    WifiDisconnect = 0
    WifiConnect = 0
    GetOutputsState = 0
    SyncEnvoiPost = 0
    OtaCheckRequested = 0
    BootCount = 0
    SwCpuResetCount = 0
    PowerOnCount = 0
    WdtCount = 0
    GuruMeditation = 0
    Panic = 0
    Brownout = 0
    StackOverflow = 0
    UnwantedReboots = 0
    RebootsAfterFirst = 0
    Warnings = 0
    Errors = 0
    FwVersion = ""
}

$perSlice = @()
foreach ($f in $files) {
    $name = [System.IO.Path]::GetFileName($f)
    Write-Host "  Traitement: $name" -ForegroundColor Gray
    $delta = Get-CountsFromLogFile -Path $f
    Add-Counts -Sum $sum -Delta $delta
    $perSlice += [PSCustomObject]@{
        File = $name
        Lines = $delta.LineCount
        Boots = $delta.BootCount
        Errors = $delta.Errors
        Warnings = $delta.Warnings
        PostSends = $delta.PostSends
        LowHeapWarnings = $delta.LowHeapWarnings
    }
}

$totalBranch = $sum.BranchData + $sum.BranchTimeout
$ratioDataPct = if ($totalBranch -gt 0) { [math]::Round(100.0 * $sum.BranchData / $totalBranch, 1) } else { 0 }
$fwVersion = if ($sum.FwVersion) { $sum.FwVersion } else { "voir logs" }

$criticalIssues = @()
if ($sum.GuruMeditation -gt 0) { $criticalIssues += "Guru Meditation: $($sum.GuruMeditation)" }
if ($sum.Panic -gt 0) { $criticalIssues += "Panic: $($sum.Panic)" }
if ($sum.Brownout -gt 0) { $criticalIssues += "Brownout: $($sum.Brownout)" }
if ($sum.StackOverflow -gt 0) { $criticalIssues += "Stack Overflow: $($sum.StackOverflow)" }
if ($sum.UnwantedReboots -gt 0) { $criticalIssues += "Reboot(s) non voulu(s): $($sum.UnwantedReboots) (boots: $($sum.BootCount), SW_CPU_RESET: $($sum.SwCpuResetCount))" }

$status = "À SURVEILLER"
if ($criticalIssues.Count -gt 0) { $status = "INSTABLE" }
elseif ($sum.PostSends -gt 0 -and $sum.JsonParseErrors -eq 0) { $status = "STABLE" }

$md = @"
# Rapport agrégé – Monitoring ESP32 longue durée

- **Date rapport:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- **Fichiers agrégés:** $($files.Count)
- **Version firmware:** $fwVersion
- **Total lignes:** $($sum.LineCount)

## Totaux sur la période

| Métrique | Valeur |
|----------|--------|
| Boots (rst:0x) | $($sum.BootCount) |
| Reboots après 1er démarrage | $($sum.RebootsAfterFirst) |
| SW_CPU_RESET | $($sum.SwCpuResetCount) |
| Envois POST | $($sum.PostSends) |
| Erreurs POST | $($sum.PostErrors) |
| Erreurs parsing JSON | $($sum.JsonParseErrors) |
| Alertes heap faible | $($sum.LowHeapWarnings) |
| Timeouts netRPC | $($sum.NetRpcTimeouts) |
| WiFi déconnexions | $($sum.WifiDisconnect) |
| WiFi reconnexions | $($sum.WifiConnect) |
| Branche data / timeout | $($sum.BranchData) / $($sum.BranchTimeout) (ratio data: $ratioDataPct %) |
| Erreurs | $($sum.Errors) |
| Warnings | $($sum.Warnings) |
| Queue pleine | $($sum.QueueFullErrors) |
| DHT22 échecs / désactivations | $($sum.Dht22Failures) / $($sum.Dht22Disabled) |

## Erreurs critiques

$(if ($criticalIssues.Count -eq 0) { "Aucune." } else { ($criticalIssues | ForEach-Object { "- $_" }) -join "`n" })

## Détail par tranche

| Fichier | Lignes | Boots | Erreurs | Warnings | POST | Heap faible |
|---------|--------|-------|---------|----------|------|-------------|
$(($perSlice | ForEach-Object { "| $($_.File) | $($_.Lines) | $($_.Boots) | $($_.Errors) | $($_.Warnings) | $($_.PostSends) | $($_.LowHeapWarnings) |" }) -join "`n")

## Résumé

- **Statut:** $status
- **Dossier des logs:** $(if ($LogFolder) { $LogFolder } else { "voir chemins fournis" })

"@

$md | Out-File -FilePath $OutputReport -Encoding UTF8
Write-Host "Rapport écrit: $OutputReport" -ForegroundColor Green
Write-Host "Statut: $status" -ForegroundColor $(if ($status -eq "STABLE") { "Green" } elseif ($status -eq "INSTABLE") { "Red" } else { "Yellow" })
