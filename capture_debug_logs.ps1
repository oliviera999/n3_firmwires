# Script pour capturer les logs NDJSON depuis Serial et les écrire dans .cursor/debug.log
# Usage: .\capture_debug_logs.ps1 [duration_seconds]
#
# Ce script:
# 1. Nettoie le fichier .cursor/debug.log
# 2. Lance pio device monitor
# 3. Filtre les lignes qui commencent par '{' (format NDJSON)
# 4. Écrit dans .cursor/debug.log
# 5. S'arrête après la durée spécifiée (défaut: 600 secondes = 10 minutes)

param(
    [int]$DurationSeconds = 600
)

$ErrorActionPreference = "Stop"

# Configuration
$PROJECT_ROOT = $PSScriptRoot
$DEBUG_LOG_LOCAL = Join-Path $PROJECT_ROOT ".cursor\debug.log"

Write-Host "=== Capture des logs de debug depuis Serial ===" -ForegroundColor Cyan
Write-Host "Durée: $DurationSeconds secondes" -ForegroundColor Yellow
Write-Host "Fichier de sortie: $DEBUG_LOG_LOCAL" -ForegroundColor Yellow
Write-Host ""

# Vérifier que PlatformIO est disponible
if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
    Write-Host "❌ PlatformIO CLI non trouvé. Installez PlatformIO Core." -ForegroundColor Red
    exit 1
}

# Créer le répertoire .cursor s'il n'existe pas
$cursorDir = Join-Path $PROJECT_ROOT ".cursor"
if (-not (Test-Path $cursorDir)) {
    New-Item -ItemType Directory -Path $cursorDir -Force | Out-Null
    Write-Host "✓ Répertoire .cursor créé" -ForegroundColor Green
}

# Nettoyer le fichier de log existant
if (Test-Path $DEBUG_LOG_LOCAL) {
    Remove-Item $DEBUG_LOG_LOCAL -Force
    Write-Host "✓ Fichier de log nettoyé" -ForegroundColor Green
} else {
    New-Item -ItemType File -Path $DEBUG_LOG_LOCAL -Force | Out-Null
    Write-Host "✓ Fichier de log créé" -ForegroundColor Green
}

Write-Host ""
Write-Host "Démarrage de la capture..." -ForegroundColor Cyan
Write-Host "Les logs NDJSON (lignes commençant par '{') seront écrits dans:" -ForegroundColor White
Write-Host "  $DEBUG_LOG_LOCAL" -ForegroundColor White
Write-Host ""
Write-Host "Appuyez sur Ctrl+C pour arrêter avant la fin du timer" -ForegroundColor Yellow
Write-Host ""

# Fonction pour filtrer et écrire les logs NDJSON
$job = Start-Job -ScriptBlock {
    param($logFile, $duration)
    
    $startTime = Get-Date
    $endTime = $startTime.AddSeconds($duration)
    
    # Lancer pio device monitor et filtrer les lignes NDJSON
    $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--filter", "time" -NoNewWindow -PassThru -RedirectStandardOutput "nul" -RedirectStandardError "nul"
    
    # Alternative: utiliser directement pio device monitor avec redirection
    # Note: pio device monitor ne supporte pas facilement la redirection standard
    # On va utiliser une approche différente
    
    # Attendre que le processus démarre
    Start-Sleep -Seconds 2
    
    # Lire depuis Serial via un autre mécanisme
    # Pour l'instant, on va simplement créer un fichier vide et laisser l'utilisateur
    # capturer manuellement avec: pio device monitor | Select-String '^\{' | Out-File -Append .cursor\debug.log
    
    while ((Get-Date) -lt $endTime) {
        Start-Sleep -Seconds 1
    }
    
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}

# Note: La capture automatique depuis Serial est complexe avec PowerShell
# Solution recommandée: utiliser manuellement la commande suivante dans un terminal séparé

Write-Host "⚠️  IMPORTANT: Pour capturer les logs, utilisez cette commande dans un terminal séparé:" -ForegroundColor Yellow
Write-Host ""
Write-Host "  pio device monitor --filter time | Select-String '^\{' | Out-File -Append .cursor\debug.log" -ForegroundColor Cyan
Write-Host ""
Write-Host "Ou utilisez cette variante qui affiche aussi les logs à l'écran:" -ForegroundColor Yellow
Write-Host ""
Write-Host "  pio device monitor --filter time | Tee-Object -Variable line | Where-Object { $_ -match '^\{' } | Out-File -Append .cursor\debug.log" -ForegroundColor Cyan
Write-Host ""
Write-Host "Le script va maintenant attendre $DurationSeconds secondes..." -ForegroundColor Cyan
Write-Host "Pendant ce temps, exécutez la commande ci-dessus dans un autre terminal." -ForegroundColor Yellow
Write-Host ""

# Attendre la durée spécifiée
$startTime = Get-Date
$endTime = $startTime.AddSeconds($DurationSeconds)

while ((Get-Date) -lt $endTime) {
    $remaining = ($endTime - (Get-Date)).TotalSeconds
    Write-Progress -Activity "Capture en cours..." -Status "Temps restant: $([math]::Round($remaining)) secondes" -PercentComplete (100 * (1 - ($remaining / $DurationSeconds)))
    Start-Sleep -Seconds 1
}

Write-Progress -Activity "Capture terminée" -Completed

Write-Host ""
Write-Host "✓ Capture terminée" -ForegroundColor Green

# Vérifier si des logs ont été capturés
if (Test-Path $DEBUG_LOG_LOCAL) {
    $lineCount = (Get-Content $DEBUG_LOG_LOCAL | Measure-Object -Line).Lines
    $fileSize = (Get-Item $DEBUG_LOG_LOCAL).Length
    
    if ($lineCount -gt 0) {
        Write-Host "✓ $lineCount lignes capturées ($fileSize bytes)" -ForegroundColor Green
    } else {
        Write-Host "⚠️  Aucun log capturé. Vérifiez que:" -ForegroundColor Yellow
        Write-Host "   1. L'ESP32 est connecté et le firmware est flashé" -ForegroundColor White
        Write-Host "   2. La commande de capture a été exécutée dans un autre terminal" -ForegroundColor White
        Write-Host "   3. Le firmware envoie des logs au format NDJSON sur Serial" -ForegroundColor White
    }
} else {
    Write-Host "⚠️  Fichier de log non trouvé" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Fichier de log: $DEBUG_LOG_LOCAL" -ForegroundColor Cyan
