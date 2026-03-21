# Décode un backtrace ESP32 (Guru Meditation) avec addr2line.
# Usage:
#   .\decode_backtrace.ps1                    # dernier log monitor_5min_*.log (ELF wroom-test par défaut)
#   .\decode_backtrace.ps1 -LogFile xxx.log   # fichier spécifique
#   .\decode_backtrace.ps1 -LogFile xxx.log -Elf .pio\build\wroom-beta\firmware.elf   # log + ELF d'un autre env
#   .\decode_backtrace.ps1 -Addresses "0x40093cab 0x40128cad" -Elf .pio\build\wroom-test\firmware.elf

param(
    [string]$LogFile = "",
    [string]$Addresses = "",
    [string]$Elf = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$helpers = Join-Path $projectRoot "..\scripts\Get-PioBuildHelpers.ps1"
if (Test-Path -LiteralPath $helpers) {
    . $helpers
    $elfDefault = Get-N3PioFirmwareElf -ProjectRoot $projectRoot -Environment "wroom-test"
} else {
    $elfDefault = Join-Path $projectRoot ".pio\build\wroom-test\firmware.elf"
}
$addr2line = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32-elf-addr2line.exe"

if (-not (Test-Path $addr2line)) {
    Write-Host "Erreur: addr2line introuvable. Build au moins une fois avec 'pio run -e wroom-test'." -ForegroundColor Red
    exit 1
}

if ($Addresses -and $Elf) {
    $addrs = $Addresses -split "\s+"
    & $addr2line -pfia -e $Elf @addrs
    exit 0
}

if ($LogFile -and -not (Test-Path $LogFile)) {
    Write-Host "Fichier log introuvable: $LogFile" -ForegroundColor Red
    exit 1
}

if (-not $LogFile) {
    $logsDir = Join-Path $projectRoot "logs"
    $latest = $null
    if (Test-Path $logsDir) {
        $latest = Get-ChildItem -Path $logsDir -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue |
            Sort-Object CreationTime -Descending | Select-Object -First 1
    }
    if (-not $latest) {
        $latest = Get-ChildItem -Path $projectRoot -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue |
            Sort-Object CreationTime -Descending | Select-Object -First 1
    }
    if (-not $latest) {
        Write-Host "Aucun log monitor_5min_*.log trouvé (logs/ ou racine)." -ForegroundColor Red
        exit 1
    }
    $LogFile = $latest.FullName
    Write-Host "Log: $LogFile" -ForegroundColor Cyan
}

$lines = Get-Content -Path $LogFile
$backtraceLine = $null
foreach ($l in $lines) {
    if ($l -match "0x[0-9a-fA-F]+:0x[0-9a-fA-F]+.*0x[0-9a-fA-F]+:0x[0-9a-fA-F]+" -and $l -match "Backt") {
        $backtraceLine = $l
        break
    }
}
if (-not $backtraceLine) {
    Write-Host "Aucune ligne Backtrace trouvée dans le log." -ForegroundColor Yellow
    exit 0
}
# Extraire les PC (avant ':') de la ligne
$addrs = [regex]::Matches($backtraceLine, "0x([0-9a-fA-F]+):") | ForEach-Object { "0x" + $_.Groups[1].Value }
if ($addrs.Count -eq 0) {
    Write-Host "Aucune adresse extraite." -ForegroundColor Yellow
    exit 0
}

$elfPath = if ($Elf -and (Test-Path $Elf)) { (Resolve-Path $Elf).Path } else { $elfDefault }
if (-not (Test-Path $elfPath)) {
    Write-Host "ELF introuvable: $elfPath (lancez 'pio run -e wroom-test' ou précisez -Elf si besoin)." -ForegroundColor Yellow
    exit 1
}
Write-Host "ELF: $elfPath" -ForegroundColor Gray

Write-Host "Adresses: $($addrs -join ' ')" -ForegroundColor Gray
Write-Host ""
& $addr2line -pfia -e $elfPath @addrs
