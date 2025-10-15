# Script de build de production pour ESP32 FFP5CS
# Minifie les assets, compile et prépare pour upload

param(
    [switch]$SkipMinify,
    [switch]$UploadFS,
    [switch]$UploadFirmware,
    [string]$Port = "COM3"
)

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "   Build Production ESP32 FFP5CS" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""

# Vérifier que nous sommes dans le bon répertoire
if (-not (Test-Path "platformio.ini")) {
    Write-Host "❌ Erreur: platformio.ini introuvable" -ForegroundColor Red
    Write-Host "   Exécutez ce script depuis la racine du projet" -ForegroundColor Yellow
    exit 1
}

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

pio run

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
if (Test-Path ".pio/build/esp32dev/firmware.bin") {
    $firmwareSize = (Get-Item ".pio/build/esp32dev/firmware.bin").Length
    Write-Host "📊 Taille firmware: $([math]::Round($firmwareSize/1KB, 2)) KB" -ForegroundColor Cyan
}

# Étape 5: Build filesystem
Write-Host ""
Write-Host "💿 Étape 5: Build filesystem" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Gray

pio run --target buildfs

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
if (Test-Path ".pio/build/esp32dev/littlefs.bin") {
    $fsSize = (Get-Item ".pio/build/esp32dev/littlefs.bin").Length
    Write-Host "📊 Taille filesystem: $([math]::Round($fsSize/1KB, 2)) KB" -ForegroundColor Cyan
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
    Write-Host "📤 Upload vers ESP32 (port: $Port)" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor Gray
    
    if ($UploadFS) {
        Write-Host "⏳ Upload filesystem..." -ForegroundColor Cyan
        
        # Temporairement utiliser data_minified
        if (Test-Path "data_minified") {
            Rename-Item -Path "data" -NewName "data_dev"
            Rename-Item -Path "data_minified" -NewName "data"
        }
        
        pio run --target uploadfs --upload-port $Port
        
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
        pio run --target upload --upload-port $Port
        
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
Write-Host "  • .pio/build/esp32dev/firmware.bin" -ForegroundColor White
Write-Host "  • .pio/build/esp32dev/littlefs.bin" -ForegroundColor White

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

