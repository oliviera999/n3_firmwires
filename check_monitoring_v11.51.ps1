# Script de vérification du monitoring en cours
# Vérifie l'état du monitoring toutes les 5 minutes

Write-Host "=== VERIFICATION DU MONITORING v11.51 ===" -ForegroundColor Green
Write-Host "Heure de vérification: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Yellow

# Vérifier les fichiers de log récents
$LogFiles = Get-ChildItem -Path "." -Name "monitor_20min_v11.51_*.log" | Sort-Object LastWriteTime -Descending

if ($LogFiles) {
    $LatestLog = $LogFiles[0]
    $LogInfo = Get-Item $LatestLog
    
    Write-Host "`n📁 Fichier de log actuel: $LatestLog" -ForegroundColor Cyan
    Write-Host "📊 Taille: $([math]::Round($LogInfo.Length/1KB, 2)) KB" -ForegroundColor Cyan
    Write-Host "⏰ Dernière modification: $($LogInfo.LastWriteTime)" -ForegroundColor Cyan
    
    # Calculer la durée écoulée
    $ElapsedTime = (Get-Date) - $LogInfo.LastWriteTime
    $ElapsedMinutes = [math]::Round($ElapsedTime.TotalMinutes, 1)
    
    Write-Host "Temps ecoule depuis derniere ecriture: $ElapsedMinutes minutes" -ForegroundColor Cyan
    
    if ($ElapsedMinutes -lt 1) {
        Write-Host "Monitoring actif - ecriture recente" -ForegroundColor Green
    } elseif ($ElapsedMinutes -lt 5) {
        Write-Host "Monitoring possible - ecriture recente" -ForegroundColor Yellow
    } else {
        Write-Host "Monitoring probablement termine" -ForegroundColor Red
    }
    
    # Afficher les dernières lignes du log
    Write-Host "`nDernieres lignes du log:" -ForegroundColor Magenta
    $LastLines = Get-Content $LatestLog -Tail 10
    $LastLines | ForEach-Object {
        Write-Host "  $_" -ForegroundColor Gray
    }
    
} else {
    Write-Host "❌ Aucun fichier de log de monitoring trouvé" -ForegroundColor Red
}

# Vérifier les processus PlatformIO
$PioProcesses = Get-Process | Where-Object {$_.ProcessName -like "*pio*" -or $_.ProcessName -like "*python*"} | Select-Object ProcessName, Id, CPU, WorkingSet

if ($PioProcesses) {
    Write-Host "`n🔧 Processus PlatformIO actifs:" -ForegroundColor Blue
    $PioProcesses | ForEach-Object {
        Write-Host "  - $($_.ProcessName) (PID: $($_.Id), CPU: $($_.CPU), RAM: $([math]::Round($_.WorkingSet/1MB, 2)) MB)" -ForegroundColor Blue
    }
} else {
    Write-Host "`n⚠️ Aucun processus PlatformIO détecté" -ForegroundColor Yellow
}

Write-Host "`n=== FIN DE LA VERIFICATION ===" -ForegroundColor Green
