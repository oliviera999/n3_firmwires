# Script Flash + Monitor v11.05 - Validation Obligatoire
# Date: 2025-10-12
# Phase 2 @ 100% - Validation finale

$ErrorActionPreference = "Continue"

Write-Host "╔═══════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║    FLASH + MONITOR v11.05 - VALIDATION OBLIGATOIRE    ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# Chemin du projet
$projectPath = "C:\Users\olivi\Mon Drive\travail\##olution\##Projets\##prototypage\platformIO\Projects\ffp5cs"
Set-Location $projectPath

Write-Host "📁 Projet: $projectPath" -ForegroundColor Green
Write-Host ""

# Étape 1: Détection port
Write-Host "🔍 ÉTAPE 1: Détection port COM..." -ForegroundColor Yellow
Write-Host ""
pio device list
Write-Host ""

# Étape 2: Flash firmware
Write-Host "⚡ ÉTAPE 2: Flash firmware v11.05..." -ForegroundColor Yellow
Write-Host "   Environment: wroom-prod" -ForegroundColor Gray
Write-Host "   Flash: 82.3% (1.67MB)" -ForegroundColor Gray
Write-Host "   RAM: 19.5% (64KB)" -ForegroundColor Gray
Write-Host ""

$uploadResult = pio run -e wroom-prod -t upload 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Firmware flashé avec succès !" -ForegroundColor Green
} else {
    Write-Host "❌ Échec flash firmware" -ForegroundColor Red
    Write-Host ""
    Write-Host "ERREUR DÉTECTÉE:" -ForegroundColor Red
    $uploadResult | Select-String -Pattern "error|Error|fatal" | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "SOLUTIONS:" -ForegroundColor Yellow
    Write-Host "  1. Fermer tous les monitors série (Ctrl+C dans autres terminaux)" -ForegroundColor Yellow
    Write-Host "  2. Fermer Arduino IDE / PuTTY / autres outils série" -ForegroundColor Yellow
    Write-Host "  3. Débrancher/rebrancher ESP32" -ForegroundColor Yellow
    Write-Host "  4. Relancer ce script" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Appuyez sur une touche pour continuer malgré l'erreur..." -ForegroundColor Gray
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}
Write-Host ""

# Étape 3: Flash filesystem
Write-Host "💾 ÉTAPE 3: Flash filesystem (SPIFFS)..." -ForegroundColor Yellow
Write-Host "   Assets web: data/" -ForegroundColor Gray
Write-Host "   Taille: ~400KB" -ForegroundColor Gray
Write-Host ""

