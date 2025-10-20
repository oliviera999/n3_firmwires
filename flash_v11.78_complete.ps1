# Script de flash complet ESP32 v11.78
# Flash firmware + système de fichiers SPIFFS

Write-Host "=== FLASH COMPLET ESP32 v11.78 ===" -ForegroundColor Green
Write-Host "Correction GPIO virtuels - application immédiate des changements depuis serveur distant" -ForegroundColor Yellow
Write-Host ""

# Vérifier que nous sommes dans le bon répertoire
if (-not (Test-Path "platformio.ini")) {
    Write-Host "ERREUR: platformio.ini non trouvé. Exécutez ce script depuis la racine du projet." -ForegroundColor Red
    exit 1
}

# Compilation
Write-Host "1/3 - Compilation du firmware..." -ForegroundColor Cyan
pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERREUR: Échec de la compilation" -ForegroundColor Red
    exit 1
}

Write-Host "✅ Compilation réussie" -ForegroundColor Green

# Flash firmware
Write-Host "2/3 - Flash du firmware vers ESP32..." -ForegroundColor Cyan
Write-Host "Assurez-vous que l'ESP32 est connecté et en mode flash" -ForegroundColor Yellow
pio run -t upload
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERREUR: Échec du flash firmware" -ForegroundColor Red
    Write-Host "Vérifiez la connexion USB et réessayez manuellement" -ForegroundColor Yellow
    exit 1
}

Write-Host "✅ Flash firmware réussi" -ForegroundColor Green

# Flash système de fichiers
Write-Host "3/3 - Flash du système de fichiers SPIFFS..." -ForegroundColor Cyan
pio run -t uploadfs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERREUR: Échec du flash SPIFFS" -ForegroundColor Red
    Write-Host "Vérifiez la connexion USB et réessayez manuellement" -ForegroundColor Yellow
    exit 1
}

Write-Host "✅ Flash SPIFFS réussi" -ForegroundColor Green

Write-Host ""
Write-Host "=== FLASH COMPLET TERMINÉ ===" -ForegroundColor Green
Write-Host "Version 11.78 déployée avec succès !" -ForegroundColor Green
Write-Host ""
Write-Host "Améliorations v11.78:" -ForegroundColor Cyan
Write-Host "- Fix GPIO virtuels: application immédiate des changements depuis serveur distant" -ForegroundColor White
Write-Host "- Les modifications de configuration sont maintenant prises en compte en temps réel" -ForegroundColor White
Write-Host ""
Write-Host "Recommandation: Effectuer un monitoring de 90 secondes pour vérifier la stabilité" -ForegroundColor Yellow




