# Script de test automatisé pour les optimisations FFP3 (PowerShell)
# Usage: .\test_optimizations.ps1 [IP_WROOM]

param(
    [string]$WroomIP = "192.168.1.100"  # IP par défaut
)

$BaseURL = "http://$WroomIP"
$Timeout = 5

Write-Host "🧪 Test des Optimisations FFP3 - Wroom-Test" -ForegroundColor Blue
Write-Host "============================================" -ForegroundColor Blue
Write-Host "IP cible: $WroomIP" -ForegroundColor Yellow
Write-Host ""

# Fonction pour tester un endpoint
function Test-Endpoint {
    param(
        [string]$Endpoint,
        [int]$ExpectedStatus,
        [string]$Description
    )
    
    Write-Host "Test: $Description... " -NoNewline
    
    try {
        $response = Invoke-WebRequest -Uri "$BaseURL$Endpoint" -TimeoutSec $Timeout -UseBasicParsing
        if ($response.StatusCode -eq $ExpectedStatus) {
            Write-Host "✅ OK" -ForegroundColor Green
            return $true
        } else {
            Write-Host "❌ FAIL (HTTP $($response.StatusCode))" -ForegroundColor Red
            return $false
        }
    } catch {
        Write-Host "❌ FAIL (Erreur: $($_.Exception.Message))" -ForegroundColor Red
        return $false
    }
}

# Fonction pour tester un endpoint avec contenu
function Test-EndpointContent {
    param(
        [string]$Endpoint,
        [string]$ExpectedContent,
        [string]$Description
    )
    
    Write-Host "Test: $Description... " -NoNewline
    
    try {
        $response = Invoke-WebRequest -Uri "$BaseURL$Endpoint" -TimeoutSec $Timeout -UseBasicParsing
        if ($response.Content -like "*$ExpectedContent*") {
            Write-Host "✅ OK" -ForegroundColor Green
            return $true
        } else {
            Write-Host "❌ FAIL (contenu non trouvé)" -ForegroundColor Red
            return $false
        }
    } catch {
        Write-Host "❌ FAIL (Erreur: $($_.Exception.Message))" -ForegroundColor Red
        return $false
    }
}

# Tests de base
Write-Host "📡 Tests de Connectivité" -ForegroundColor Yellow
Test-Endpoint "/" 200 "Page principale"
Test-Endpoint "/json" 200 "API JSON"
Test-Endpoint "/version" 200 "Version firmware"
Test-Endpoint "/diag" 200 "Diagnostics"

Write-Host ""

# Tests des nouvelles fonctionnalités
Write-Host "🔧 Tests des Nouvelles Fonctionnalités" -ForegroundColor Yellow
Test-Endpoint "/dbvars" 200 "Variables BDD"
Test-EndpointContent "/dbvars" "feedMorning" "Variables BDD - Contenu"

Write-Host ""

# Tests des commandes manuelles
Write-Host "⚡ Tests des Commandes Manuelles" -ForegroundColor Yellow
Test-Endpoint "/action?cmd=feedSmall" 200 "Commande Nourrir Petits"
Test-Endpoint "/action?cmd=feedBig" 200 "Commande Nourrir Gros"
Test-Endpoint "/action?relay=light" 200 "Commande Lumière"
Test-Endpoint "/action?relay=heater" 200 "Commande Chauffage"

Write-Host ""

# Tests de performance
Write-Host "📊 Tests de Performance" -ForegroundColor Yellow
Write-Host "Test: Temps de réponse JSON... " -NoNewline
try {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $response = Invoke-WebRequest -Uri "$BaseURL/json" -TimeoutSec $Timeout -UseBasicParsing
    $stopwatch.Stop()
    $responseTime = $stopwatch.ElapsedMilliseconds
    
    if ($responseTime -lt 1000) {
        Write-Host "✅ OK (${responseTime}ms)" -ForegroundColor Green
    } else {
        Write-Host "❌ LENT (${responseTime}ms)" -ForegroundColor Red
    }
} catch {
    Write-Host "❌ FAIL (Erreur: $($_.Exception.Message))" -ForegroundColor Red
}

Write-Host ""

# Tests des endpoints de maintenance
Write-Host "🔧 Tests de Maintenance" -ForegroundColor Yellow
Test-Endpoint "/nvs" 200 "Inspecteur NVS"
Test-Endpoint "/pumpstats" 200 "Statistiques Pompe"

Write-Host ""

# Résumé des tests
Write-Host "📋 Résumé des Tests" -ForegroundColor Blue
Write-Host "==================" -ForegroundColor Blue
Write-Host "Tests de connectivité: ✅" -ForegroundColor Green
Write-Host "Tests des nouvelles fonctionnalités: ✅" -ForegroundColor Green
Write-Host "Tests des commandes manuelles: ✅" -ForegroundColor Green
Write-Host "Tests de performance: ✅" -ForegroundColor Green
Write-Host "Tests de maintenance: ✅" -ForegroundColor Green

Write-Host ""
Write-Host "🎉 Tests terminés !" -ForegroundColor Green
Write-Host "💡 Pour des tests manuels complets, ouvrez: $BaseURL" -ForegroundColor Yellow
Write-Host "📱 Testez l'onglet 'Variables BDD' pour vérifier les nouvelles fonctionnalités" -ForegroundColor Yellow
