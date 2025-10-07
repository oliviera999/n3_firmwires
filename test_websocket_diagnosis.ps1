# Test de diagnostic WebSocket ESP32
Write-Host "🔧 Test de diagnostic WebSocket ESP32" -ForegroundColor Green
Write-Host ""

$ESP32_IP = "192.168.0.87"

Write-Host "📡 Test de connectivité de base..." -ForegroundColor Yellow
try {
    $ping = Test-NetConnection -ComputerName $ESP32_IP -Port 80 -InformationLevel Quiet
    if ($ping) {
        Write-Host "✅ Port 80 (HTTP) accessible" -ForegroundColor Green
    } else {
        Write-Host "❌ Port 80 (HTTP) inaccessible" -ForegroundColor Red
    }
} catch {
    Write-Host "❌ Erreur test port 80: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📡 Test port 81 (WebSocket dédié)..." -ForegroundColor Yellow
try {
    $ping81 = Test-NetConnection -ComputerName $ESP32_IP -Port 81 -InformationLevel Quiet
    if ($ping81) {
        Write-Host "✅ Port 81 (WebSocket) accessible" -ForegroundColor Green
    } else {
        Write-Host "❌ Port 81 (WebSocket) inaccessible" -ForegroundColor Red
    }
} catch {
    Write-Host "❌ Erreur test port 81: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "🌐 Test HTTP sur port 80..." -ForegroundColor Yellow
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP" -TimeoutSec 5 -UseBasicParsing
    Write-Host "✅ HTTP port 80 répond: $($response.StatusCode)" -ForegroundColor Green
} catch {
    Write-Host "❌ HTTP port 80 erreur: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "🌐 Test HTTP sur port 81..." -ForegroundColor Yellow
try {
    $response81 = Invoke-WebRequest -Uri "http://$ESP32_IP`:81" -TimeoutSec 5 -UseBasicParsing
    Write-Host "✅ HTTP port 81 répond: $($response81.StatusCode)" -ForegroundColor Green
} catch {
    Write-Host "❌ HTTP port 81 erreur: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📋 Résumé du diagnostic:" -ForegroundColor Cyan
Write-Host "• IP ESP32: $ESP32_IP" -ForegroundColor White
Write-Host "• Port 80 (HTTP): $(if ($ping) { 'Accessible' } else { 'Inaccessible' })" -ForegroundColor White
Write-Host "• Port 81 (WebSocket): $(if ($ping81) { 'Accessible' } else { 'Inaccessible' })" -ForegroundColor White

if (-not $ping81) {
    Write-Host ""
    Write-Host "🔍 Diagnostic du problème WebSocket:" -ForegroundColor Yellow
    Write-Host "• Le port 81 n'est pas accessible" -ForegroundColor White
    Write-Host "• Possible causes:" -ForegroundColor White
    Write-Host "  - Le serveur WebSocket n'est pas démarré sur l'ESP32" -ForegroundColor White
    Write-Host "  - Problème de configuration réseau" -ForegroundColor White
    Write-Host "  - Firewall bloquant le port 81" -ForegroundColor White
    Write-Host "  - Le firmware n'a pas été compilé avec le WebSocket activé" -ForegroundColor White
}
