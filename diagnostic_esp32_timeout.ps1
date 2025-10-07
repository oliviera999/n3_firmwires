# Diagnostic complet ESP32 - Problème de timeout
Write-Host "🔍 Diagnostic ESP32 - Problème de timeout" -ForegroundColor Green
Write-Host ""

$ESP32_IP = "192.168.0.86"

Write-Host "📋 État actuel identifié:" -ForegroundColor Cyan
Write-Host "• ESP32 accessible via ping ✅" -ForegroundColor Green
Write-Host "• Port 80 (HTTP) accessible ✅" -ForegroundColor Green
Write-Host "• Port 81 (WebSocket) inaccessible ❌" -ForegroundColor Red
Write-Host "• Serveur web retourne erreur 500 ❌" -ForegroundColor Red

Write-Host ""
Write-Host "🔧 Actions de diagnostic:" -ForegroundColor Yellow

# Test de connectivité de base
Write-Host "1. Test de connectivité de base..." -ForegroundColor White
try {
    $ping = Test-NetConnection -ComputerName $ESP32_IP -InformationLevel Quiet
    Write-Host "   Ping: $(if ($ping) { '✅ Réussi' } else { '❌ Échec' })" -ForegroundColor $(if ($ping) { 'Green' } else { 'Red' })
} catch {
    Write-Host "   Ping: ❌ Erreur - $($_.Exception.Message)" -ForegroundColor Red
}

# Test port 80
Write-Host "2. Test port 80 (HTTP)..." -ForegroundColor White
try {
    $http80 = Test-NetConnection -ComputerName $ESP32_IP -Port 80 -InformationLevel Quiet
    Write-Host "   Port 80: $(if ($http80) { '✅ Accessible' } else { '❌ Inaccessible' })" -ForegroundColor $(if ($http80) { 'Green' } else { 'Red' })
} catch {
    Write-Host "   Port 80: ❌ Erreur - $($_.Exception.Message)" -ForegroundColor Red
}

# Test port 81
Write-Host "3. Test port 81 (WebSocket)..." -ForegroundColor White
try {
    $ws81 = Test-NetConnection -ComputerName $ESP32_IP -Port 81 -InformationLevel Quiet
    Write-Host "   Port 81: $(if ($ws81) { '✅ Accessible' } else { '❌ Inaccessible' })" -ForegroundColor $(if ($ws81) { 'Green' } else { 'Red' })
} catch {
    Write-Host "   Port 81: ❌ Erreur - $($_.Exception.Message)" -ForegroundColor Red
}

# Test réponse HTTP
Write-Host "4. Test réponse HTTP..." -ForegroundColor White
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP" -Method Head -TimeoutSec 10 -ErrorAction SilentlyContinue
    Write-Host "   HTTP Status: $($response.StatusCode) $($response.StatusDescription)" -ForegroundColor $(if ($response.StatusCode -eq 200) { 'Green' } else { 'Red' })
} catch {
    Write-Host "   HTTP Status: ❌ Erreur - $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📋 Diagnostic du problème:" -ForegroundColor Cyan
Write-Host "• L'ESP32 est démarré et connecté au WiFi ✅" -ForegroundColor White
Write-Host "• Le serveur web HTTP fonctionne mais retourne une erreur 500 ❌" -ForegroundColor White
Write-Host "• Le serveur WebSocket n'est pas démarré ou a un problème ❌" -ForegroundColor White

Write-Host ""
Write-Host "🔧 Solutions possibles:" -ForegroundColor Yellow
Write-Host "1. Redémarrer l'ESP32 (appuyer sur le bouton RESET)" -ForegroundColor White
Write-Host "2. Vérifier les logs série pour identifier l'erreur 500" -ForegroundColor White
Write-Host "3. Vérifier que le firmware est à jour" -ForegroundColor White
Write-Host "4. Tester avec le moniteur série PlatformIO" -ForegroundColor White

Write-Host ""
Write-Host "📝 Commandes utiles:" -ForegroundColor Cyan
Write-Host "• Moniteur série: pio device monitor --port COM3 --baud 115200" -ForegroundColor White
Write-Host "• Compiler et flasher: pio run --target upload" -ForegroundColor White
Write-Host "• Test WebSocket: Ouvrir test_websocket.html dans le navigateur" -ForegroundColor White

Write-Host ""
Write-Host "🎯 Prochaines étapes recommandées:" -ForegroundColor Green
Write-Host "1. Redémarrer l'ESP32" -ForegroundColor White
Write-Host "2. Attendre 30 secondes et retester la connectivité" -ForegroundColor White
Write-Host "3. Si le problème persiste, consulter les logs série" -ForegroundColor White
