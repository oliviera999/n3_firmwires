# Surveillance du monitoring ESP32-WROOM v11.49
# Vérification périodique de l'état du monitoring

Write-Host "🔍 Surveillance du monitoring ESP32-WROOM v11.49" -ForegroundColor Green
Write-Host "📅 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

# Vérifier les processus PlatformIO actifs
Write-Host "`n🔧 Processus PlatformIO actifs:" -ForegroundColor Yellow
$pioProcesses = Get-Process | Where-Object {$_.ProcessName -like "*pio*"}
if ($pioProcesses) {
    $pioProcesses | Format-Table ProcessName, Id, CPU, WorkingSet -AutoSize
} else {
    Write-Host "   Aucun processus PlatformIO détecté" -ForegroundColor Red
}

# Vérifier les processus PowerShell actifs
Write-Host "`n💻 Processus PowerShell actifs:" -ForegroundColor Yellow
$psProcesses = Get-Process | Where-Object {$_.ProcessName -like "*powershell*"}
if ($psProcesses) {
    $psProcesses | Format-Table ProcessName, Id, CPU, WorkingSet -AutoSize
} else {
    Write-Host "   Aucun processus PowerShell détecté" -ForegroundColor Red
}

# Vérifier les fichiers de log créés
Write-Host "`n📝 Fichiers de log ESP32-WROOM v11.49:" -ForegroundColor Yellow
$logFiles = Get-ChildItem -Path "." -Filter "monitor_v11.49_wroom_test_30min_*" | Sort-Object LastWriteTime -Descending
if ($logFiles) {
    $logFiles | Format-Table Name, Length, LastWriteTime -AutoSize
    
    # Afficher les dernières lignes du log le plus récent
    $latestLog = $logFiles[0]
    Write-Host "`n📊 Dernières lignes du log le plus récent ($($latestLog.Name)):" -ForegroundColor Cyan
    Get-Content $latestLog.FullName -Tail 10 | ForEach-Object { Write-Host $_ -ForegroundColor White }
} else {
    Write-Host "   Aucun fichier de log trouvé" -ForegroundColor Red
}

# Vérifier les ports série utilisés
Write-Host "`n🔌 Ports série disponibles:" -ForegroundColor Yellow
try {
    $ports = [System.IO.Ports.SerialPort]::getPortNames()
    if ($ports) {
        $ports | ForEach-Object { Write-Host "   COM$_" -ForegroundColor White }
    } else {
        Write-Host "   Aucun port série détecté" -ForegroundColor Red
    }
} catch {
    Write-Host "   Impossible de détecter les ports série" -ForegroundColor Red
}

Write-Host "`n⏰ Prochaine vérification dans 2 minutes..." -ForegroundColor Cyan
