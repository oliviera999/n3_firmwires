# DIAGNOSTIC COMPLET - ESP32 WROOM-TEST
# Analyse du probleme "page hors-ligne"

Write-Host "=== DIAGNOSTIC COMPLET ESP32 WROOM-TEST ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "PROBLEME IDENTIFIE:" -ForegroundColor Red
Write-Host "- Page d'index reste hors-ligne" -ForegroundColor White
Write-Host "- Aucune donnee ne s'affiche" -ForegroundColor White
Write-Host "- Port COM6 inaccessible" -ForegroundColor White
Write-Host ""

Write-Host "CAUSES POSSIBLES:" -ForegroundColor Yellow
Write-Host "1. ESP32 ne demarre pas correctement" -ForegroundColor White
Write-Host "2. Probleme de configuration WiFi" -ForegroundColor White
Write-Host "3. Firmware corrompu ou incomplet" -ForegroundColor White
Write-Host "4. Probleme de partition LittleFS" -ForegroundColor White
Write-Host "5. Conflit de port serie" -ForegroundColor White
Write-Host ""

Write-Host "SOLUTIONS A ESSAYER:" -ForegroundColor Green
Write-Host ""

Write-Host "SOLUTION 1: Redemarrage physique" -ForegroundColor Yellow
Write-Host "- Debrancher et rebrancher l'USB" -ForegroundColor White
Write-Host "- Appuyer sur le bouton RESET de l'ESP32" -ForegroundColor White
Write-Host "- Attendre 10 secondes puis tester" -ForegroundColor White
Write-Host ""

Write-Host "SOLUTION 2: Reflash complet" -ForegroundColor Yellow
Write-Host "- pio run -e wroom-test -t erase" -ForegroundColor White
Write-Host "- pio run -e wroom-test -t upload" -ForegroundColor White
Write-Host "- pio run -e wroom-test -t uploadfs" -ForegroundColor White
Write-Host ""

Write-Host "SOLUTION 3: Verification du firmware" -ForegroundColor Yellow
Write-Host "- Verifier que le firmware compile sans erreur" -ForegroundColor White
Write-Host "- Verifier la taille du firmware (< 2MB)" -ForegroundColor White
Write-Host "- Verifier les partitions dans platformio.ini" -ForegroundColor White
Write-Host ""

Write-Host "SOLUTION 4: Test en mode AP" -ForegroundColor Yellow
Write-Host "- L'ESP32 devrait creer un point d'acces" -ForegroundColor White
Write-Host "- SSID: ESP_E3592D ou similaire" -ForegroundColor White
Write-Host "- IP: 192.168.4.1" -ForegroundColor White
Write-Host ""

Write-Host "SOLUTION 5: Diagnostic avance" -ForegroundColor Yellow
Write-Host "- Utiliser un autre port USB" -ForegroundColor White
Write-Host "- Tester avec un autre cable USB" -ForegroundColor White
Write-Host "- Verifier les drivers CH340" -ForegroundColor White
Write-Host ""

Write-Host "PROCHAINES ETAPES RECOMMANDEES:" -ForegroundColor Cyan
Write-Host "1. Redemarrage physique de l'ESP32" -ForegroundColor White
Write-Host "2. Attendre 30 secondes" -ForegroundColor White
Write-Host "3. Relancer le diagnostic reseau" -ForegroundColor White
Write-Host "4. Si echec, proceder au reflash complet" -ForegroundColor White
Write-Host ""

Write-Host "COMMANDES DE TEST:" -ForegroundColor Magenta
Write-Host "powershell -ExecutionPolicy Bypass -File diagnose_wroom_test.ps1" -ForegroundColor White
Write-Host "pio device monitor -e wroom-test --baud 115200" -ForegroundColor White
Write-Host ""

Write-Host "=== FIN DU DIAGNOSTIC ===" -ForegroundColor Cyan
