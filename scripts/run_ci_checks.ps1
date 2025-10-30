<#
.SYNOPSIS
  Compile the firmware with PlatformIO and analyse the 90 second serial log for critical issues.

.PARAMETER LogPath
  Path to the serial log captured during the mandatory 90s monitoring window.

.PARAMETER SkipBuild
  Skip the PlatformIO build step (useful when the artefacts are already up to date).

.EXAMPLE
  pwsh ./scripts/run_ci_checks.ps1 -LogPath .\pythonserial\esp32_logs.txt

.NOTES
  The script searches for Guru Meditation, panic, watchdog resets, and memory anomalies.
#>

param(
    [string]$LogPath = "pythonserial/esp32_logs.txt",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Invoke-PlatformBuild {
    Write-Host "[CI] ▶️ Running PlatformIO build" -ForegroundColor Cyan
    & platformio run
    if ($LASTEXITCODE -ne 0) {
        Write-Error "PlatformIO build failed. Check compiler output."
    }
    Write-Host "[CI] ✅ Build completed" -ForegroundColor Green
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

