# Script de test de connectivité WebSocket pour FFP3
# Teste les ports 80 et 81 pour la connexion WebSocket

param(
    [string]$ESP32_IP = "192.168.0.87"
)

Write-Host "🔍 Test de connectivité WebSocket FFP3" -ForegroundColor Cyan
Write-Host "ESP32 IP: $ESP32_IP" -ForegroundColor Yellow
Write-Host ""

# Test 1: Vérifier que l'ESP32 répond sur le port 80 (HTTP)
Write-Host "1️⃣ Test HTTP (port 80)..." -ForegroundColor Green
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP/" -TimeoutSec 5 -UseBasicParsing
    if ($response.StatusCode -eq 200) {
        Write-Host "✅ HTTP OK - Serveur web accessible" -ForegroundColor Green
    } else {
        Write-Host "⚠️ HTTP Status: $($response.StatusCode)" -ForegroundColor Yellow
    }
} catch {
    Write-Host "❌ HTTP KO - $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# Test 2: Vérifier l'endpoint JSON
Write-Host "2️⃣ Test endpoint JSON..." -ForegroundColor Green
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP/json" -TimeoutSec 5 -UseBasicParsing
    if ($response.StatusCode -eq 200) {
        $json = $response.Content | ConvertFrom-Json
        Write-Host "✅ JSON OK - Données disponibles" -ForegroundColor Green
        Write-Host "   Température eau: $($json.tempWater)°C" -ForegroundColor Cyan
        Write-Host "   Température air: $($json.tempAir)°C" -ForegroundColor Cyan
        Write-Host "   Humidité: $($json.humidity)%" -ForegroundColor Cyan
    } else {
        Write-Host "⚠️ JSON Status: $($response.StatusCode)" -ForegroundColor Yellow
    }
} catch {
    Write-Host "❌ JSON KO - $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# Test 3: Vérifier la version du firmware
Write-Host "3️⃣ Test version firmware..." -ForegroundColor Green
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP/version" -TimeoutSec 5 -UseBasicParsing
    if ($response.StatusCode -eq 200) {
        $version = $response.Content | ConvertFrom-Json
        Write-Host "✅ Version OK - $($version.version)" -ForegroundColor Green
    } else {
        Write-Host "⚠️ Version Status: $($response.StatusCode)" -ForegroundColor Yellow
    }
} catch {
    Write-Host "❌ Version KO - $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# Test 4: Vérifier les ports WebSocket (simulation)
Write-Host "4️⃣ Test ports WebSocket..." -ForegroundColor Green

# Test port 80
Write-Host "   Port 80 (HTTP standard):" -ForegroundColor Cyan
try {
    $tcpClient = New-Object System.Net.Sockets.TcpClient
    $tcpClient.ConnectAsync($ESP32_IP, 80).Wait(2000)
    if ($tcpClient.Connected) {
        Write-Host "   ✅ Port 80 ouvert" -ForegroundColor Green
        $tcpClient.Close()
    } else {
        Write-Host "   ❌ Port 80 fermé" -ForegroundColor Red
    }
} catch {
    Write-Host "   ❌ Port 80 inaccessible: $($_.Exception.Message)" -ForegroundColor Red
}

# Test port 81
Write-Host "   Port 81 (WebSocket dédié):" -ForegroundColor Cyan
try {
    $tcpClient = New-Object System.Net.Sockets.TcpClient
    $tcpClient.ConnectAsync($ESP32_IP, 81).Wait(2000)
    if ($tcpClient.Connected) {
        Write-Host "   ✅ Port 81 ouvert" -ForegroundColor Green
        $tcpClient.Close()
    } else {
        Write-Host "   ❌ Port 81 fermé" -ForegroundColor Red
    }
} catch {
    Write-Host "   ❌ Port 81 inaccessible: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""

# Test 5: Diagnostic réseau
Write-Host "5️⃣ Diagnostic réseau..." -ForegroundColor Green
try {
    $ping = Test-Connection -ComputerName $ESP32_IP -Count 1 -Quiet
    if ($ping) {
        Write-Host "✅ Ping OK - ESP32 accessible" -ForegroundColor Green
    } else {
        Write-Host "❌ Ping KO - ESP32 inaccessible" -ForegroundColor Red
    }
} catch {
    Write-Host "❌ Ping erreur: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "📋 Résumé des corrections apportées:" -ForegroundColor Cyan
Write-Host "• Frontend modifié pour essayer les ports 80 et 81" -ForegroundColor White
Write-Host "• Gestion d'erreur améliorée avec fallback automatique" -ForegroundColor White
Write-Host "• Messages de debug plus informatifs" -ForegroundColor White
Write-Host "• Mode polling activé si WebSocket échoue" -ForegroundColor White
Write-Host ""
Write-Host "🔄 Prochaines étapes:" -ForegroundColor Yellow
Write-Host "1. Recompiler et flasher le firmware" -ForegroundColor White
Write-Host "2. Ouvrir http://$ESP32_IP dans le navigateur" -ForegroundColor White
Write-Host "3. Vérifier la console du navigateur (F12)" -ForegroundColor White
Write-Host "4. Les données devraient s'afficher même sans WebSocket" -ForegroundColor White
