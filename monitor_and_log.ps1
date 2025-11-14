# Script pour lancer le monitoring pendant 15 minutes avec PlatformIO

$duration = 900 # 15 minutes en secondes
$logFile = "esp32_logs_v11.116_$(Get-Date -Format 'yyyyMMdd_HHmmss').txt"

Write-Host "=== MONITORING ESP32 FFP5CS v11.116 ===" -ForegroundColor Green
Write-Host "Duration: 15 minutes"
Write-Host "Les logs seront sauvés dans: $logFile"
Write-Host "========================================"
Write-Host ""
Write-Host "Appuyez sur Ctrl+C après 15 minutes pour arrêter le monitoring"
Write-Host ""

# Lancer le monitoring avec PlatformIO et sauver dans un fichier
Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "cd /d `"C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs`" && pio device monitor --baud 115200 --filter time | tee $logFile" -NoNewWindow -Wait

Write-Host "`nMonitoring terminé. Logs sauvés dans: $logFile"
