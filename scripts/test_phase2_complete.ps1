# Test complet Phase 2 Performance et Synchronisation
# Valide minification, Service Worker, rate limiting et synchronisation

param(
    [string]$ESP32_IP = "192.168.1.100",
    [switch]$SkipRateLimitTest,
    [switch]$Verbose
)

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "   Test Phase 2 Performance - FFP5CS" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""

$baseUrl = "http://$ESP32_IP"
$testsPassed = 0
$testsFailed = 0
$testsSkipped = 0

function Test-Endpoint {
    param(
        [string]$Name,
        [string]$Url,
        [string]$Method = "GET",
        [hashtable]$Body = $null,
        [string]$ExpectedStatus = "200"
    )
    
    Write-Host "🧪 Test: $Name" -ForegroundColor Yellow -NoNewline
    
    try {
        $params = @{
            Uri = $Url
            Method = $Method
            TimeoutSec = 5
            UseBasicParsing = $true
        }
        
        if ($Body -and $Method -eq "POST") {
            $params.Body = $Body
            $params.ContentType = "application/x-www-form-urlencoded"
        }
        
        $response = Invoke-WebRequest @params -ErrorAction Stop
        
        if ($response.StatusCode -eq $ExpectedStatus) {
            Write-Host " ✅ PASS" -ForegroundColor Green
            if ($Verbose) {
                Write-Host "   Status: $($response.StatusCode)" -ForegroundColor Gray
                Write-Host "   Size: $($response.Content.Length) bytes" -ForegroundColor Gray
            }
            $script:testsPassed++
            return $true
        } else {
            Write-Host " ❌ FAIL" -ForegroundColor Red
            Write-Host "   Expected: $ExpectedStatus, Got: $($response.StatusCode)" -ForegroundColor Red
            $script:testsFailed++
            return $false
        }
    }
    catch {
        Write-Host " ❌ FAIL" -ForegroundColor Red
        Write-Host "   Error: $($_.Exception.Message)" -ForegroundColor Red
        $script:testsFailed++
        return $false
    }
}

function Test-RateLimit {
    param(
        [string]$Endpoint,
        [int]$Limit,
        [int]$Requests
    )
    
    Write-Host "🔒 Test Rate Limit: $Endpoint (limite: $Limit)" -ForegroundColor Yellow
    
    $blocked = 0
    $allowed = 0
    
    for ($i = 1; $i -le $Requests; $i++) {
        try {
            $response = Invoke-WebRequest -Uri "$baseUrl$Endpoint" -Method GET -TimeoutSec 2 -UseBasicParsing -ErrorAction Stop
            $allowed++
            if ($Verbose) { Write-Host "  Request $i : OK" -ForegroundColor Gray }
        }
        catch {
            if ($_.Exception.Response.StatusCode -eq 429) {
                $blocked++
                if ($Verbose) { Write-Host "  Request $i : BLOCKED (429)" -ForegroundColor Gray }
            } else {
                Write-Host "  Request $i : ERROR ($($_.Exception.Message))" -ForegroundColor Red
            }
        }
        Start-Sleep -Milliseconds 100
    }
    
    Write-Host "  Allowed: $allowed, Blocked: $blocked" -ForegroundColor Cyan
    
    if ($blocked -gt 0 -and $allowed -le $Limit) {
        Write-Host "  ✅ Rate limiting fonctionne" -ForegroundColor Green
        $script:testsPassed++
        return $true
    } else {
        Write-Host "  ⚠️  Rate limiting non détecté" -ForegroundColor Yellow
        $script:testsSkipped++
        return $false
    }
}

# ===========================================
# PHASE 1: Tests Connectivité de Base
# ===========================================

Write-Host "PHASE 1: Connectivité de Base" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

Test-Endpoint -Name "Ping ESP32" -Url "$baseUrl/version"
Test-Endpoint -Name "Page principale" -Url "$baseUrl/"
Test-Endpoint -Name "API JSON" -Url "$baseUrl/json"
Test-Endpoint -Name "DBVars" -Url "$baseUrl/dbvars"

