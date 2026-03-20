<#
.SYNOPSIS
  Compile les 3 environnements uploadphotosserver en sequence.

.DESCRIPTION
  Sequence recommandee sous Windows:
  1) Build de warmup sur un firmware WROOM stable (n3pp/esp32dev)
  2) Build msp1, n3pp, ffp3
  Cette sequence evite les erreurs intermittentes de cache pioarduino.

.PARAMETER WarmupProject
  Firmware de warmup a compiler d'abord (par defaut: n3pp).

.PARAMETER WarmupEnv
  Environnement de warmup (par defaut: esp32dev).

.PARAMETER SkipWarmup
  Ignore l'etape de warmup.

.PARAMETER StopOnError
  Arrete au premier echec.
#>

param(
    [string]$WarmupProject = "n3pp",
    [string]$WarmupEnv = "esp32dev",
    [switch]$SkipWarmup,
    [switch]$StopOnError
)

$ErrorActionPreference = "Continue"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$firmwiresRoot = Split-Path -Parent $projectDir

Push-Location $projectDir
try {
    $results = [ordered]@{}

    if (-not $SkipWarmup) {
        $warmupPath = Join-Path $firmwiresRoot $WarmupProject
        Write-Host "[build] Warmup pioarduino : $WarmupProject/$WarmupEnv" -ForegroundColor Cyan
        Push-Location $warmupPath
        & pio run -e $WarmupEnv
        $warmupCode = $LASTEXITCODE
        Pop-Location
        if ($warmupCode -ne 0) {
            Write-Host "[build] Warmup en echec (code $warmupCode)." -ForegroundColor Red
            if ($StopOnError) { exit $warmupCode }
        } else {
            Write-Host "[build] Warmup OK." -ForegroundColor Green
        }
        Write-Host ""
    }

    $envs = @("msp1", "n3pp", "ffp3")
    foreach ($envName in $envs) {
        Write-Host "[build] Compilation uploadphotosserver/$envName ..." -ForegroundColor Cyan
        $start = Get-Date
        & pio run -e $envName
        $code = $LASTEXITCODE
        $elapsed = [math]::Round(((Get-Date) - $start).TotalSeconds, 1)
        $status = if ($code -eq 0) { "OK" } else { "ECHEC" }
        $results[$envName] = @{ Status = $status; Duration = $elapsed; ExitCode = $code }
        $color = if ($code -eq 0) { "Green" } else { "Red" }
        Write-Host "[build] $envName : $status (${elapsed}s)" -ForegroundColor $color
        Write-Host ""
        if ($code -ne 0 -and $StopOnError) {
            break
        }
    }

    $failed = @($results.Values | Where-Object { $_.Status -eq "ECHEC" }).Count
    Write-Host "=== RESULTATS uploadphotosserver ===" -ForegroundColor Cyan
    $results.GetEnumerator() | ForEach-Object {
        $color = if ($_.Value.Status -eq "OK") { "Green" } else { "Red" }
        Write-Host " - $($_.Key): $($_.Value.Status) ($($_.Value.Duration)s)" -ForegroundColor $color
    }

    if ($failed -gt 0) { exit 1 }
    exit 0
}
finally {
    Pop-Location
}
