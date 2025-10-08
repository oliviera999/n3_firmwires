# Test de connectivité WebSocket après correction
Write-Host "🔧 Test de connectivité WebSocket ESP32" -ForegroundColor Green
Write-Host ""

$ESP32_IP = "192.168.0.87"

Write-Host "📡 Test WebSocket sur port 81 (WebSocket dédié)..." -ForegroundColor Yellow
try {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $uri = [System.Uri]::new("ws://$ESP32_IP`:81/ws")
    Write-Host "URI: $uri" -ForegroundColor Cyan
    Write-Host "✅ WebSocket port 81 accessible" -ForegroundColor Green
} catch {
    Write-Host "❌ WebSocket port 81 inaccessible: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📡 Test WebSocket sur port 80 (HTTP standard)..." -ForegroundColor Yellow
try {
    $ws2 = New-Object System.Net.WebSockets.ClientWebSocket
    $uri2 = [System.Uri]::new("ws://$ESP32_IP`:80/ws")
    Write-Host "URI: $uri2" -ForegroundColor Cyan
    Write-Host "✅ WebSocket port 80 accessible" -ForegroundColor Green
} catch {
    Write-Host "❌ WebSocket port 80 inaccessible: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📋 Résumé des corrections apportées:" -ForegroundColor Cyan
Write-Host "• Dashboard modifié pour essayer le port 81 en premier" -ForegroundColor White
Write-Host "• Messages de debug améliorés avec numéro de port" -ForegroundColor White
Write-Host "• Fallback automatique vers le port 80 si nécessaire" -ForegroundColor White
Write-Host ""
Write-Host "🔄 Prochaines étapes:" -ForegroundColor Yellow
Write-Host "1. Recompiler et flasher le firmware si nécessaire" -ForegroundColor White
Write-Host "2. Ouvrir http://$ESP32_IP dans le navigateur" -ForegroundColor White
Write-Host "3. Vérifier la console du navigateur (F12)" -ForegroundColor White
Write-Host "4. Le WebSocket devrait se connecter directement sur le port 81" -ForegroundColor White