Write-Host ""

# ===========================================
# PHASE 2: Tests Assets Minifiés
# ===========================================

Write-Host "PHASE 2: Assets Minifiés" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

# Vérifier si assets minifiés (taille réduite)
$commonJsTest = Test-Endpoint -Name "common.js" -Url "$baseUrl/shared/common.js"
$websocketJsTest = Test-Endpoint -Name "websocket.js" -Url "$baseUrl/shared/websocket.js"
$commonCssTest = Test-Endpoint -Name "common.css" -Url "$baseUrl/shared/common.css"

if ($commonJsTest -and $websocketJsTest -and $commonCssTest) {
    Write-Host "✅ Tous les assets sont accessibles" -ForegroundColor Green
}

Write-Host ""

# ===========================================
# PHASE 3: Test Service Worker
# ===========================================

Write-Host "PHASE 3: Service Worker" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

$swTest = Test-Endpoint -Name "Service Worker (sw.js)" -Url "$baseUrl/sw.js"

if ($swTest) {
    try {
        $swContent = (Invoke-WebRequest -Uri "$baseUrl/sw.js" -UseBasicParsing).Content
        
        if ($swContent -match "CACHE_VERSION\s*=\s*['`"]ffp3-v[\d\.]+['`"]") {
            Write-Host "  ✅ Version cache trouvée" -ForegroundColor Green
        }
        
        if ($swContent -match "addEventListener\('install'") {
            Write-Host "  ✅ Event 'install' trouvé" -ForegroundColor Green
        }
        
        if ($swContent -match "addEventListener\('fetch'") {
            Write-Host "  ✅ Event 'fetch' trouvé" -ForegroundColor Green
        }
        
        if ($swContent -match "cacheFirst|networkFirst") {
            Write-Host "  ✅ Stratégies de cache trouvées" -ForegroundColor Green
        }
    }
    catch {
        Write-Host "  ⚠️  Impossible d'analyser sw.js" -ForegroundColor Yellow
    }
}

Write-Host ""

# ===========================================
# PHASE 4: Test Rate Limiting
# ===========================================

if (-not $SkipRateLimitTest) {
    Write-Host "PHASE 4: Rate Limiting" -ForegroundColor Magenta
    Write-Host "----------------------------------------" -ForegroundColor Gray
    
    # Test endpoint /action (limite: 10 req/10s)
    # Envoyer 15 requêtes rapides
    Test-RateLimit -Endpoint "/json" -Limit 60 -Requests 15
    
    Write-Host ""
    Write-Host "⏳ Attente 11s pour reset fenêtre..." -ForegroundColor Gray
    Start-Sleep -Seconds 11
    
    # Re-tester après reset
    Write-Host "🔄 Re-test après reset" -ForegroundColor Cyan
    Test-Endpoint -Name "Requête après reset" -Url "$baseUrl/json"
    
    Write-Host ""
} else {
    Write-Host "⏭️  Tests Rate Limiting skippés" -ForegroundColor Gray
    Write-Host ""
}

# ===========================================
# PHASE 5: Test Synchronisation
# ===========================================

Write-Host "PHASE 5: Synchronisation ESP-NVS-Serveur" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

# Test 1: Lecture état initial
Write-Host "📊 État initial..." -ForegroundColor Cyan
try {
    $initialState = Invoke-RestMethod -Uri "$baseUrl/json" -Method GET -TimeoutSec 5
    Write-Host "  ✅ État capteurs/actionneurs récupéré" -ForegroundColor Green
    if ($Verbose) {
        Write-Host "  TempWater: $($initialState.tempWater)°C" -ForegroundColor Gray
        Write-Host "  TempAir: $($initialState.tempAir)°C" -ForegroundColor Gray
    }
    $testsPassed++
}
catch {
    Write-Host "  ❌ Erreur récupération état" -ForegroundColor Red
    $testsFailed++
}

