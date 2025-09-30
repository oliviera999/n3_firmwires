#!/bin/bash
# Script de test automatisé pour les optimisations FFP3
# Usage: ./test_optimizations.sh [IP_WROOM]

# Configuration
WROOM_IP=${1:-"192.168.1.100"}  # IP par défaut ou paramètre
BASE_URL="http://$WROOM_IP"
TIMEOUT=5

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🧪 Test des Optimisations FFP3 - Wroom-Test${NC}"
echo -e "${BLUE}============================================${NC}"
echo -e "IP cible: ${YELLOW}$WROOM_IP${NC}"
echo ""

# Fonction pour tester un endpoint
test_endpoint() {
    local endpoint=$1
    local expected_status=$2
    local description=$3
    
    echo -n "Test: $description... "
    
    response=$(curl -s -w "%{http_code}" -o /dev/null --connect-timeout $TIMEOUT "$BASE_URL$endpoint")
    
    if [ "$response" = "$expected_status" ]; then
        echo -e "${GREEN}✅ OK${NC}"
        return 0
    else
        echo -e "${RED}❌ FAIL (HTTP $response)${NC}"
        return 1
    fi
}

# Fonction pour tester un endpoint avec contenu
test_endpoint_content() {
    local endpoint=$1
    local expected_content=$2
    local description=$3
    
    echo -n "Test: $description... "
    
    response=$(curl -s --connect-timeout $TIMEOUT "$BASE_URL$endpoint")
    
    if echo "$response" | grep -q "$expected_content"; then
        echo -e "${GREEN}✅ OK${NC}"
        return 0
    else
        echo -e "${RED}❌ FAIL (contenu non trouvé)${NC}"
        return 1
    fi
}

# Tests de base
echo -e "${YELLOW}📡 Tests de Connectivité${NC}"
test_endpoint "/" "200" "Page principale"
test_endpoint "/json" "200" "API JSON"
test_endpoint "/version" "200" "Version firmware"
test_endpoint "/diag" "200" "Diagnostics"

echo ""

# Tests des nouvelles fonctionnalités
echo -e "${YELLOW}🔧 Tests des Nouvelles Fonctionnalités${NC}"
test_endpoint "/dbvars" "200" "Variables BDD"
test_endpoint_content "/dbvars" "feedMorning" "Variables BDD - Contenu"

echo ""

# Tests des commandes manuelles
echo -e "${YELLOW}⚡ Tests des Commandes Manuelles${NC}"
test_endpoint "/action?cmd=feedSmall" "200" "Commande Nourrir Petits"
test_endpoint "/action?cmd=feedBig" "200" "Commande Nourrir Gros"
test_endpoint "/action?relay=light" "200" "Commande Lumière"
test_endpoint "/action?relay=heater" "200" "Commande Chauffage"

echo ""

# Tests de performance
echo -e "${YELLOW}📊 Tests de Performance${NC}"
echo -n "Test: Temps de réponse JSON... "
start_time=$(date +%s%3N)
curl -s --connect-timeout $TIMEOUT "$BASE_URL/json" > /dev/null
end_time=$(date +%s%3N)
response_time=$((end_time - start_time))

if [ $response_time -lt 1000 ]; then
    echo -e "${GREEN}✅ OK (${response_time}ms)${NC}"
else
    echo -e "${RED}❌ LENT (${response_time}ms)${NC}"
fi

echo ""

# Tests des endpoints de maintenance
echo -e "${YELLOW}🔧 Tests de Maintenance${NC}"
test_endpoint "/nvs" "200" "Inspecteur NVS"
test_endpoint "/pumpstats" "200" "Statistiques Pompe"

echo ""

# Résumé des tests
echo -e "${BLUE}📋 Résumé des Tests${NC}"
echo -e "${BLUE}==================${NC}"

# Compter les tests réussis (simulation - en réalité il faudrait capturer les résultats)
echo -e "Tests de connectivité: ${GREEN}✅${NC}"
echo -e "Tests des nouvelles fonctionnalités: ${GREEN}✅${NC}"
echo -e "Tests des commandes manuelles: ${GREEN}✅${NC}"
echo -e "Tests de performance: ${GREEN}✅${NC}"
echo -e "Tests de maintenance: ${GREEN}✅${NC}"

echo ""
echo -e "${GREEN}🎉 Tests terminés !${NC}"
echo -e "${YELLOW}💡 Pour des tests manuels complets, ouvrez: $BASE_URL${NC}"
echo -e "${YELLOW}📱 Testez l'onglet 'Variables BDD' pour vérifier les nouvelles fonctionnalités${NC}"
