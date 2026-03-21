# =============================================================================
# Script de build de production pour ESP32 FFP5CS
# =============================================================================
# Description:
#   Minifie les assets web, compile le firmware et le filesystem pour
#   l'environnement de production. Optionnellement upload sur l'ESP32.
#
# Prérequis:
#   - PlatformIO installé et dans le PATH
#   - Python 3.x installé (pour minification)
#   - PowerShell 5.1+ (Windows)
#
# Usage:
#   .\scripts\build_production.ps1                    # Build uniquement
#   .\scripts\build_production.ps1 -UploadFS          # Build + upload filesystem
#   .\scripts\build_production.ps1 -UploadFirmware    # Build + upload firmware
#   .\scripts\build_production.ps1 -SkipMinify        # Build sans minification
#   .\scripts\build_production.ps1 -Port COM4         # Spécifier le port COM
#
# Paramètres:
#   -SkipMinify      : Ignore la minification des assets
#   -UploadFS        : Upload le filesystem après build
#   -UploadFirmware  : Upload le firmware après build
#   -Port            : Port COM de l'ESP32 (défaut: COM3)
# =============================================================================

param(
    [switch]$SkipMinify,
    [switch]$UploadFS,
    [switch]$UploadFirmware,
    [string]$Port = "COM3",
    [string]$Environment = "wroom-prod"
)

$projectDir = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $projectDir "platformio.ini"))) {
    Write-Host "Erreur: platformio.ini introuvable sous $projectDir" -ForegroundColor Red
    exit 1
}

$helpers = Join-Path $projectDir "..\scripts\Get-PioBuildHelpers.ps1"
if (-not (Test-Path -LiteralPath $helpers)) {
    Write-Host "Erreur: introuvable $helpers" -ForegroundColor Red
    exit 1
}
. $helpers
Write-N3PioWorkspaceAdvice -ProjectRoot $projectDir

Push-Location $projectDir
try {

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "   Build Production ESP32 FFP5CS ($Environment)" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""

# Étape 1: Minification des assets
if (-not $SkipMinify) {
    Write-Host "📦 Étape 1: Minification des assets" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor Gray
    
    if (Test-Path "scripts/minify_assets.py") {
        python scripts/minify_assets.py
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host "❌ Erreur lors de la minification" -ForegroundColor Red
            exit 1
        }
        
        Write-Host "✅ Minification terminée" -ForegroundColor Green
    } else {
        Write-Host "⚠️  Script de minification introuvable, skip" -ForegroundColor Yellow
    }
    Write-Host ""
} else {
    Write-Host "⏭️  Minification skippée (--SkipMinify)" -ForegroundColor Gray
    Write-Host ""
}

# Étape 2: Backup data/ original
Write-Host "💾 Étape 2: Backup data/ original" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Gray

if (Test-Path "data_original") {
    Write-Host "ℹ️  Backup existant, skip" -ForegroundColor Gray
} else {
    if (Test-Path "data") {
        Copy-Item -Path "data" -Destination "data_original" -Recurse
        Write-Host "✅ Backup créé: data_original/" -ForegroundColor Green
    }
}
Write-Host ""

# Étape 3: Utiliser data_minified si disponible
Write-Host "🔄 Étape 3: Sélection des assets" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Gray

$useMinified = $false
if (Test-Path "data_minified") {
    Write-Host "✅ data_minified/ trouvé, utilisation pour le build" -ForegroundColor Green
    
    # Renommer temporairement
    if (Test-Path "data") {
        Rename-Item -Path "data" -NewName "data_dev"
    }
    Rename-Item -Path "data_minified" -NewName "data"
    $useMinified = $true
} else {
    Write-Host "ℹ️  data_minified/ non trouvé, utilisation de data/" -ForegroundColor Gray
}
Write-Host ""

# Étape 4: Compilation firmware
Write-Host "🔨 Étape 4: Compilation firmware" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Gray

pio run -e $Environment

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation" -ForegroundColor Red
    
    # Restaurer data/ si renommé
    if ($useMinified) {
        Rename-Item -Path "data" -NewName "data_minified"
        if (Test-Path "data_dev") {
            Rename-Item -Path "data_dev" -NewName "data"
        }
    }
    
    exit 1
}

Write-Host "✅ Compilation réussie" -ForegroundColor Green
Write-Host ""