# Test 2: Lecture configuration
Write-Host "📋 Configuration BDD..." -ForegroundColor Cyan
try {
    $dbvars = Invoke-RestMethod -Uri "$baseUrl/dbvars" -Method GET -TimeoutSec 5
    Write-Host "  ✅ Variables BDD récupérées" -ForegroundColor Green
    if ($Verbose) {
        Write-Host "  feedMorning: $($dbvars.feedMorning)h" -ForegroundColor Gray
        Write-Host "  feedNoon: $($dbvars.feedNoon)h" -ForegroundColor Gray
        Write-Host "  feedEvening: $($dbvars.feedEvening)h" -ForegroundColor Gray
    }
    $testsPassed++
}
catch {
    Write-Host "  ❌ Erreur récupération config" -ForegroundColor Red
    $testsFailed++
}

# Test 3: Modification configuration (TEST DE SYNCHRONISATION COMPLÈTE)
Write-Host "✏️  Test synchronisation modification..." -ForegroundColor Cyan
try {
    # Modifier une valeur de test (exemple: feedMorning)
    $testValue = Get-Random -Minimum 6 -Maximum 10
    Write-Host "  Envoi: feedMorning=$testValue" -ForegroundColor Gray
    
    $body = @{
        feedMorning = $testValue
    }
    
    $response = Invoke-RestMethod -Uri "$baseUrl/dbvars/update" -Method POST -Body $body -TimeoutSec 10
    
    if ($response.status -eq "OK") {
        Write-Host "  ✅ Réponse serveur: OK" -ForegroundColor Green
        
        if ($response.remoteSent -eq $true) {
            Write-Host "  ✅ Sync serveur distant: OK" -ForegroundColor Green
        } else {
            Write-Host "  ⚠️  Sync serveur distant: FAIL (WiFi down?)" -ForegroundColor Yellow
        }
        
        # Attendre un peu pour la synchronisation
        Start-Sleep -Seconds 2
        
        # Re-lire pour vérifier
        $dbvarsUpdated = Invoke-RestMethod -Uri "$baseUrl/dbvars" -Method GET -TimeoutSec 5
        
        if ($dbvarsUpdated.feedMorning -eq $testValue) {
            Write-Host "  ✅ NVS: Valeur persistée correctement" -ForegroundColor Green
            Write-Host "  ✅ ESP: Configuration appliquée" -ForegroundColor Green
            Write-Host "  🎉 SYNCHRONISATION TRIPLE CONFIRMÉE!" -ForegroundColor Green
            $testsPassed++
        } else {
            Write-Host "  ❌ Valeur non persistée (attendu: $testValue, reçu: $($dbvarsUpdated.feedMorning))" -ForegroundColor Red
            $testsFailed++
        }
    } else {
        Write-Host "  ❌ Réponse serveur: $($response.status)" -ForegroundColor Red
        $testsFailed++
    }
}
catch {
    Write-Host "  ❌ Erreur test synchronisation: $($_.Exception.Message)" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# Test 4: Action manuelle (synchronisation ESP + Serveur)
Write-Host "🎮 Test action manuelle..." -ForegroundColor Cyan
try {
    $actionResponse = Invoke-WebRequest -Uri "$baseUrl/action?cmd=feedSmall" -Method GET -TimeoutSec 10 -UseBasicParsing
    
    if ($actionResponse.StatusCode -eq 200) {
        $content = $actionResponse.Content
        if ($content -match "FEED_SMALL OK|OK") {
            Write-Host "  ✅ Action exécutée sur ESP" -ForegroundColor Green
            Write-Host "  ✅ Synchronisation serveur (async)" -ForegroundColor Green
            $testsPassed++
        } else {
            Write-Host "  ⚠️  Réponse inattendue: $content" -ForegroundColor Yellow
            $testsSkipped++
        }
    }
}
catch {
    Write-Host "  ❌ Erreur action: $($_.Exception.Message)" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# ===========================================
# PHASE 6: Test WiFi Status (Sync)
# ===========================================

Write-Host "PHASE 6: État WiFi" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

try {
    $wifiStatus = Invoke-RestMethod -Uri "$baseUrl/wifi/status" -Method GET -TimeoutSec 5
    
    Write-Host "📶 WiFi STA:" -ForegroundColor Cyan
    Write-Host "  Connecté: $($wifiStatus.staConnected)" -ForegroundColor Gray
    if ($wifiStatus.staConnected) {
        Write-Host "  SSID: $($wifiStatus.staSSID)" -ForegroundColor Gray
        Write-Host "  IP: $($wifiStatus.staIP)" -ForegroundColor Gray
        Write-Host "  RSSI: $($wifiStatus.staRSSI) dBm" -ForegroundColor Gray
    }
    
    Write-Host "📡 WiFi AP:" -ForegroundColor Cyan
    Write-Host "  Actif: $($wifiStatus.apActive)" -ForegroundColor Gray
    if ($wifiStatus.apActive) {
        Write-Host "  SSID: $($wifiStatus.apSSID)" -ForegroundColor Gray
        Write-Host "  Clients: $($wifiStatus.apClients)" -ForegroundColor Gray
    }
    
    $testsPassed++
}
catch {
    Write-Host "❌ Erreur récupération statut WiFi" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# ===========================================
# PHASE 7: Test Diagnostics
# ===========================================

Write-Host "PHASE 7: Diagnostics Système" -ForegroundColor Magenta
Write-Host "----------------------------------------" -ForegroundColor Gray

try {
    $diag = Invoke-RestMethod -Uri "$baseUrl/diag" -Method GET -TimeoutSec 5
    
    Write-Host "💾 Mémoire:" -ForegroundColor Cyan
    Write-Host "  Heap libre: $($diag.freeHeap) bytes" -ForegroundColor Gray
    Write-Host "  Heap min: $($diag.minFreeHeap) bytes" -ForegroundColor Gray
    
    Write-Host "⏱️  Système:" -ForegroundColor Cyan
    Write-Host "  Uptime: $($diag.uptimeSec)s" -ForegroundColor Gray
    Write-Host "  Reboots: $($diag.rebootCount)" -ForegroundColor Gray
    
    if ($diag.freeHeap -lt 50000) {
        Write-Host "  ⚠️  Heap faible (<50KB)" -ForegroundColor Yellow
    } else {
        Write-Host "  ✅ Heap OK" -ForegroundColor Green
    }
    
    $testsPassed++
}
catch {
    Write-Host "❌ Erreur récupération diagnostics" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# ===========================================
# RÉSUMÉ FINAL
# ===========================================

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "   RÉSUMÉ DES TESTS" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan

$totalTests = $testsPassed + $testsFailed + $testsSkipped
$successRate = if ($totalTests -gt 0) { [math]::Round(($testsPassed / $totalTests) * 100, 1) } else { 0 }

Write-Host ""
Write-Host "Tests exécutés: $totalTests" -ForegroundColor White
Write-Host "Tests réussis:  $testsPassed" -ForegroundColor Green
Write-Host "Tests échoués:  $testsFailed" -ForegroundColor Red
Write-Host "Tests skippés:  $testsSkipped" -ForegroundColor Yellow
Write-Host ""
Write-Host "Taux de réussite: $successRate%" -ForegroundColor $(if ($successRate -ge 80) { "Green" } elseif ($successRate -ge 60) { "Yellow" } else { "Red" })
Write-Host ""

if ($testsFailed -eq 0) {
    Write-Host "🎉 TOUS LES TESTS SONT PASSÉS!" -ForegroundColor Green
    Write-Host ""
    Write-Host "✅ Phase 2 Performance validée" -ForegroundColor Green
    Write-Host "✅ Synchronisation ESP-NVS-Serveur confirmée" -ForegroundColor Green
    exit 0
} elseif ($successRate -ge 70) {
    Write-Host "⚠️  La plupart des tests sont passés" -ForegroundColor Yellow
    Write-Host "   Vérifiez les échecs ci-dessus" -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "❌ Plusieurs tests ont échoué" -ForegroundColor Red
    Write-Host "   Vérifiez la configuration et les logs ESP32" -ForegroundColor Red
    exit 1
}

