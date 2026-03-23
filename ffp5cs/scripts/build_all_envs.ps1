<#
.SYNOPSIS
  Compile les 4 environnements critiques FFP5CS et produit un rapport de verification.

.DESCRIPTION
  Build sequentiel de wroom-prod, wroom-test, wroom-s3-test et wroom-s3-prod.
  Un nettoyage automatique est effectue lors du basculement WROOM <-> S3
  (plateformes differentes : pioarduino vs platformio/espressif32).
  En fin d'execution, un resume indique le statut de chaque env.

.PARAMETER Envs
  Liste des environnements a compiler. Par defaut les 4 critiques.

.PARAMETER Clean
  Force un nettoyage complet de tous les envs avant de compiler.

.PARAMETER StopOnError
  Arrete l'execution au premier echec au lieu de continuer.

.PARAMETER Verbose
  Affiche la sortie complete de PlatformIO (sinon seule la derniere ligne est affichee).

.EXAMPLE
  .\scripts\build_all_envs.ps1
  .\scripts\build_all_envs.ps1 -StopOnError
  .\scripts\build_all_envs.ps1 -Envs wroom-test,wroom-s3-test
  .\scripts\build_all_envs.ps1 -Clean
#>

param(
    [string[]]$Envs = @("wroom-prod", "wroom-test", "wroom-s3-test", "wroom-s3-prod"),
    [switch]$Clean,
    [switch]$StopOnError,
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
Push-Location $projectDir

try {
    function Get-PioCliCommand {
        $pioCmd = Get-Command "pio" -ErrorAction SilentlyContinue
        if ($pioCmd) { return "pio" }
        $platformioCmd = Get-Command "platformio" -ErrorAction SilentlyContinue
        if ($platformioCmd) { return "platformio" }
        throw "Ni 'pio' ni 'platformio' ne sont disponibles dans le PATH."
    }

    function Get-DeclaredPioEnvs {
        param([string]$IniPath)
        if (-not (Test-Path -LiteralPath $IniPath)) { return @() }
        $lines = Get-Content -LiteralPath $IniPath -ErrorAction SilentlyContinue
        $envs = @()
        foreach ($line in $lines) {
            if ($line -match '^\[env:([^\]]+)\]') {
                $envs += $Matches[1]
            }
        }
        return $envs
    }

    if (-not (Test-Path "platformio.ini")) {
        Write-Error "platformio.ini introuvable. Executez ce script depuis ffp5cs/ ou via scripts/."
        exit 1
    }

    $pioCli = Get-PioCliCommand
    $declaredEnvs = Get-DeclaredPioEnvs -IniPath (Join-Path $projectDir "platformio.ini")
    $invalidEnvs = @($Envs | Where-Object { $_ -notin $declaredEnvs })
    if ($invalidEnvs.Count -gt 0) {
        Write-Error ("Environnement(s) inconnu(s) dans platformio.ini: {0}" -f ($invalidEnvs -join ", "))
        exit 1
    }

    $helpers = Join-Path $projectDir "..\scripts\Get-PioBuildHelpers.ps1"
    if (Test-Path -LiteralPath $helpers) {
        . $helpers
        Write-N3PioWorkspaceAdvice -ProjectRoot $projectDir
    }

    $prefix = "build"
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "  FFP5CS - Build multi-environnements" -ForegroundColor Cyan
    Write-Host ("  Envs : " + ($Envs -join ', ')) -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host ""

    $s3EnvList = @("wroom-s3-test", "wroom-s3-prod", "wroom-s3-test-psram", "wroom-s3-test-psram-v2", "wroom-s3-test-devkit", "wroom-s3-test-usb")

    function Get-EnvFamily([string]$name) {
        if ($s3EnvList -contains $name) { return "s3" }
        return "wroom"
    }

    $results = [ordered]@{}
    $totalStart = Get-Date
    $previousFamily = $null

    if ($Clean) {
        Write-Host "[$prefix] Nettoyage complet (tous les envs)..." -ForegroundColor Yellow
        foreach ($envName in $Envs) {
            & $pioCli run -e $envName -t clean 2>&1 | Out-Null
        }
        Write-Host "[$prefix] Nettoyage termine." -ForegroundColor Green
        Write-Host ""
    }

    foreach ($envName in $Envs) {
        $family = Get-EnvFamily $envName
        $needsClean = ($null -ne $previousFamily) -and ($family -ne $previousFamily)

        if ($needsClean -and -not $Clean) {
            Write-Host "[$prefix] Basculement $previousFamily -> $family : nettoyage de $envName..." -ForegroundColor Yellow
            & $pioCli run -e $envName -t clean 2>&1 | Out-Null
        }

        Write-Host "[$prefix] Compilation : $envName ($family)" -ForegroundColor Cyan
        $envStart = Get-Date

        if ($Verbose) {
            & $pioCli run -e $envName 2>&1 | ForEach-Object { Write-Host $_ }
        } else {
            $output = & $pioCli run -e $envName 2>&1
        }
        $exitCode = $LASTEXITCODE
        $elapsed = [math]::Round(((Get-Date) - $envStart).TotalSeconds, 1)

        if ($exitCode -eq 0) {
            Write-Host "[$prefix] $envName : OK (${elapsed}s)" -ForegroundColor Green

            $binPath = if (Get-Command Get-N3PioFirmwareBin -ErrorAction SilentlyContinue) {
                Get-N3PioFirmwareBin -ProjectRoot $projectDir -Environment $envName
            } else {
                Join-Path (Join-Path (Join-Path ".pio" "build") $envName) "firmware.bin"
            }
            if (Test-Path $binPath) {
                $sizeKB = [math]::Round((Get-Item $binPath).Length / 1KB, 1)
                Write-Host "        firmware.bin : $sizeKB KB" -ForegroundColor Gray
            }

            $results[$envName] = @{ Status = "OK"; Duration = $elapsed; ExitCode = 0 }
        } else {
            Write-Host "[$prefix] $envName : ECHEC (code $exitCode, ${elapsed}s)" -ForegroundColor Red
            if (-not $Verbose -and $output) {
                $errorLines = $output | Where-Object { $_ -match '(?i)(error|fatal|failed)' } | Select-Object -Last 10
                if ($errorLines) {
                    $errorLines | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
                }
            }
            $results[$envName] = @{ Status = "ECHEC"; Duration = $elapsed; ExitCode = $exitCode }

            if ($StopOnError) {
                Write-Host ""
                Write-Host "[$prefix] Arret sur erreur (-StopOnError)." -ForegroundColor Red
                break
            }
        }

        $previousFamily = $family
        Write-Host ""
    }

    $totalDuration = [math]::Round(((Get-Date) - $totalStart).TotalSeconds, 1)
    $passed = @($results.Values | Where-Object { $_.Status -eq "OK" }).Count
    $failed = @($results.Values | Where-Object { $_.Status -eq "ECHEC" }).Count

    $statusColor = if ($failed -gt 0) { "Red" } else { "Green" }
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "  Resultats : $passed/$($results.Count) OK, $failed echec(s)" -ForegroundColor $statusColor
    Write-Host "  Duree totale : ${totalDuration}s" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    $results.GetEnumerator() | ForEach-Object {
        $color = if ($_.Value.Status -eq "OK") { "Green" } else { "Red" }
        $icon  = if ($_.Value.Status -eq "OK") { "OK " } else { "KO " }
        $dur = $_.Value.Duration
        Write-Host "  [$icon] $($_.Key) (${dur}s)" -ForegroundColor $color
    }

    Write-Host ""

    if ($failed -gt 0) {
        exit 1
    }
    exit 0

} finally {
    Pop-Location
}