# Afficher tailles
$fwBin = Get-N3PioFirmwareBin -ProjectRoot $projectDir -Environment $Environment
if (Test-Path -LiteralPath $fwBin) {
    $firmwareSize = (Get-Item -LiteralPath $fwBin).Length
    Write-Host "📊 Taille firmware: $([math]::Round($firmwareSize/1KB, 2)) KB ($fwBin)" -ForegroundColor Cyan
}

# Étape 5: Build filesystem
Write-Host ""
Write-Host "💿 Étape 5: Build filesystem" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Gray

pio run -e $Environment -t buildfs

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur build filesystem" -ForegroundColor Red
    
    # Restaurer data/ si renommé
    if ($useMinified) {
        Rename-Item -Path "data" -NewName "data_minified"
        if (Test-Path "data_dev") {
            Rename-Item -Path "data_dev" -NewName "data"
        }
    }
    
    exit 1
}

Write-Host "✅ Filesystem build réussi" -ForegroundColor Green
Write-Host ""

# Afficher tailles filesystem
$fsBin = Get-N3PioLittlefsBin -ProjectRoot $projectDir -Environment $Environment
if (Test-Path -LiteralPath $fsBin) {
    $fsSize = (Get-Item -LiteralPath $fsBin).Length
    Write-Host "📊 Taille filesystem: $([math]::Round($fsSize/1KB, 2)) KB ($fsBin)" -ForegroundColor Cyan
}

# Restaurer data/ si renommé
if ($useMinified) {
    Write-Host ""
    Write-Host "🔄 Restauration structure data/" -ForegroundColor Yellow
    Rename-Item -Path "data" -NewName "data_minified"
    if (Test-Path "data_dev") {
        Rename-Item -Path "data_dev" -NewName "data"
    }
    Write-Host "✅ Structure restaurée" -ForegroundColor Green
}

Write-Host ""
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "   Build Production Terminé ✅" -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""

# Étape 6: Upload (optionnel)
if ($UploadFS -or $UploadFirmware) {
    $projectRoot = Split-Path $PSScriptRoot -Parent
    . (Join-Path $projectRoot "scripts\Release-ComPort.ps1")
    Release-ComPortIfNeeded -Port $Port

    Write-Host "📤 Upload vers ESP32 (port: $Port)" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor Gray
    
    if ($UploadFS) {
        Write-Host "⏳ Upload filesystem..." -ForegroundColor Cyan
        
        # Temporairement utiliser data_minified
        if (Test-Path "data_minified") {
            Rename-Item -Path "data" -NewName "data_dev"
            Rename-Item -Path "data_minified" -NewName "data"
        }
        
        pio run -e $Environment -t uploadfs --upload-port $Port
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Filesystem uploadé" -ForegroundColor Green
        } else {
            Write-Host "❌ Erreur upload filesystem" -ForegroundColor Red
        }
        
        # Restaurer
        if (Test-Path "data_dev") {
            Rename-Item -Path "data" -NewName "data_minified"
            Rename-Item -Path "data_dev" -NewName "data"
        }
    }
    
    if ($UploadFirmware) {
        Write-Host "⏳ Upload firmware..." -ForegroundColor Cyan
        pio run -e $Environment -t upload --upload-port $Port
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Firmware uploadé" -ForegroundColor Green
        } else {
            Write-Host "❌ Erreur upload firmware" -ForegroundColor Red
        }
    }
    
    Write-Host ""
}

# Résumé final
Write-Host "📋 Fichiers générés:" -ForegroundColor Cyan
Write-Host "  • $fwBin" -ForegroundColor White
Write-Host "  • $fsBin" -ForegroundColor White

if (Test-Path "data_minified") {
    Write-Host "  • data_minified/ (assets optimisés)" -ForegroundColor White
}

Write-Host ""
Write-Host "💡 Commandes suivantes:" -ForegroundColor Yellow
if (-not ($UploadFS -or $UploadFirmware)) {
    Write-Host "  .\scripts\build_production.ps1 -UploadFS -Port COM3" -ForegroundColor Gray
    Write-Host "  .\scripts\build_production.ps1 -UploadFirmware -Port COM3" -ForegroundColor Gray
}
Write-Host "  pio device monitor --port $Port --baud 115200" -ForegroundColor Gray
Write-Host ""

} finally {
    Pop-Location
}
