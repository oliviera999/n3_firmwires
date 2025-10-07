# Script de test simple pour réveil ESP32
# Utilisation: .\test_wakeup_simple.ps1 -ESP_IP "192.168.1.100"

param(
    [string]$ESP_IP = "192.168.1.100"
)

Write-Host "🔔 Test de réveil ESP32 - $ESP_IP" -ForegroundColor Yellow

# Test 1: Ping simple
Write-Host "`n1️⃣ Test ping..." -ForegroundColor Cyan
try {
    $pingResult = Test-Connection -ComputerName $ESP_IP -Count 1 -Quiet
    if ($pingResult) {
        Write-Host "✅ Ping réussi" -ForegroundColor Green
    } else {
        Write-Host "❌ Ping échoué" -ForegroundColor Red
    }
} catch {
    Write-Host "❌ Erreur ping: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 2: Endpoint ping
Write-Host "`n2️⃣ Test endpoint /ping..." -ForegroundColor Cyan
try {
    $response = Invoke-RestMethod -Uri "http://$ESP_IP/ping" -Method GET -TimeoutSec 10
    Write-Host "✅ Ping HTTP réussi" -ForegroundColor Green
    Write-Host "📊 Réponse: $($response | ConvertTo-Json)" -ForegroundColor Gray
} catch {
    Write-Host "❌ Erreur ping HTTP: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 3: Endpoint wakeup
Write-Host "`n3️⃣ Test endpoint /wakeup..." -ForegroundColor Cyan
try {
    $response = Invoke-RestMethod -Uri "http://$ESP_IP/wakeup" -Method GET -TimeoutSec 10
    Write-Host "✅ Wakeup HTTP réussi" -ForegroundColor Green
    Write-Host "📊 Réponse: $($response | ConvertTo-Json)" -ForegroundColor Gray
} catch {
    Write-Host "❌ Erreur wakeup HTTP: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 4: API wakeup avec action
Write-Host "`n4️⃣ Test API /api/wakeup..." -ForegroundColor Cyan
try {
    $body = @{
        action = "status"
        source = "powershell_test"
        timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssZ")
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "http://$ESP_IP/api/wakeup" -Method POST -Body $body -ContentType "application/json" -TimeoutSec 10
    Write-Host "✅ API wakeup réussi" -ForegroundColor Green
    Write-Host "📊 Réponse: $($response | ConvertTo-Json)" -ForegroundColor Gray
} catch {
    Write-Host "❌ Erreur API wakeup: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n🎉 Tests terminés !" -ForegroundColor Yellow
Write-Host "💡 Si tous les tests réussissent, le système de réveil WiFi fonctionne correctement." -ForegroundColor Gray
