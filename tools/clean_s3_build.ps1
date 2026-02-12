# Force-supprime .pio\build\wroom-s3-test et .pio\libdeps\wroom-s3-test avec retries.
# Utile quand fullclean ou Library Manager échoue (WinError 32: fichier utilisé par un autre processus).
#
# Si WinError 32 sur framework-arduinoespressif32: .\tools\clean_s3_build.ps1 -IncludeFramework
# (fermer Cursor, moniteur série, et tout terminal qui utilise le projet avant)
#
# Usage: .\tools\clean_s3_build.ps1
#        .\tools\clean_s3_build.ps1 -IncludeFramework   # + suppression package framework (WinError 32)

param([switch]$IncludeFramework = $false)

$projectRoot = if ($PSScriptRoot) {
    $d = Split-Path $PSScriptRoot -Parent
    if (Test-Path (Join-Path $d "platformio.ini")) { $d } else { Get-Location }
} else { Get-Location }

$dirs = @(
    (Join-Path $projectRoot ".pio\build\wroom-s3-test"),
    (Join-Path $projectRoot ".pio\libdeps\wroom-s3-test")
)
if ($IncludeFramework) {
    $pioPkg = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32"
    $dirs = @($pioPkg) + $dirs
    Write-Host "clean_s3_build: mode -IncludeFramework (suppression package framework)" -ForegroundColor Cyan
}
$maxRetries = 3
$delaySeconds = 2

function Remove-DirForce($path) {
    if (-not (Test-Path $path)) { return $true }
    try {
        Remove-Item -Path $path -Recurse -Force -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Remove-DirCmd($path) {
    if (-not (Test-Path $path)) { return $true }
    $q = $path -replace '/', '\'
    $ret = cmd /c "rd /s /q `"$q`""
    return (-not (Test-Path $path))
}

foreach ($dir in $dirs) {
    if (-not (Test-Path $dir)) { continue }
    $removed = $false
    for ($r = 1; $r -le $maxRetries; $r++) {
        if (Remove-DirForce $dir) {
            $removed = $true
            Write-Host "clean_s3_build: supprimé $dir" -ForegroundColor Green
            break
        }
        if ($r -lt $maxRetries) {
            Write-Host "clean_s3_build: tentative $r/$maxRetries échouée, attente ${delaySeconds}s..." -ForegroundColor Yellow
            Start-Sleep -Seconds $delaySeconds
        }
    }
    if (-not $removed -and (Test-Path $dir)) {
        Write-Host "clean_s3_build: fallback cmd rd /s /q..." -ForegroundColor Gray
        if (Remove-DirCmd $dir) {
            Write-Host "clean_s3_build: supprimé (cmd) $dir" -ForegroundColor Green
        } else {
            Write-Host "clean_s3_build: impossible de supprimer $dir" -ForegroundColor Red
            Write-Host "  Fermez Cursor, ouvrez PowerShell externe, relancez ce script puis pio run." -ForegroundColor Yellow
        }
    }
}
