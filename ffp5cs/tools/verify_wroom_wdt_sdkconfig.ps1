# Vérifie que le build wroom-test a bien un timeout TWDT à 30 s (sdkconfig ou reconfig runtime).
# Usage: après "pio run -e wroom-test", exécuter: .\tools\verify_wroom_wdt_sdkconfig.ps1
# La reconfig runtime dans app.cpp (IDF 5+) est la source de vérité; ce script vérifie le sdkconfig si présent.

$ErrorActionPreference = "Stop"
# Racine projet = parent du dossier tools
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not (Test-Path $ProjectRoot)) { $ProjectRoot = (Get-Location).Path }

$helpers = Join-Path $ProjectRoot "..\scripts\Get-PioBuildHelpers.ps1"
if (Test-Path -LiteralPath $helpers) {
    . $helpers
    $buildDir = Get-N3PioEnvBuildDir -ProjectRoot $ProjectRoot -Environment "wroom-test"
} else {
    $buildDir = Join-Path $ProjectRoot (Join-Path ".pio" (Join-Path "build" "wroom-test"))
}
$expected = "CONFIG_ESP_TASK_WDT_TIMEOUT_S=30"
$found = $false
$path = $null

# 1) sdkconfig à la racine du build
$candidates = @(
    (Join-Path $buildDir "sdkconfig"),
    (Join-Path (Join-Path $buildDir "config") "sdkconfig.h")
)
foreach ($c in $candidates) {
    if (Test-Path $c -PathType Leaf) {
        $content = Get-Content $c -Raw
        if ($content -match "CONFIG_ESP_TASK_WDT_TIMEOUT_S[=\s]+(\d+)") {
            $path = $c
            $val = $Matches[1]
            $found = ($val -eq "30")
            Write-Host "Fichier: $c"
            Write-Host "CONFIG_ESP_TASK_WDT_TIMEOUT_S=$val"
            if ($found) { Write-Host "OK: timeout 30 s présent dans le sdkconfig du build." -ForegroundColor Green }
            else { Write-Host "ATTENTION: timeout $val s (attendu 30). La reconfig runtime (app.cpp) s'applique au boot." -ForegroundColor Yellow }
            break
        }
    }
}

# 2) Recherche récursive d'un sdkconfig contenant la clé
if (-not $path) {
    $dir = $buildDir
    if (Test-Path $dir -PathType Container) {
        $files = Get-ChildItem -Path $dir -Recurse -Filter "sdkconfig" -File -ErrorAction SilentlyContinue
        foreach ($f in $files) {
            $line = Select-String -Path $f.FullName -Pattern "CONFIG_ESP_TASK_WDT_TIMEOUT_S" -List
            if ($line) {
                $path = $f.FullName
                if ((Get-Content $f.FullName -Raw) -match "CONFIG_ESP_TASK_WDT_TIMEOUT_S[=\s]+(\d+)") {
                    $val = $Matches[1]
                    $found = ($val -eq "30")
                    Write-Host "Fichier: $($f.FullName)"
                    Write-Host "CONFIG_ESP_TASK_WDT_TIMEOUT_S=$val"
                    if ($found) { Write-Host "OK: timeout 30 s présent." -ForegroundColor Green }
                    else { Write-Host "ATTENTION: timeout $val s (attendu 30)." -ForegroundColor Yellow }
                }
                break
            }
        }
    }
}

if (-not $path) {
    Write-Host "Aucun sdkconfig avec CONFIG_ESP_TASK_WDT_TIMEOUT_S trouvé dans le dossier de build wroom-test ($buildDir)."
    Write-Host "Pour un build Arduino, c'est normal: la reconfig au runtime (app.cpp) est la source de vérité."
    Write-Host "Vérifier en log au boot: [BOOT] Watchdog configuré: timeout=30000 ms (WROOM)"
}

# 3) Vérifier que le fichier custom est bien 30
$customPath = Join-Path $ProjectRoot "sdkconfig_wroom_wdt.txt"
if (Test-Path $customPath -PathType Leaf) {
    $customLine = Select-String -Path $customPath -Pattern "CONFIG_ESP_TASK_WDT_TIMEOUT_S=30"
    if ($customLine) {
        Write-Host "sdkconfig_wroom_wdt.txt: CONFIG_ESP_TASK_WDT_TIMEOUT_S=30 (OK)"
    } else {
        Write-Host "sdkconfig_wroom_wdt.txt: pas de CONFIG_ESP_TASK_WDT_TIMEOUT_S=30" -ForegroundColor Yellow
    }
}
