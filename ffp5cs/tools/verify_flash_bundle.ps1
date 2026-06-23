# Verifie jeu coherent bootloader + partitions + firmware (build dir).
# Bloque le flash si firmware.bin recent avec bootloader/partitions obsoletes (panic Cache error).
# Usage: .\tools\verify_flash_bundle.ps1 -Environment wroom-prod

param(
    [Parameter(Mandatory = $true)]
    [string]$Environment
)

$ErrorActionPreference = "Stop"
$MinFirmwareBytes = 1200000
$MaxFirmwareLeadSec = 3600
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
if (Test-Path -LiteralPath $helpers) { . $helpers }

$buildDir = if (Get-Command Get-N3PioEnvBuildDir -ErrorAction SilentlyContinue) {
    Get-N3PioEnvBuildDir -ProjectRoot $ProjectRoot -Environment $Environment
} else {
    Join-Path $ProjectRoot (Join-Path ".pio" (Join-Path "build" $Environment))
}
$artifactsDir = Join-Path $ProjectRoot ".pio_artifacts\$Environment"

$errors = @()

$firmwarePath = Join-Path $buildDir "firmware.bin"
if (-not (Test-Path -LiteralPath $firmwarePath)) {
    $errors += "firmware.bin introuvable : $firmwarePath"
} else {
    $fw = Get-Item -LiteralPath $firmwarePath
    if ($fw.Length -lt $MinFirmwareBytes) {
        $errors += "firmware.bin trop petit ($($fw.Length) < $MinFirmwareBytes) : build incomplet"
    }
}

function Get-BundleBinPath {
    param([string]$Name)
    $root = Join-Path $buildDir $Name
    if (Test-Path -LiteralPath $root) { return $root }
    $found = Get-ChildItem -Path $buildDir -Recurse -Filter $Name -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\CMakeFiles\\' } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    $art = Join-Path $artifactsDir $Name
    if (Test-Path -LiteralPath $art) { return $art }
    return $null
}

if ($errors.Count -eq 0) {
    $fw = Get-Item -LiteralPath $firmwarePath
    foreach ($name in @("bootloader.bin", "partitions.bin")) {
        $path = Get-BundleBinPath -Name $name
        if (-not $path) {
            $errors += "Manquant : $name (build $buildDir)"
            continue
        }
        $item = Get-Item -LiteralPath $path
        $rootPath = Join-Path $buildDir $name
        if ($path -ne $rootPath) {
            $errors += "$name absent a la racine du build (trouve : $path) - sync requise"
        }
        $ageSec = ($fw.LastWriteTime - $item.LastWriteTime).TotalSeconds
        if ($ageSec -gt $MaxFirmwareLeadSec) {
            $errors += (
                "$name obsolete : firmware $($fw.LastWriteTime) vs $name $($item.LastWriteTime) " +
                "(ecart ${ageSec}s > ${MaxFirmwareLeadSec}s) - risque panic Cache error au boot"
            )
        }
        $artPath = Join-Path $artifactsDir $name
        if ((Test-Path -LiteralPath $artPath) -and $path -eq $rootPath) {
            $artLen = (Get-Item -LiteralPath $artPath).Length
            if ($artLen -ne $item.Length) {
                $errors += "$name : taille build=$($item.Length) vs artifacts=$artLen"
            }
        }
    }
}

if ($errors.Count -gt 0) {
    foreach ($e in $errors) { Write-Host "ERREUR: $e" -ForegroundColor Red }
    Write-Host "Recovery : pio run -e $Environment -t clean puis pio run -e $Environment" -ForegroundColor Yellow
    exit 1
}

Write-Host "OK : bundle flash $Environment coherent (bootloader/partitions alignes sur firmware.bin)." -ForegroundColor Green
exit 0
