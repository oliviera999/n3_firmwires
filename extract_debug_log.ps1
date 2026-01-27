# Script pour extraire le fichier de log de debug depuis LittleFS vers .cursor/debug.log
# Usage: .\extract_debug_log.ps1

$ErrorActionPreference = "Stop"

# Configuration
$PROJECT_ROOT = $PSScriptRoot
$DEBUG_LOG_FS = "/debug.log"
$DEBUG_LOG_LOCAL = Join-Path $PROJECT_ROOT ".cursor\debug.log"
$ENV_NAME = "wroom-test"

Write-Host "=== Extraction du log de debug depuis LittleFS ===" -ForegroundColor Cyan

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

# Méthode 1: Utiliser pio run -t uploadfs pour uploader un fichier vide, puis lire via Serial
# Méthode 2: Utiliser un endpoint HTTP pour télécharger le fichier (si web server actif)
# Méthode 3: Utiliser Serial pour envoyer le contenu du fichier ligne par ligne

Write-Host ""
Write-Host "Méthode recommandée: Utiliser Serial pour capturer les logs NDJSON" -ForegroundColor Yellow
Write-Host ""
Write-Host "Alternative: Si le web server est actif, ajoutez un endpoint /api/debug-log" -ForegroundColor Yellow
Write-Host "qui retourne le contenu de /debug.log depuis LittleFS" -ForegroundColor Yellow
Write-Host ""

# Pour l'instant, on va créer un script qui utilise Serial
# L'utilisateur devra modifier le firmware pour envoyer les logs via Serial
# ou utiliser l'endpoint HTTP

Write-Host "Pour extraire les logs:" -ForegroundColor Cyan
Write-Host "1. Option A: Modifier le firmware pour envoyer /debug.log via Serial (format NDJSON)" -ForegroundColor White
Write-Host "2. Option B: Ajouter un endpoint HTTP /api/debug-log qui retourne le fichier" -ForegroundColor White
Write-Host "3. Option C: Utiliser pio run -t uploadfs et un script de lecture" -ForegroundColor White
Write-Host ""

# Créer un fichier vide si nécessaire (pour que l'outil de debug puisse le lire)
if (-not (Test-Path $DEBUG_LOG_LOCAL)) {
    New-Item -ItemType File -Path $DEBUG_LOG_LOCAL -Force | Out-Null
    Write-Host "✓ Fichier .cursor/debug.log créé (vide)" -ForegroundColor Green
} else {
    Write-Host "✓ Fichier .cursor/debug.log existe déjà" -ForegroundColor Green
}

Write-Host ""
Write-Host "Note: Pour capturer les logs depuis Serial, utilisez:" -ForegroundColor Yellow
Write-Host "  pio device monitor --filter time | Select-String '^\{' | Out-File -Append .cursor\debug.log" -ForegroundColor White
Write-Host ""
