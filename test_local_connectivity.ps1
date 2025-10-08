# Test de connectivite local pour ESP32
# Teste les IPs locales et communes

Write-Host "=== TEST CONNECTIVITE LOCAL ESP32 ===" -ForegroundColor Cyan
Write-Host ""

# IPs a tester
$testIPs = @(
    "127.0.0.1",
    "localhost", 
    "192.168.4.1",  # IP standard mode AP ESP32
    "192.168.1.1",
    "10.0.0.1",
    "172.16.0.1"
)

Write-Host "TEST DES IPs LOCALES:" -ForegroundColor Yellow
$foundIP = $null

foreach ($ip in $testIPs) {
    Write-Host "Test de $ip..." -NoNewline
    try {
        $response = Invoke-WebRequest -Uri "http://$ip/" -TimeoutSec 3 -ErrorAction SilentlyContinue
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
    
    # Test des endpoints
    Write-Host "TEST DES ENDPOINTS:" -ForegroundColor Yellow
    
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
            
            # Afficher un apercu de la reponse pour /json
            if ($endpoint -eq "/json" -and $response.StatusCode -eq 200) {
                Write-Host "  Apercu JSON: $($response.Content.Substring(0, [Math]::Min(100, $response.Content.Length)))..." -ForegroundColor Gray
            }
        } catch {
            Write-Host " ERREUR: $($_.Exception.Message)" -ForegroundColor Red
        }
    }
    
} else {
    Write-Host ""
    Write-Host "ESP32 non trouve sur les IPs locales" -ForegroundColor Red
    Write-Host ""
    Write-Host "SOLUTIONS:" -ForegroundColor Yellow
    Write-Host "1. Verifiez que l'ESP32 est alimente" -ForegroundColor White
    Write-Host "2. Verifiez la connexion USB" -ForegroundColor White
    Write-Host "3. L'ESP32 pourrait etre en mode AP" -ForegroundColor White
    Write-Host "4. Essayez de vous connecter au WiFi ESP32" -ForegroundColor White
}

Write-Host ""
Write-Host "=== FIN DU TEST ===" -ForegroundColor Cyan
