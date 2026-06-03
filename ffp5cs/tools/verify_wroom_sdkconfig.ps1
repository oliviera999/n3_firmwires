# Vérifie sdkconfig et firmware.bin d'un build WROOM (anti boot-loop Cache error / build incomplet).
# Usage: .\tools\verify_wroom_sdkconfig.ps1 -Environment wroom-beta
#        .\tools\verify_wroom_sdkconfig.ps1 -Environment wroom-test -WarnOnly

param(
    [string]$Environment = "wroom-test",
    [switch]$WarnOnly = $false
)

$ErrorActionPreference = "Stop"
$MinFirmwareBytes = 1200000
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not (Test-Path (Join-Path $ProjectRoot "platformio.ini"))) {
    $ProjectRoot = (Get-Location).Path
}

$helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
if (-not (Test-Path -LiteralPath $helpers)) {
    $helpers = Join-Path (Split-Path -Parent $ProjectRoot) "scripts\Get-PioBuildHelpers.ps1"
}
if (Test-Path -LiteralPath $helpers) {
    . $helpers
    $buildDir = Get-N3PioEnvBuildDir -ProjectRoot $ProjectRoot -Environment $Environment
    $firmwareBin = Get-N3PioFirmwareBin -ProjectRoot $ProjectRoot -Environment $Environment
} else {
    $buildDir = Join-Path $ProjectRoot (Join-Path ".pio" (Join-Path "build" $Environment))
    $firmwareBin = Join-Path $buildDir "firmware.bin"
}

$errors = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

function Test-SdkconfigContent {
    param([string]$Content, [string]$Label)
    if ($Content -match '(?m)^\s*CONFIG_ESP32_SPIRAM_SUPPORT=y') {
        $errors.Add("$Label : CONFIG_ESP32_SPIRAM_SUPPORT=y (WROOM sans PSRAM)")
    }
    if ($Content -match '(?m)^\s*CONFIG_SPIRAM_SUPPORT=y') {
        $errors.Add("$Label : CONFIG_SPIRAM_SUPPORT=y")
    }
    if ($Content -match '(?m)^\s*CONFIG_SPIRAM_CACHE_WORKAROUND=y') {
        $errors.Add("$Label : CONFIG_SPIRAM_CACHE_WORKAROUND=y")
    }
    if ($Content -match 'CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=(\d+)') {
        if ($Matches[1] -ne "240") {
            $errors.Add("$Label : CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=$($Matches[1]) (attendu 240)")
        }
    }
    if ($Content -match 'CONFIG_ESP_TASK_WDT_TIMEOUT_S=(\d+)') {
        if ($Matches[1] -ne "30") {
            $warnings.Add("$Label : CONFIG_ESP_TASK_WDT_TIMEOUT_S=$($Matches[1]) (attendu 30 ; runtime peut corriger)")
        }
    }
}

Write-Host "=== verify_wroom_sdkconfig : $Environment ===" -ForegroundColor Cyan
Write-Host "Build dir: $buildDir"

if (Test-Path -LiteralPath $firmwareBin) {
    $len = (Get-Item -LiteralPath $firmwareBin).Length
    Write-Host "firmware.bin: $len octets"
    if ($len -lt $MinFirmwareBytes) {
        $errors.Add(
            "firmware.bin trop petit ($len < $MinFirmwareBytes) : build incomplet (phase 1 pioarduino ?). Ne pas flasher."
        )
    }
} else {
    $errors.Add("firmware.bin introuvable : $firmwareBin")
}

$appObj = Get-ChildItem -Path $buildDir -Recurse -Filter "app.cpp.o" -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $appObj) {
    $errors.Add("app.cpp.o absent : phase 2 Arduino (FFP5CS) non compilee. Lancer 'pio run -e wroom-prod' puis 'pio run -e $Environment', ou supprimer C:\pio-builds\ffp5cs\$Environment et rebuilder.")
} else {
    Write-Host "app.cpp.o: $($appObj.FullName) ($($appObj.Length) octets)"
}

foreach ($rel in @("bootloader.bin", "partitions.bin")) {
    $p = Join-Path $buildDir $rel
    if (-not (Test-Path -LiteralPath $p)) {
        $warnings.Add("Manquant dans le build dir : $rel (upload PlatformIO peut encore le trouver via rglob)")
    }
}

$sdkCandidates = @(
    (Join-Path $buildDir "sdkconfig"),
    (Join-Path $buildDir "config\sdkconfig.h"),
    (Join-Path $ProjectRoot ".pio_artifacts\$Environment\config\sdkconfig.h")
)
$parsed = $false
foreach ($c in $sdkCandidates) {
    if (Test-Path -LiteralPath $c) {
        $content = Get-Content -LiteralPath $c -Raw -ErrorAction SilentlyContinue
        if ($content) {
            Test-SdkconfigContent -Content $content -Label $c
            $parsed = $true
        }
    }
}
if (-not $parsed) {
    $warnings.Add("Aucun sdkconfig/sdkconfig.h lisible ; verifier manuellement apres build.")
}

$customPath = Join-Path $ProjectRoot "sdkconfig_wroom_wdt.txt"
if (Test-Path -LiteralPath $customPath) {
    $cl = Get-Content -LiteralPath $customPath -Raw
    if ($cl -notmatch 'CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=240') {
        $warnings.Add("sdkconfig_wroom_wdt.txt : frequence CPU 240 MHz non explicite")
    }
} else {
    $warnings.Add("sdkconfig_wroom_wdt.txt introuvable")
}

foreach ($w in $warnings) {
    Write-Host "AVERTISSEMENT: $w" -ForegroundColor Yellow
}
foreach ($e in $errors) {
    Write-Host "ERREUR: $e" -ForegroundColor Red
}

if ($errors.Count -gt 0) {
    if ($WarnOnly) {
        Write-Host "Echec (mode WarnOnly) : $($errors.Count) erreur(s)." -ForegroundColor Yellow
        exit 0
    }
    Write-Host "Verification echouee ($($errors.Count) erreur(s))." -ForegroundColor Red
    exit 1
}

Write-Host "OK : build WROOM $Environment verifie." -ForegroundColor Green
exit 0
