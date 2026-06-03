# Verifie jeu cohérent bootloader + partitions + firmware (build dir et .pio_artifacts).
# Usage: .\tools\verify_flash_bundle.ps1 -Environment wroom-beta

param(
    [Parameter(Mandatory = $true)]
    [string]$Environment
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
if (Test-Path -LiteralPath $helpers) { . $helpers }

$buildDir = if (Get-Command Get-N3PioEnvBuildDir -ErrorAction SilentlyContinue) {
    Get-N3PioEnvBuildDir -ProjectRoot $ProjectRoot -Environment $Environment
} else {
    Join-Path $ProjectRoot (Join-Path ".pio" (Join-Path "build" $Environment))
}
$artifactsDir = Join-Path $ProjectRoot ".pio_artifacts\$Environment"

$required = @("firmware.bin", "bootloader.bin", "partitions.bin")
$errors = @()

foreach ($name in $required) {
    $buildPath = Join-Path $buildDir $name
    if (-not (Test-Path -LiteralPath $buildPath)) {
        $found = Get-ChildItem -Path $buildDir -Recurse -Filter $name -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $buildPath = $found.FullName }
    }
    if (-not (Test-Path -LiteralPath $buildPath)) {
        $errors += "Manquant dans build: $name ($buildDir)"
        continue
    }
    $buildItem = Get-Item -LiteralPath $buildPath
    $artPath = Join-Path $artifactsDir $name
    if (Test-Path -LiteralPath $artPath) {
        $artItem = Get-Item -LiteralPath $artPath
        if ($artItem.Length -ne $buildItem.Length) {
            $errors += "$name : taille build=$($buildItem.Length) vs artifacts=$($artItem.Length)"
        }
    }
}

$firmwarePath = Join-Path $buildDir "firmware.bin"
if (Test-Path -LiteralPath $firmwarePath) {
    if ((Get-Item -LiteralPath $firmwarePath).Length -lt 1200000) {
        $errors += "firmware.bin trop petit pour flash production"
    }
}

if ($errors.Count -gt 0) {
    foreach ($e in $errors) { Write-Host "ERREUR: $e" -ForegroundColor Red }
    exit 1
}

Write-Host "OK : bundle flash $Environment coherent." -ForegroundColor Green
exit 0
