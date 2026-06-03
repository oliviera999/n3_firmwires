# Build PlatformIO rapide (sans clean) pour alternance n3pp / ffp5cs / msp.
# Usage (depuis racine IOT_n3 ou firmwires/) :
#   .\firmwires\scripts\Invoke-PioBuildFast.ps1 -Project ffp5cs -Environment wroom-prod
#   .\firmwires\scripts\Invoke-PioBuildFast.ps1 -Project n3pp -Environment esp32dev -Verify

param(
    [Parameter(Mandatory = $true)]
    [string]$Project,
    [Parameter(Mandatory = $true)]
    [string]$Environment,
    [switch]$Verify,
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$firmwiresRoot = Split-Path -Parent $scriptDir
$projDir = Join-Path $firmwiresRoot $Project
if (-not (Test-Path -LiteralPath (Join-Path $projDir "platformio.ini"))) {
    Write-Host "Erreur: $projDir (platformio.ini introuvable)" -ForegroundColor Red
    exit 1
}

$helpers = Join-Path $scriptDir "Get-PioBuildHelpers.ps1"
if (Test-Path -LiteralPath $helpers) { . $helpers }

$pio = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pio) { $pio = Get-Command platformio -ErrorAction SilentlyContinue }
if (-not $pio) { throw "pio / platformio introuvable dans PATH" }

if (Get-Command Repair-N3PioBuildJunction -ErrorAction SilentlyContinue) {
    $null = Repair-N3PioBuildJunction -ProjectRoot $projDir -Environment $Environment
}

Push-Location $projDir
try {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $args = @("run", "-e", $Environment)
    if ($Jobs -gt 0) {
        $args += "-j", "$Jobs"
    } elseif ($env:OS -eq "Windows_NT") {
        $args += "-j", "1"
    }
    Write-Host "=== Build rapide $Project / $Environment (sans clean) ===" -ForegroundColor Cyan
    & $pio.Name @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if ($Verify -and $Project -eq "ffp5cs" -and $Environment -match '^wroom-' -and $Environment -ne 'wroom-tls-test') {
        $verifyScript = Join-Path $projDir "tools\verify_wroom_sdkconfig.ps1"
        if (Test-Path -LiteralPath $verifyScript) {
            & $verifyScript -Environment $Environment
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
    }

    $sw.Stop()
    if (Get-Command Get-N3PioFirmwareBin -ErrorAction SilentlyContinue) {
        $bin = Get-N3PioFirmwareBin -ProjectRoot $projDir -Environment $Environment
        if (Test-Path -LiteralPath $bin) {
            $len = (Get-Item -LiteralPath $bin).Length
            Write-Host "firmware.bin: $bin ($len octets)" -ForegroundColor Green
        }
    }
    Write-Host "Duree: $([math]::Round($sw.Elapsed.TotalMinutes, 1)) min" -ForegroundColor Green
} finally {
    Pop-Location
}
