# Test de vérification WebSocket après correction
Write-Host "🔧 Test de vérification WebSocket ESP32" -ForegroundColor Green
Write-Host ""

$ESP32_IP = "192.168.0.86"

Write-Host "📋 Corrections apportées:" -ForegroundColor Cyan
Write-Host "• Timeout de connexion WebSocket (5 secondes)" -ForegroundColor White
Write-Host "• Gestion d'erreur améliorée avec readyState" -ForegroundColor White
Write-Host "• Messages de debug détaillés" -ForegroundColor White
Write-Host "• Fermeture propre des connexions" -ForegroundColor White
Write-Host "• Délai augmenté entre tentatives (2 secondes)" -ForegroundColor White

Write-Host ""
Write-Host "🌐 Test de connectivité..." -ForegroundColor Yellow
try {
    $ping80 = Test-NetConnection -ComputerName $ESP32_IP -Port 80 -InformationLevel Quiet
    $ping81 = Test-NetConnection -ComputerName $ESP32_IP -Port 81 -InformationLevel Quiet
    
    Write-Host "Port 80 (HTTP): $(if ($ping80) { '✅ Accessible' } else { '❌ Inaccessible' })" -ForegroundColor $(if ($ping80) { 'Green' } else { 'Red' })
    Write-Host "Port 81 (WebSocket): $(if ($ping81) { '✅ Accessible' } else { '❌ Inaccessible' })" -ForegroundColor $(if ($ping81) { 'Green' } else { 'Red' })
} catch {
    Write-Host "❌ Erreur test connectivité: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📋 Instructions de test:" -ForegroundColor Cyan
Write-Host "1. Ouvrir http://$ESP32_IP dans le navigateur" -ForegroundColor White
Write-Host "2. Ouvrir la console développeur (F12)" -ForegroundColor White
Write-Host "3. Recharger la page (Ctrl+F5)" -ForegroundColor White
Write-Host "4. Observer les messages de console:" -ForegroundColor White
Write-Host "   - 'Tentative de connexion WebSocket sur ws://$ESP32_IP`:81/ws'" -ForegroundColor White
Write-Host "   - 'WebSocket connecté sur le port 81' (si succès)" -ForegroundColor White
Write-Host "   - 'Message WebSocket brut reçu:' (si données reçues)" -ForegroundColor White

Write-Host ""
Write-Host "🔍 Si le problème persiste:" -ForegroundColor Yellow
Write-Host "• Vérifier les logs ESP32 via le moniteur série" -ForegroundColor White
Write-Host "• Tester avec le fichier test_websocket.html" -ForegroundColor White
Write-Host "• Vérifier que le firmware est à jour" -ForegroundColor White
