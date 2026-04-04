<#
.SYNOPSIS
  Valide rapidement l'env wroom-beta-local sur carte (flash + monitor serie).

.DESCRIPTION
  - Build et upload optionnels de l'env wroom-beta-local.
  - Capture des logs serie pendant une fenetre definie.
  - Verifie la presence des endpoints test attendus et d'une issue HTTP observable.

.EXAMPLE
  .\scripts\test_wroom_beta_local_serial.ps1 -Port COM7 -MonitorSeconds 150
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$MonitorSeconds = 120,
    [switch]$SkipUpload,
    [switch]$RequireHeartbeat,
    [string]$Environment = "wroom-beta-local",
    [string]$LogPath
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$captureScript = Join-Path $projectDir "tools/monitor/serial_capture.py"
$logsDir = Join-Path $projectDir "logs"

if (-not (Test-Path -LiteralPath $captureScript)) {
    throw "Script introuvable: $captureScript"
}

if (-not (Test-Path -LiteralPath $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir | Out-Null
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $LogPath = Join-Path $logsDir ("beta_local_serial_{0}.log" -f $timestamp)
}

Push-Location $projectDir
try {
    if (-not $SkipUpload) {
        Write-Host "[beta-local] Build + upload ($Environment) sur $Port..." -ForegroundColor Cyan
        pio run -e $Environment -t upload --upload-port $Port
        if ($LASTEXITCODE -ne 0) {
            throw "Upload echoue (env=$Environment, port=$Port)."
        }
    } else {
        Write-Host "[beta-local] Upload saute (--SkipUpload)." -ForegroundColor Yellow
    }

    Write-Host "[beta-local] Capture serie ${MonitorSeconds}s -> $LogPath" -ForegroundColor Cyan
    python $captureScript --port $Port --baud 115200 --duration $MonitorSeconds --out $LogPath --quiet
    if ($LASTEXITCODE -ne 0) {
        throw "Capture serie echouee."
    }
} finally {
    Pop-Location
}

$content = Get-Content -LiteralPath $LogPath -ErrorAction Stop
$foundPostData = @($content | Select-String -Pattern "post-data-test").Count -gt 0
$foundHeartbeat = @($content | Select-String -Pattern "heartbeat-test").Count -gt 0
$foundHttp200 = @($content | Select-String -Pattern "(?i)(http.*code.?=200|code.?=200|http 200)").Count -gt 0
$foundHttpIssue = @($content | Select-String -Pattern "(?i)(code.?=401|code.?=403|timeout|failed|echec|erreur)").Count -gt 0

Write-Host ""
Write-Host "[beta-local] Resultats assertions:" -ForegroundColor Cyan
Write-Host ("  endpoint post-data-test : {0}" -f ($(if ($foundPostData) { "OK" } else { "KO" })))
Write-Host ("  endpoint heartbeat-test : {0}" -f ($(if ($foundHeartbeat) { "OK" } else { "KO" })))
Write-Host ("  heartbeat obligatoire : {0}" -f ($(if ($RequireHeartbeat) { "OUI" } else { "NON" })))
Write-Host ("  statut HTTP observable : {0}" -f ($(if ($foundHttp200 -or $foundHttpIssue) { "OK" } else { "KO" })))

if (-not $foundPostData -or ($RequireHeartbeat -and -not $foundHeartbeat) -or (-not $foundHttp200 -and -not $foundHttpIssue)) {
    throw "Validation beta-local incomplete. Consulter le log: $LogPath"
}

Write-Host ""
Write-Host "[beta-local] Validation serie OK." -ForegroundColor Green
Write-Host "[beta-local] Log: $LogPath" -ForegroundColor Gray
