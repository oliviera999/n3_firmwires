param(
    [string]$Environment = "msp1",
    [string]$Port = "",
    [int]$DurationSeconds = 300,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $projectRoot "logs"
if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logFile = Join-Path $logDir "monitor_${Environment}_${stamp}.log"

Push-Location $projectRoot
try {
    $portArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        $portArgs = @("--upload-port", $Port)
    }

    if (-not $SkipBuild) {
        Write-Host "[CAM] Build env=$Environment" -ForegroundColor Cyan
        & pio run -e $Environment
    }

    Write-Host "[CAM] Erase flash env=$Environment" -ForegroundColor Cyan
    & pio run -e $Environment -t erase @portArgs

    Write-Host "[CAM] Upload firmware env=$Environment" -ForegroundColor Cyan
    & pio run -e $Environment -t upload @portArgs

    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw "python introuvable. Installer Python pour monitor_serial_cam.py."
    }

    $monitorPort = if ([string]::IsNullOrWhiteSpace($Port)) { "COM5" } else { $Port }
    Write-Host "[CAM] Monitor ${DurationSeconds}s sur $monitorPort -> $logFile" -ForegroundColor Cyan
    & python ".\tools\monitor_serial_cam.py" $monitorPort -s $DurationSeconds | Tee-Object -FilePath $logFile
}
finally {
    Pop-Location
}
