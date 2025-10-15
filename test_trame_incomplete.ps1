# Test : Que se passe-t-il avec une trame incomplète ?
# Simule un POST avec données manquantes

Write-Host "=== Test Trame Incomplète ===" -ForegroundColor Cyan
Write-Host ""

# Test 1: Trame minimale (seulement sensor + version)
Write-Host "Test 1: Trame MINIMALE (2 champs sur 31)..." -ForegroundColor Yellow
$response1 = curl.exe -X POST "http://iot.olution.info/ffp3/post-data-test" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  -d "api_key=fdGTMoptd5CD2ert3" `
  -d "sensor=test-incomplete" `
  -d "version=11.35" `
  -w "\nHTTP: %{http_code}\n" `
  -s

Write-Host "Réponse: $response1" -ForegroundColor $(if($response1 -like "*success*") {"Green"} else {"Red"})
Write-Host ""

# Test 2: Trame partielle (sans températures)
Write-Host "Test 2: Trame PARTIELLE (sans températures)..." -ForegroundColor Yellow
$response2 = curl.exe -X POST "http://iot.olution.info/ffp3/post-data-test" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  -d "api_key=fdGTMoptd5CD2ert3" `
  -d "sensor=test-partial" `
  -d "version=11.35" `
  -d "EauAquarium=200" `
  -d "etatHeat=1" `
  -d "etatUV=1" `
  -w "\nHTTP: %{http_code}\n" `
  -s

Write-Host "Réponse: $response2" -ForegroundColor $(if($response2 -like "*success*") {"Green"} else {"Red"})
Write-Host ""

# Test 3: Trame sans GPIO critiques
Write-Host "Test 3: Trame SANS GPIO critiques (chauffage, pompes)..." -ForegroundColor Yellow
$response3 = curl.exe -X POST "http://iot.olution.info/ffp3/post-data-test" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  -d "api_key=fdGTMoptd5CD2ert3" `
  -d "sensor=test-no-gpio" `
  -d "version=11.35" `
  -d "TempAir=25.0" `
  -d "Humidite=60.0" `
  -d "TempEau=28.0" `
  -w "\nHTTP: %{http_code}\n" `
  -s

Write-Host "Réponse: $response3" -ForegroundColor $(if($response3 -like "*success*") {"Green"} else {"Red"})
Write-Host ""

Write-Host "=== Analyse Résultats ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Pour voir les données insérées, exécute sur le serveur:" -ForegroundColor Yellow
Write-Host "  SELECT * FROM ffp3Data2 WHERE sensor LIKE 'test-%' ORDER BY id DESC LIMIT 3;" -ForegroundColor Gray
Write-Host ""
Write-Host "Pour voir les GPIO mis à jour:" -ForegroundColor Yellow
Write-Host "  SELECT gpio, name, state, requestTime FROM ffp3Outputs2 WHERE gpio IN (2,15,16,18) ORDER BY gpio;" -ForegroundColor Gray

