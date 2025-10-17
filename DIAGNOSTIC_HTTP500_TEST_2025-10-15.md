# DIAGNOSTIC HTTP 500 - ENVIRONNEMENT TEST

**Date**: 2025-10-15  
**Problème**: Erreur HTTP 500 sur endpoint `/post-data-test`  
**Statut**: 🔍 **EN COURS DE DIAGNOSTIC**

## 🎯 **PROBLÈME IDENTIFIÉ**

L'ESP32 en environnement TEST envoie des données vers `/post-data-test` mais reçoit une **erreur HTTP 500** au lieu de HTTP 200.

### **Hypothèses principales**

1. **Tables TEST manquantes** : `ffp3Data2`, `ffp3Outputs2` n'existent pas
2. **Configuration .env incorrecte** : `ENV=prod` au lieu de `ENV=test`
3. **Problème de routing** : Route `/post-data-test` mal configurée
4. **Erreur SQL** : Tentative d'insertion dans table inexistante

## 🛠️ **PLAN DE CORRECTION**

### **Phase 1 : Diagnostic**
- ✅ Script de vérification des tables TEST
- ✅ Script de test curl
- ⏳ Exécution sur serveur distant
- ⏳ Analyse des résultats

### **Phase 2 : Correction**
- ⏳ Ajout de logs détaillés
- ⏳ Test avec curl
- ⏳ Analyse des logs serveur
- ⏳ Correction des erreurs

### **Phase 3 : Validation**
- ⏳ Test complet communication ESP32 ↔ Serveur
- ⏳ Vérification données dans `ffp3Data2`
- ⏳ Vérification GPIO dans `ffp3Outputs2`

## 📋 **SCRIPTS CRÉÉS**

### **1. `ffp3/tools/check_test_tables.php`**
- Vérifie l'existence des tables TEST
- Analyse la structure des colonnes
- Teste l'insertion de données
- Liste les GPIO configurés

### **2. `ffp3/tools/test_post_data.sh`**
- Simule une requête ESP32 avec curl
- Teste l'endpoint `/post-data-test`
- Affiche le code HTTP et la réponse

## 🚀 **PROCHAINES ÉTAPES**

1. **Exécuter le diagnostic** sur le serveur distant
2. **Analyser les résultats** pour identifier la cause
3. **Appliquer les corrections** nécessaires
4. **Tester la communication** ESP32 ↔ Serveur

## 📊 **RÉSULTAT ATTENDU**

Après correction :
- ✅ **HTTP 200** au lieu de 500
- ✅ **Données insérées** dans `ffp3Data2`
- ✅ **GPIO mis à jour** dans `ffp3Outputs2`
- ✅ **Communication fluide** ESP32 ↔ Serveur

---

**Note** : Ce diagnostic permettra d'identifier précisément la cause de l'erreur HTTP 500 et de la corriger définitivement.