/**
 * @file test_nvs_cache_verification.cpp
 * @brief Tests pour vérifier la vérification cache dans les méthodes save*() de NVSManager
 * 
 * Ce fichier contient des tests unitaires pour vérifier que :
 * 1. Les valeurs identiques ne sont pas écrites en flash (vérification cache fonctionne)
 * 2. Les valeurs différentes sont écrites en flash
 * 3. Les logs confirment le comportement attendu
 * 
 * Pour exécuter ces tests, ajouter ce fichier dans le projet et appeler testNVSCacheVerification()
 * depuis setup() ou une fonction de test dédiée.
 */

#include "nvs_manager.h"
#include <Arduino.h>

/**
 * Test 1: Vérifier que saveBool() ne réécrit pas si valeur identique
 */
void testSaveBoolIdentical() {
    Serial.println("\n=== Test saveBool() - Valeur identique ===");
    
    const char* ns = "test";
    const char* key = "flag";
    bool value = true;
    
    // Première écriture
    Serial.println("Première écriture: saveBool('test', 'flag', true)");
    NVSError err1 = g_nvsManager.saveBool(ns, key, value);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec même valeur
    Serial.println("Deuxième écriture (même valeur): saveBool('test', 'flag', true)");
    Serial.println("Attendu: '[NVS] 💾 Valeur inchangée, pas de sauvegarde: test::flag'");
    NVSError err2 = g_nvsManager.saveBool(ns, key, value);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveBool() ===\n");
}

/**
 * Test 2: Vérifier que saveBool() écrit si valeur différente
 */
void testSaveBoolDifferent() {
    Serial.println("\n=== Test saveBool() - Valeur différente ===");
    
    const char* ns = "test";
    const char* key = "flag2";
    bool value1 = true;
    bool value2 = false;
    
    // Première écriture
    Serial.println("Première écriture: saveBool('test', 'flag2', true)");
    NVSError err1 = g_nvsManager.saveBool(ns, key, value1);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec valeur différente
    Serial.println("Deuxième écriture (valeur différente): saveBool('test', 'flag2', false)");
    Serial.println("Attendu: '[NVS] ✅ Sauvegardé: test::flag2 = 0'");
    NVSError err2 = g_nvsManager.saveBool(ns, key, value2);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveBool() ===\n");
}

/**
 * Test 3: Vérifier que saveInt() ne réécrit pas si valeur identique
 */
void testSaveIntIdentical() {
    Serial.println("\n=== Test saveInt() - Valeur identique ===");
    
    const char* ns = "test";
    const char* key = "counter";
    int value = 42;
    
    // Première écriture
    Serial.println("Première écriture: saveInt('test', 'counter', 42)");
    NVSError err1 = g_nvsManager.saveInt(ns, key, value);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec même valeur
    Serial.println("Deuxième écriture (même valeur): saveInt('test', 'counter', 42)");
    Serial.println("Attendu: '[NVS] 💾 Valeur inchangée, pas de sauvegarde: test::counter'");
    NVSError err2 = g_nvsManager.saveInt(ns, key, value);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveInt() ===\n");
}

/**
 * Test 4: Vérifier que saveInt() écrit si valeur différente
 */
void testSaveIntDifferent() {
    Serial.println("\n=== Test saveInt() - Valeur différente ===");
    
    const char* ns = "test";
    const char* key = "counter2";
    int value1 = 42;
    int value2 = 43;
    
    // Première écriture
    Serial.println("Première écriture: saveInt('test', 'counter2', 42)");
    NVSError err1 = g_nvsManager.saveInt(ns, key, value1);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec valeur différente
    Serial.println("Deuxième écriture (valeur différente): saveInt('test', 'counter2', 43)");
    Serial.println("Attendu: '[NVS] ✅ Sauvegardé: test::counter2 = 43'");
    NVSError err2 = g_nvsManager.saveInt(ns, key, value2);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveInt() ===\n");
}

/**
 * Test 5: Vérifier que saveFloat() ne réécrit pas si valeur identique
 */
