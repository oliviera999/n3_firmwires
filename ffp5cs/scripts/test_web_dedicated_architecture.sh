#!/bin/bash
# Script de test pour l'architecture Web Dédié
# Valide la nouvelle hiérarchie des priorités et la réactivité web

echo "🚀 Test de l'Architecture Web Dédié - FFP3 v7.0"
echo "================================================"

# Configuration
ESP_IP="192.168.1.100"  # Remplacer par l'IP de votre ESP32
TEST_DURATION=30         # Durée du test en secondes

echo "📡 IP ESP32: $ESP_IP"
echo "⏱️  Durée du test: ${TEST_DURATION}s"
echo ""

# Fonction de test de connectivité
test_connectivity() {
    echo "🔍 Test de connectivité..."
    if ping -c 1 $ESP_IP > /dev/null 2>&1; then
        echo "✅ ESP32 accessible"
        return 0
    else
        echo "❌ ESP32 inaccessible"
        return 1
    fi
}

# Fonction de test de réactivité web
test_web_responsiveness() {
    echo "🌐 Test de réactivité web..."
    
    local total_time=0
    local success_count=0
    local test_count=10
    
    for i in $(seq 1 $test_count); do
        echo -n "  Test $i/$test_count... "
        
        start_time=$(date +%s%3N)
        response=$(curl -s -w "%{http_code}" -o /dev/null "http://$ESP_IP/" --max-time 5)
        end_time=$(date +%s%3N)
        
        response_time=$((end_time - start_time))
        
        if [ "$response" = "200" ]; then
            echo "✅ ${response_time}ms"
            total_time=$((total_time + response_time))
            success_count=$((success_count + 1))
        else
            echo "❌ HTTP $response"
        fi
        
        sleep 1
    done
    
    if [ $success_count -gt 0 ]; then
        avg_time=$((total_time / success_count))
        echo "📊 Temps de réponse moyen: ${avg_time}ms"
        echo "📈 Taux de succès: $((success_count * 100 / test_count))%"
        
        if [ $avg_time -lt 200 ]; then
            echo "✅ Réactivité web EXCELLENTE (< 200ms)"
        elif [ $avg_time -lt 500 ]; then
            echo "✅ Réactivité web BONNE (< 500ms)"
        else
            echo "⚠️  Réactivité web à améliorer (> 500ms)"
        fi
    else
        echo "❌ Aucun test réussi"
    fi
}

# Fonction de test des APIs
test_apis() {
    echo "🔌 Test des APIs..."
    
    local apis=("/json" "/version" "/diag" "/pumpstats")
    
    for api in "${apis[@]}"; do
        echo -n "  Test $api... "
        response=$(curl -s -w "%{http_code}" -o /dev/null "http://$ESP_IP$api" --max-time 3)
        
        if [ "$response" = "200" ]; then
            echo "✅ OK"
        else
            echo "❌ HTTP $response"
        fi
    done
}

# Fonction de test de charge
test_load() {
    echo "⚡ Test de charge (${TEST_DURATION}s)..."
    
    local start_time=$(date +%s)
    local request_count=0
    local success_count=0
    
    while [ $(($(date +%s) - start_time)) -lt $TEST_DURATION ]; do
        response=$(curl -s -w "%{http_code}" -o /dev/null "http://$ESP_IP/json" --max-time 2)
        request_count=$((request_count + 1))
        
        if [ "$response" = "200" ]; then
            success_count=$((success_count + 1))
        fi
        
        sleep 0.1  # 10 requêtes par seconde
    done
    
    local duration=$(($(date +%s) - start_time))
    local rps=$((request_count / duration))
    local success_rate=$((success_count * 100 / request_count))
    
    echo "📊 Requêtes totales: $request_count"
    echo "📈 Requêtes/seconde: $rps"
    echo "✅ Taux de succès: ${success_rate}%"
    
    if [ $success_rate -gt 95 ]; then
        echo "✅ Stabilité EXCELLENTE"
    elif [ $success_rate -gt 90 ]; then
        echo "✅ Stabilité BONNE"
    else
        echo "⚠️  Stabilité à améliorer"
    fi
}

# Fonction de test des WebSockets
test_websockets() {
    echo "🔌 Test des WebSockets..."
    
    # Test basique de connexion WebSocket
    echo -n "  Connexion WebSocket... "
    
    # Utiliser wscat si disponible, sinon test simple
    if command -v wscat > /dev/null 2>&1; then
        timeout 5 wscat -c "ws://$ESP_IP/ws" > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "✅ OK"
        else
            echo "❌ Échec"
        fi
    else
        echo "⚠️  wscat non disponible - test WebSocket ignoré"
    fi
}

# Fonction de monitoring des stacks
monitor_stacks() {
    echo "📊 Monitoring des stacks..."
    
    response=$(curl -s "http://$ESP_IP/optimization-stats" --max-time 5)
    
    if [ $? -eq 0 ]; then
        echo "✅ Statistiques récupérées"
        echo "$response" | jq '.' 2>/dev/null || echo "$response"
    else
        echo "❌ Impossible de récupérer les statistiques"
    fi
}

# Fonction principale
main() {
    echo "Démarrage des tests..."
    echo ""
    
    # Test de connectivité
    if ! test_connectivity; then
        echo "❌ Arrêt des tests - ESP32 inaccessible"
        exit 1
    fi
    echo ""
    
    # Tests de base
    test_web_responsiveness
    echo ""
    
    test_apis
    echo ""
    
    test_websockets
    echo ""
    
    # Test de charge
    test_load
    echo ""
    
    # Monitoring
    monitor_stacks
    echo ""
    
    echo "🎉 Tests terminés !"
    echo ""
    echo "📋 Résumé des améliorations attendues :"
    echo "  ✅ Réactivité web < 100ms"
    echo "  ✅ Stabilité > 95%"
    echo "  ✅ Isolation des tâches"
    echo "  ✅ Priorités optimisées"
    echo ""
    echo "📖 Documentation : docs/guides/WEB_DEDICATED_TASK_ARCHITECTURE.md"
}

# Vérification des dépendances
if ! command -v curl > /dev/null 2>&1; then
    echo "❌ curl requis pour les tests"
    exit 1
fi

if ! command -v ping > /dev/null 2>&1; then
    echo "❌ ping requis pour les tests"
    exit 1
fi

# Exécution
main "$@"
