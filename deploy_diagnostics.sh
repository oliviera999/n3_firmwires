#!/bin/bash

# =============================================================================
# Script de déploiement des diagnostics HTTP 500 TEST
# =============================================================================
# Description:
#   Déploie les scripts de diagnostic sur le serveur distant et exécute
#   des tests pour identifier les problèmes HTTP 500.
#
# Prérequis:
#   - Accès SSH au serveur (clé SSH configurée)
#   - Accès SCP pour le transfert de fichiers
#   - Bash (Linux/Mac) ou Git Bash/WSL (Windows)
#
# Usage:
#   ./deploy_diagnostics.sh
#
# Configuration:
#   - Modifier les variables SERVER, USER, REMOTE_PATH si nécessaire
#
# Date: 2025-10-15
# =============================================================================

echo "========================================"
echo "DÉPLOIEMENT DIAGNOSTICS HTTP 500 TEST"
echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"

# Configuration serveur
SERVER="iot.olution.info"
USER="oliviera"
REMOTE_PATH="/home4/oliviera/iot.olution.info/ffp3"

echo "1. DÉPLOIEMENT DES SCRIPTS DE DIAGNOSTIC"
echo "========================================"

# Déployer les scripts de diagnostic
echo "📤 Déploiement des scripts de diagnostic..."
scp ffp3/tools/check_test_tables.php $USER@$SERVER:$REMOTE_PATH/tools/
scp ffp3/tools/test_post_data.sh $USER@$SERVER:$REMOTE_PATH/tools/

echo "✅ Scripts déployés"

echo ""
echo "2. EXÉCUTION DES DIAGNOSTICS SUR LE SERVEUR"
echo "==========================================="

# Exécuter le diagnostic des tables
echo "🔍 Exécution du diagnostic des tables TEST..."
ssh $USER@$SERVER "cd $REMOTE_PATH/tools && php check_test_tables.php"

echo ""
echo "🧪 Test de l'endpoint /post-data-test..."
ssh $USER@$SERVER "cd $REMOTE_PATH/tools && chmod +x test_post_data.sh && ./test_post_data.sh"

echo ""
echo "3. VÉRIFICATION DES LOGS"
echo "========================"
echo "📋 Consultation des logs post-data..."
ssh $USER@$SERVER "tail -20 $REMOTE_PATH/var/logs/post-data.log"

echo ""
echo "========================================"
echo "DÉPLOIEMENT TERMINÉ"
echo "========================================"
echo ""
echo "Prochaines étapes:"
echo "1. Analyser les résultats du diagnostic"
echo "2. Identifier la cause de l'erreur HTTP 500"
echo "3. Appliquer les corrections nécessaires"
echo "4. Tester la communication ESP32 ↔ Serveur"