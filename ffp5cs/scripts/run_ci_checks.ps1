<#
.SYNOPSIS
  Compile le firmware avec PlatformIO et analyse le log serie pour les problemes critiques.

.PARAMETER LogPath
  Chemin du log serie capture pendant la fenetre de monitoring 90s.

.PARAMETER SkipBuild
  Saute l'etape de build PlatformIO (utile quand les artefacts sont a jour).

.PARAMETER AllEnvs
  Compile les 4 environnements critiques (wroom-prod, wroom-test, wroom-s3-test,
  wroom-s3-prod) au lieu du seul env par defaut. Delegue a build_all_envs.ps1.

.PARAMETER IncludeBetaLocal
  Ajoute wroom-beta-local au build multi-env (utile pour la validation locale).

.EXAMPLE
  pwsh ./scripts/run_ci_checks.ps1 -LogPath .\pythonserial\esp32_logs.txt
  pwsh ./scripts/run_ci_checks.ps1 -AllEnvs

.NOTES
  Recherche Guru Meditation, panic, watchdog resets et anomalies memoire.
#>

param(
    [string]$LogPath = "pythonserial/esp32_logs.txt",
    [switch]$SkipBuild,
    [switch]$AllEnvs,
    [switch]$IncludeBetaLocal
)

$ErrorActionPreference = "Stop"

function Get-PioCliCommand {
    $pioCmd = Get-Command "pio" -ErrorAction SilentlyContinue
    if ($pioCmd) { return "pio" }
    $platformioCmd = Get-Command "platformio" -ErrorAction SilentlyContinue
    if ($platformioCmd) { return "platformio" }
    throw "Ni 'pio' ni 'platformio' ne sont disponibles dans le PATH."
}

function Invoke-PlatformBuild {
    $pioCli = Get-PioCliCommand
    if ($AllEnvs) {
        Write-Host "[CI] Build multi-environnements (4 envs critiques)" -ForegroundColor Cyan
        $buildScript = Join-Path $PSScriptRoot "build_all_envs.ps1"
        if (-not (Test-Path -LiteralPath $buildScript)) {
            Write-Error "Script introuvable: $buildScript"
        }
        if ($IncludeBetaLocal) {
            & $buildScript -StopOnError -IncludeBetaLocal
        } else {
            & $buildScript -StopOnError
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build multi-env echoue. Voir le rapport ci-dessus."
        }
        Write-Host "[CI] Build multi-env OK" -ForegroundColor Green
        return
    }

    Write-Host "[CI] Build PlatformIO (env par defaut)" -ForegroundColor Cyan
    & $pioCli run
    if ($LASTEXITCODE -ne 0) {
        Write-Error "PlatformIO build failed. Check compiler output."
    }
    Write-Host "[CI] Build OK" -ForegroundColor Green
}

function Test-SerialLog {
    param(
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        Write-Warning "Serial log '$Path' not found. Capture the 90s session and re-run."
        return
    }

    Write-Host "[CI] 📄 Analysing serial log: $Path" -ForegroundColor Cyan
    $content = Get-Content -Path $Path

    $critical = $content | Where-Object { $_ -match '(?i)(guru|panic|wdt|brownout)' }
    $warnings = $content | Where-Object { $_ -match '(?i)(heap|fragmentation|timeout)' }

    if ($critical.Count -gt 0) {
        Write-Host "[CI] ❌ Critical findings detected:" -ForegroundColor Red
        $critical | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    } else {
        Write-Host "[CI] ✅ No critical errors found" -ForegroundColor Green
    }

    if ($warnings.Count -gt 0) {
        Write-Host "[CI] ⚠️ Warnings:" -ForegroundColor Yellow
        $warnings | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    } else {
        Write-Host "[CI] ℹ️ No memory/timeouts warnings spotted" -ForegroundColor Gray
    }

    $heapSamples = $content | Where-Object { $_ -match '(?i)heap' }
    if ($heapSamples.Count -gt 0) {
        Write-Host "[CI] 🧮 Heap entries detected: $($heapSamples.Count)" -ForegroundColor Gray
    }
}

if (-not $SkipBuild) {
    Invoke-PlatformBuild
} else {
    Write-Host "[CI] ⏭️ Build step skipped" -ForegroundColor Yellow
}

Test-SerialLog -Path $LogPath

Write-Host "[CI] ✅ Checks completed" -ForegroundColor Green

