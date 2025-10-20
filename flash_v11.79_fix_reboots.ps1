# Script de flash v11.79 - Fix des reboots causés par les problèmes JSON dans GPIO parser
# Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

Write-Host "=== FLASH V11.79 - FIX REBOOTS ===" -ForegroundColor Green
Write-Host "Version: Fix reboots v11.78 - sécurisation JSON et mémoire dans GPIO parser" -ForegroundColor Yellow
Write-Host ""

# Arrêter tout processus qui pourrait bloquer le port COM
Write-Host "1. Vérification des processus bloquant COM6..." -ForegroundColor Cyan
try {
    $comProcesses = Get-Process | Where-Object { $_.ProcessName -like "*python*" -or 
                                               $_.ProcessName -like "*pio*" -or
                                               $_.MainWindowTitle -like "*monitor*" -or 
                                               $_.MainWindowTitle -like "*serial*" }
    if ($comProcesses) {
        Write-Host "Processus détectés: $($comProcesses.Count)" -ForegroundColor Yellow
        foreach ($proc in $comProcesses) {
            Write-Host "  - $($proc.ProcessName) (ID: $($proc.Id))" -ForegroundColor Yellow
        }
    } else {
        Write-Host "Aucun processus bloquant détecté" -ForegroundColor Green
    }
} catch {
    Write-Host "Erreur lors de la vérification des processus: $($_.Exception.Message)" -ForegroundColor Red
}

# Attendre un peu pour laisser le temps aux processus de se libérer
Write-Host "2. Attente de libération du port (5 secondes)..." -ForegroundColor Cyan
Start-Sleep -Seconds 5

# Compiler d'abord pour vérifier qu'il n'y a pas d'erreurs
Write-Host "3. Compilation du firmware v11.79..." -ForegroundColor Cyan
$compileResult = pio run
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur de compilation !" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "✅ Compilation réussie" -ForegroundColor Green

# Flash du firmware
Write-Host "4. Flash du firmware..." -ForegroundColor Cyan
$uploadResult = pio run --target upload
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash firmware réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash firmware (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryResult = pio run --target upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash firmware réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash firmware" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Flash du système de fichiers
Write-Host "5. Flash du système de fichiers SPIFFS..." -ForegroundColor Cyan
Start-Sleep -Seconds 3  # Petit délai entre les flashes
$uploadfsResult = pio run --target uploadfs
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Flash SPIFFS réussi !" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur flash SPIFFS (code: $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Tentative avec reset manuel du port..." -ForegroundColor Yellow
    
    # Attendre plus longtemps et réessayer
    Start-Sleep -Seconds 10
    $retryfsResult = pio run --target uploadfs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash SPIFFS réussi au 2ème essai !" -ForegroundColor Green
    } else {
        Write-Host "❌ Échec définitif du flash SPIFFS" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "=== FLASH COMPLET RÉUSSI ===" -ForegroundColor Green
Write-Host "Version v11.79 flashée avec succès !" -ForegroundColor Green
Write-Host ""
Write-Host "Corrections apportées:" -ForegroundColor Yellow
Write-Host "- Augmentation taille StaticJsonDocument de 512 à 1024 bytes" -ForegroundColor White
Write-Host "- Sécurisation assignation JSON avec vérifications de taille" -ForegroundColor White  
Write-Host "- Sécurisation copie emailAddress avec vérification pointeur null" -ForegroundColor White
Write-Host "- Ajout protections supplémentaires pour éviter corruptions mémoire" -ForegroundColor White
Write-Host ""
Write-Host "Recommandation: Exécuter le monitoring de 90 secondes pour vérifier la stabilité" -ForegroundColor Cyan