void testSaveFloatIdentical() {
    Serial.println("\n=== Test saveFloat() - Valeur identique ===");
    
    const char* ns = "test";
    const char* key = "temperature";
    float value = 25.50f;
    
    // Première écriture
    Serial.println("Première écriture: saveFloat('test', 'temperature', 25.50)");
    NVSError err1 = g_nvsManager.saveFloat(ns, key, value);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec même valeur
    Serial.println("Deuxième écriture (même valeur): saveFloat('test', 'temperature', 25.50)");
    Serial.println("Attendu: '[NVS] 💾 Valeur inchangée, pas de sauvegarde: test::temperature'");
    NVSError err2 = g_nvsManager.saveFloat(ns, key, value);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveFloat() ===\n");
}

/**
 * Test 6: Vérifier que saveULong() ne réécrit pas si valeur identique
 */
void testSaveULongIdentical() {
    Serial.println("\n=== Test saveULong() - Valeur identique ===");
    
    const char* ns = "test";
    const char* key = "timestamp";
    unsigned long value = 1234567890UL;
    
    // Première écriture
    Serial.println("Première écriture: saveULong('test', 'timestamp', 1234567890)");
    NVSError err1 = g_nvsManager.saveULong(ns, key, value);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec même valeur
    Serial.println("Deuxième écriture (même valeur): saveULong('test', 'timestamp', 1234567890)");
    Serial.println("Attendu: '[NVS] 💾 Valeur inchangée, pas de sauvegarde: test::timestamp'");
    NVSError err2 = g_nvsManager.saveULong(ns, key, value);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveULong() ===\n");
}

/**
 * Test 7: Vérifier que saveString() ne réécrit pas si valeur identique
 */
void testSaveStringIdentical() {
    Serial.println("\n=== Test saveString() - Valeur identique ===");
    
    const char* ns = "test";
    const char* key = "message";
    const char* value = "Hello World";
    
    // Première écriture
    Serial.println("Première écriture: saveString('test', 'message', 'Hello World')");
    NVSError err1 = g_nvsManager.saveString(ns, key, value);
    Serial.printf("Résultat: %s\n", (err1 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Deuxième écriture avec même valeur
    Serial.println("Deuxième écriture (même valeur): saveString('test', 'message', 'Hello World')");
    Serial.println("Attendu: '[NVS] 💾 Valeur inchangée, pas de sauvegarde: test::message'");
    NVSError err2 = g_nvsManager.saveString(ns, key, value);
    Serial.printf("Résultat: %s\n", (err2 == NVSError::SUCCESS) ? "SUCCESS" : "FAILED");
    
    Serial.println("=== Fin test saveString() ===\n");
}

/**
 * Fonction principale pour exécuter tous les tests
 * À appeler depuis setup() ou une fonction de test dédiée
 */
void testNVSCacheVerification() {
    Serial.println("\n");
    Serial.println("========================================");
    Serial.println("TESTS VERIFICATION CACHE NVS");
    Serial.println("========================================");
    
    // Initialiser NVS si pas déjà fait
    if (!g_nvsManager.begin()) {
        Serial.println("ERREUR: Impossible d'initialiser NVSManager");
        return;
    }
    
    // Tests valeurs identiques (ne doivent PAS écrire)
    testSaveBoolIdentical();
    testSaveIntIdentical();
    testSaveFloatIdentical();
    testSaveULongIdentical();
    testSaveStringIdentical();
    
    // Tests valeurs différentes (doivent écrire)
    testSaveBoolDifferent();
    testSaveIntDifferent();
    
    Serial.println("========================================");
    Serial.println("FIN DES TESTS");
    Serial.println("========================================");
    Serial.println("\nVérifiez les logs ci-dessus pour confirmer:");
    Serial.println("1. Valeurs identiques: message '[NVS] 💾 Valeur inchangée'");
    Serial.println("2. Valeurs différentes: message '[NVS] ✅ Sauvegardé'");
    Serial.println("\n");
}
