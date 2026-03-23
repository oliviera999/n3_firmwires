param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [int]$PostRebootSeconds = 60,
    [int]$MaxWaitSeconds = 3600
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$pythonScript = Join-Path $projectRoot "tools\monitor\monitor_until_crash.py"

if (-not (Test-Path -LiteralPath $pythonScript)) {
    Write-Host "Erreur: script introuvable $pythonScript" -ForegroundColor Red
    exit 1
}

. (Join-Path $projectRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $Port -Baud $Baud

try {
    python --version 2>&1 | Out-Null
    python -c "import serial" 2>&1 | Out-Null
} catch {
    Write-Host "Erreur: Python + pyserial requis (pip install pyserial)." -ForegroundColor Red
    exit 1
}

Write-Host "=== Monitoring jusqu'au crash/reboot ===" -ForegroundColor Cyan
Write-Host "Port: $Port | Baud: $Baud | Post-reboot: ${PostRebootSeconds}s | Max wait: ${MaxWaitSeconds}s" -ForegroundColor Gray
Write-Host ""

Push-Location $projectRoot
try {
    python $pythonScript --port $Port --baud $Baud --post-reboot $PostRebootSeconds --max-wait $MaxWaitSeconds
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
