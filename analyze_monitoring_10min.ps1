# Analyse des logs de monitoring ESP32 v11.77
param(
    [string]$LogFile = "monitor_10min_v11.77_complete_flash_2025-10-19_11-03-46.log"
)

Write-Host "=== ANALYSE DES LOGS ESP32 FFP5CS v11.77 ===" -ForegroundColor Green

if (Test-Path $LogFile) {
    $logContent = Get-Content $LogFile
    
    Write-Host "Fichier analyse: $LogFile" -ForegroundColor Cyan
    Write-Host "Nombre total de lignes: $($logContent.Count)" -ForegroundColor White
    
    # Recherche d'erreurs critiques
    $errors = $logContent | Where-Object { $_ -match "ERROR|Panic|Guru|Brownout|Core dump|ASSERT|Fatal" }
    $warnings = $logContent | Where-Object { $_ -match "WARN|Warning|Timeout|Failed" }
    
    Write-Host "`n=== RESULTATS ===" -ForegroundColor Yellow
    
    if ($errors.Count -gt 0) {
        Write-Host "ERREURS CRITIQUES: $($errors.Count)" -ForegroundColor Red
        $errors | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    } else {
        Write-Host "Aucune erreur critique detectee" -ForegroundColor Green
    }
    
    if ($warnings.Count -gt 0) {
        Write-Host "AVERTISSEMENTS: $($warnings.Count)" -ForegroundColor Yellow
        $warnings | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    }
    
    # Dernieres lignes
    Write-Host "`n=== DERNIERES LIGNES ===" -ForegroundColor Cyan
    $logContent | Select-Object -Last 15
} else {
    Write-Host "Fichier $LogFile introuvable" -ForegroundColor Red
}