$fsResult = pio run -e wroom-prod -t uploadfs 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Filesystem flashé avec succès !" -ForegroundColor Green
} else {
    Write-Host "❌ Échec flash filesystem" -ForegroundColor Red
    Write-Host ""
    Write-Host "ERREUR:" -ForegroundColor Red
    $fsResult | Select-String -Pattern "error|Error|fatal" | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
Write-Host ""

# Étape 4: Monitor 90 secondes
Write-Host "📊 ÉTAPE 4: Monitor 90 secondes..." -ForegroundColor Yellow
Write-Host "   Baud: 115200" -ForegroundColor Gray
Write-Host "   Fichier: monitor_v11.05_validation.log" -ForegroundColor Gray
Write-Host ""
Write-Host "⏱️  Démarrage monitoring... (90 secondes)" -ForegroundColor Cyan

$monitorFile = "monitor_v11.05_validation.log"
$monitorJob = Start-Job -ScriptBlock {
    param($path, $logFile)
    Set-Location $path
    pio device monitor --baud 115200 2>&1 | Out-File -Encoding UTF8 $logFile
} -ArgumentList $projectPath, $monitorFile

# Attendre 90 secondes avec barre de progression
Write-Host ""
for ($i = 1; $i -le 90; $i++) {
    $percent = [math]::Round(($i / 90) * 100)
    Write-Progress -Activity "Monitoring ESP32 v11.05" -Status "$i/90 secondes ($percent%)" -PercentComplete $percent
    Start-Sleep -Seconds 1
}

Write-Progress -Activity "Monitoring ESP32 v11.05" -Completed
Stop-Job -Job $monitorJob
Remove-Job -Job $monitorJob -Force

Write-Host ""
Write-Host "✅ Monitoring 90s terminé !" -ForegroundColor Green
Write-Host "   Logs sauvegardés: $monitorFile" -ForegroundColor Gray
Write-Host ""

# Étape 5: Analyse rapide logs
Write-Host "🔍 ÉTAPE 5: Analyse logs (priorités)..." -ForegroundColor Yellow
Write-Host ""

if (Test-Path $monitorFile) {
    $logContent = Get-Content $monitorFile -ErrorAction SilentlyContinue
    
    # Priorité 1: CRITIQUE
    Write-Host "🔴 PRIORITÉ 1 - ERREURS CRITIQUES:" -ForegroundColor Red
    $critical = $logContent | Select-String -Pattern "Guru|Panic|Brownout|Core dump|Task watchdog" -ErrorAction SilentlyContinue
    if ($critical) {
        $critical | ForEach-Object { Write-Host "  ⚠️  $_" -ForegroundColor Red }
    } else {
        Write-Host "  ✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    Write-Host ""
    
    # Priorité 2: IMPORTANT
    Write-Host "🟡 PRIORITÉ 2 - WARNINGS IMPORTANTS:" -ForegroundColor Yellow
    $warnings = $logContent | Select-String -Pattern "WiFi.*timeout|WebSocket.*disconnect|malloc failed|Stack overflow" -ErrorAction SilentlyContinue | Select-Object -First 5
    if ($warnings) {
        $warnings | ForEach-Object { Write-Host "  ⚠️  $_" -ForegroundColor Yellow }
    } else {
        Write-Host "  ✅ Aucun warning important" -ForegroundColor Green
    }
    Write-Host ""
    
    # Priorité 3: INFO - Mémoire
    Write-Host "🟢 PRIORITÉ 3 - MÉMOIRE:" -ForegroundColor Green
    $heap = $logContent | Select-String -Pattern "Heap|heap.*bytes|minimum historique" -ErrorAction SilentlyContinue | Select-Object -First 10
    if ($heap) {
        $heap | ForEach-Object { Write-Host "  ℹ️  $_" -ForegroundColor Cyan }
    } else {
        Write-Host "  ℹ️  Pas d'info mémoire dans logs" -ForegroundColor Gray
    }
    Write-Host ""
    
    # WiFi/WebSocket
    Write-Host "📡 CONNECTIVITÉ:" -ForegroundColor Cyan
    $network = $logContent | Select-String -Pattern "WiFi connected|IP address|WebSocket.*client" -ErrorAction SilentlyContinue | Select-Object -First 5
    if ($network) {
        $network | ForEach-Object { Write-Host "  ℹ️  $_" -ForegroundColor Cyan }
    } else {
        Write-Host "  ℹ️  Pas d'info réseau dans logs" -ForegroundColor Gray
    }
    Write-Host ""
} else {
    Write-Host "⚠️  Fichier log non trouvé: $monitorFile" -ForegroundColor Red
    Write-Host "   Le monitoring n'a probablement pas pu se lancer (port occupé)" -ForegroundColor Yellow
}

# Résumé
Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║           MONITORING TERMINÉ - RÉSUMÉ                 ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""
Write-Host "📄 Fichier logs: $monitorFile" -ForegroundColor White
Write-Host "📋 Prochain: Analyser en détail et créer VALIDATION_V11.05_FINALE.md" -ForegroundColor White
Write-Host ""
Write-Host "🎯 VALIDATION PRODUCTION:" -ForegroundColor Yellow
Write-Host "   - Vérifier aucun Panic/Guru/Brownout" -ForegroundColor Yellow
Write-Host "   - Vérifier heap minimum > 50KB" -ForegroundColor Yellow
Write-Host "   - Vérifier WiFi stable" -ForegroundColor Yellow
Write-Host "   - Documenter dans VALIDATION_V11.05_FINALE.md" -ForegroundColor Yellow
Write-Host ""
Write-Host "✅ Script terminé" -ForegroundColor Green

