# Garantit un jeu flash WROOM coherent avant erase/upload (recovery auto si obsolete).
# Usage: . .\tools\Ensure-WroomFlashBundle.ps1 ; Ensure-WroomFlashBundle -Environment wroom-prod -PioCli pio

function Remove-StaleWroomBundleFiles {
    param(
        [string]$ProjectRoot,
        [string]$Environment
    )
    $helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
    if (Test-Path -LiteralPath $helpers) { . $helpers }

    $dirs = @()
    if (Get-Command Get-N3PioEnvBuildDir -ErrorAction SilentlyContinue) {
        $dirs += Get-N3PioEnvBuildDir -ProjectRoot $ProjectRoot -Environment $Environment
    }
    $dirs += Join-Path $ProjectRoot ".pio_artifacts\$Environment"

    foreach ($dir in ($dirs | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        foreach ($name in @("bootloader.bin", "partitions.bin", "ota_data_initial.bin", "flash_bundle_manifest.json")) {
            $p = Join-Path $dir $name
            if (Test-Path -LiteralPath $p) {
                Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
                Write-Host "  Supprime: $p" -ForegroundColor Gray
            }
        }
    }
}

function Ensure-WroomFlashBundle {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Environment,
        [string]$ProjectRoot = "",
        [string]$PioCli = "pio",
        [switch]$NoRecover
    )

    if (-not $ProjectRoot) {
        $ProjectRoot = Split-Path -Parent $PSScriptRoot
    }

    $bundleScript = Join-Path $ProjectRoot "tools\verify_flash_bundle.ps1"
    if (-not (Test-Path -LiteralPath $bundleScript)) {
        Write-Host "verify_flash_bundle.ps1 introuvable" -ForegroundColor Red
        exit 1
    }

    & $bundleScript -Environment $Environment
    if ($LASTEXITCODE -eq 0) { return }

    if ($NoRecover) { exit $LASTEXITCODE }

    Write-Host ""
    Write-Host "Bundle flash incoherent - purge bootloader/partitions + clean + rebuild $Environment..." -ForegroundColor Yellow
    Remove-StaleWroomBundleFiles -ProjectRoot $ProjectRoot -Environment $Environment

    $jobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
    & $PioCli run -e $Environment -t clean
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
    if (Test-Path -LiteralPath $helpers) {
        . $helpers
        if (Get-Command Repair-N3PioBuildJunction -ErrorAction SilentlyContinue) {
            $null = Repair-N3PioBuildJunction -ProjectRoot $ProjectRoot -Environment $Environment
        }
    }

    & $PioCli run -e $Environment @jobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $bundleScript -Environment $Environment
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
