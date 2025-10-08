# Script pour forcer le redemarrage de l'ESP32
# Utilise esptool pour envoyer un signal de reset

Write-Host "=== REDEMARRAGE FORCE ESP32 ===" -ForegroundColor Cyan
Write-Host ""

# 1. Verifier que le port est libre
Write-Host "1. VERIFICATION DU PORT COM6:" -ForegroundColor Yellow
try {
    $port = [System.IO.Ports.SerialPort]::new("COM6", 115200)
    $port.Open()
    $port.Close()
    $port.Dispose()
    Write-Host "Port COM6 disponible" -ForegroundColor Green
} catch {
    Write-Host "Port COM6 occupe ou inaccessible" -ForegroundColor Red
    Write-Host "Erreur: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# 2. Essayer de redemarrer via esptool
Write-Host "2. REDEMARRAGE VIA ESPTOOL:" -ForegroundColor Yellow
try {
    $esptoolPath = "C:\Users\olivi\.platformio\packages\tool-esptoolpy\esptool.py"
    if (Test-Path $esptoolPath) {
        Write-Host "Utilisation d'esptool pour le reset..." -ForegroundColor White
        python $esptoolPath --port COM6 --baud 115200 run
    } else {
        Write-Host "esptool.py non trouve" -ForegroundColor Red
    }
} catch {
    Write-Host "Erreur lors du reset: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# 3. Essayer de demarrer le monitoring
Write-Host "3. DEMARRAGE DU MONITORING:" -ForegroundColor Yellow
Write-Host "Demarrage du monitoring serie..." -ForegroundColor White
Write-Host "Appuyez sur Ctrl+C pour arreter" -ForegroundColor Yellow
Write-Host ""

try {
    pio device monitor -e wroom-test --baud 115200 --filter colorize
} catch {
    Write-Host "Erreur lors du demarrage du monitoring: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== FIN DU SCRIPT ===" -ForegroundColor Cyan
