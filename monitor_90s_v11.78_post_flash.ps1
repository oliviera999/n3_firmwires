# Script de monitoring 90 secondes post-flash v11.78
# Capture des logs série pour analyse de stabilité
# Fix GPIO virtuels - application immédiate des changements depuis serveur distant

$Port = "COM6"  # Ajustez selon votre configuration
$BaudRate = 115200
$Duration = 90  # secondes
$LogFile = "monitor_90s_v11.78_post_flash_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "=== MONITORING POST-FLASH v11.78 - 90 SECONDES ===" -ForegroundColor Green
Write-Host "Fix GPIO virtuels - application immédiate des changements depuis serveur distant" -ForegroundColor Yellow
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Baud: $BaudRate" -ForegroundColor Yellow
Write-Host "Durée: $Duration secondes" -ForegroundColor Yellow
Write-Host "Log: $LogFile" -ForegroundColor Yellow
Write-Host "Démarrage: $(Get-Date)" -ForegroundColor Cyan
Write-Host ""

# Créer le fichier de log avec en-tête
$LogHeader = @"
=== MONITORING POST-FLASH v11.78 - $(Get-Date) ===
Version: 11.78 - Fix GPIO virtuels - application immédiate des changements depuis serveur distant
Port: $Port, Baud: $BaudRate, Durée: $Duration secondes

=== PRIORITÉS D'ANALYSE ===
🔴 Priorité 1: Guru Meditation, Panic, Brownout, Core dump
🟡 Priorité 2: Warnings watchdog, timeouts WiFi/WebSocket  
🟢 Priorité 3: Utilisation mémoire (heap/stack)
⚪ Secondaire: Valeurs des capteurs (sauf indication contraire)

"@

$LogHeader | Out-File -FilePath $LogFile -Encoding UTF8

# Fonction pour capturer les logs
function Start-LogCapture {
    param($Port, $BaudRate, $LogFile, $Duration)
    
    try {
        Write-Host "Démarrage du monitoring série..." -ForegroundColor Cyan
        
        # Utiliser pio device monitor pour capturer les logs
        $process = Start-Process -FilePath "pio" -ArgumentList "device", "monitor", "--port", $Port, "--baud", $BaudRate, "--filter", "time" -RedirectStandardOutput $LogFile -RedirectStandardError "error_$LogFile" -PassThru -NoNewWindow
        
        Write-Host "Monitoring démarré (PID: $($process.Id))" -ForegroundColor Green
        
        # Attendre la durée spécifiée avec affichage du countdown
        for ($i = $Duration; $i -gt 0; $i--) {
            Write-Progress -Activity "Monitoring ESP32 v11.78" -Status "Temps restant: $i secondes" -PercentComplete (($Duration - $i) / $Duration * 100)
            Start-Sleep -Seconds 1
        }
        
        Write-Progress -Activity "Monitoring ESP32 v11.78" -Status "Terminé" -Completed
        
        # Arrêter le processus
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        
        Write-Host "Monitoring terminé après $Duration secondes" -ForegroundColor Green
        
        # Ajouter les infos de fin de log
        "`n=== FIN DU MONITORING - $(Get-Date) ===" | Out-File -FilePath $LogFile -Append -Encoding UTF8
        
    } catch {
        Write-Host "Erreur lors du monitoring: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Démarrer la capture
Start-LogCapture -Port $Port -BaudRate $BaudRate -LogFile $LogFile -Duration $Duration

Write-Host ""
Write-Host "=== ANALYSE RECOMMANDÉE ===" -ForegroundColor Green
Write-Host "Fichier de log: $LogFile" -ForegroundColor Yellow
Write-Host ""
Write-Host "Points d'attention à vérifier dans les logs:" -ForegroundColor Cyan
Write-Host "🔴 Critique: Rechercher 'Guru Meditation', 'Panic', 'Brownout', 'Core dump'" -ForegroundColor Red
Write-Host "🟡 Important: Vérifier les warnings watchdog, timeouts WiFi/WebSocket" -ForegroundColor Yellow  
Write-Host "🟢 Normal: Contrôler l'utilisation mémoire (heap/stack)" -ForegroundColor Green
Write-Host "⚪ Secondaire: Analyser les valeurs des capteurs si nécessaire" -ForegroundColor Gray
Write-Host ""
Write-Host "Si erreurs critiques détectées → corriger avant de continuer" -ForegroundColor Red
Write-Host "Monitoring terminé avec succès !" -ForegroundColor Green




