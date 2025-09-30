# Script de diagnostic pour wroom-test
# Teste la connectivite et identifie les problemes

Write-Host "=== DIAGNOSTIC WROOM-TEST ===" -ForegroundColor Cyan
Write-Host ""

# 1. Verifier les ports serie
Write-Host "1. PORTS SERIE DISPONIBLES:" -ForegroundColor Yellow
pio device list
Write-Host ""

# 2. Tester la connectivite reseau
Write-Host "2. TEST DE CONNECTIVITE:" -ForegroundColor Yellow
Write-Host "Recherche de l'ESP32 sur le reseau..."

# Scan des IPs communes
$commonIPs = @("192.168.1.100", "192.168.1.101", "192.168.1.102", "192.168.1.103", "192.168.1.104", "192.168.1.105")
$foundIP = $null

foreach ($ip in $commonIPs) {
    Write-Host "Test de $ip..." -NoNewline
    try {
        $response = Invoke-WebRequest -Uri "http://$ip/" -TimeoutSec 2 -ErrorAction SilentlyContinue
        if ($response.StatusCode -eq 200) {
            Write-Host " TROUVE!" -ForegroundColor Green
            $foundIP = $ip
            break
        }
    } catch {
        Write-Host " ECHEC"
    }
}

if ($foundIP) {
    Write-Host ""
    Write-Host "ESP32 trouve a l'adresse: $foundIP" -ForegroundColor Green
    Write-Host ""
    
    # 3. Tester les endpoints
    Write-Host "3. TEST DES ENDPOINTS:" -ForegroundColor Yellow
    
    $endpoints = @(
        "/",
        "/json",
        "/dbvars",
        "/action?cmd=status"
    )
    
    foreach ($endpoint in $endpoints) {
        Write-Host "Test de $endpoint..." -NoNewline
        try {
            $response = Invoke-WebRequest -Uri "http://$foundIP$endpoint" -TimeoutSec 5
            Write-Host " OK (Status: $($response.StatusCode))" -ForegroundColor Green
        } catch {
            Write-Host " ERREUR: $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    
    Write-Host ""
    Write-Host "4. INFORMATIONS SYSTEME:" -ForegroundColor Yellow
    try {
        $jsonResponse = Invoke-WebRequest -Uri "http://$foundIP/json" -TimeoutSec 5
        $jsonData = $jsonResponse.Content | ConvertFrom-Json
        Write-Host "Uptime: $($jsonData.uptime)" -ForegroundColor Green
        Write-Host "Free Heap: $($jsonData.freeHeap)" -ForegroundColor Green
        Write-Host "WiFi Status: $($jsonData.wifiStatus)" -ForegroundColor Green
    } catch {
        Write-Host "Impossible de recuperer les donnees JSON" -ForegroundColor Red
    }
    
} else {
    Write-Host ""
    Write-Host "ESP32 non trouve sur le reseau" -ForegroundColor Red
    Write-Host ""
    Write-Host "4. DIAGNOSTIC AVANCE:" -ForegroundColor Yellow
    Write-Host "- Verifiez que l'ESP32 est alimente" -ForegroundColor White
    Write-Host "- Verifiez la connexion USB" -ForegroundColor White
    Write-Host "- Verifiez les logs serie pour les erreurs" -ForegroundColor White
    Write-Host "- L'ESP32 pourrait etre en mode AP" -ForegroundColor White
}

Write-Host ""
Write-Host "=== FIN DU DIAGNOSTIC ===" -ForegroundColor Cyan
