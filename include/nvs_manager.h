#pragma once
#include <Preferences.h>
#include <Arduino.h>
#include <vector>
#include <map>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NVSLockGuard;

/**
 * Gestionnaire NVS Centralisé - Version Simplifiée
 * 
 * Consolidation des namespaces NVS de 14 à 4 pour réduire la fragmentation
 * Gestion d'erreurs centralisée avec validation simplifiée (longueur clé uniquement)
 * 
 * Version: 11.169 (simplifiée - 4 namespaces, TIME/SENSORS fusionnés)
 */

// Namespaces consolidés (réduction de 14 à 4)
namespace NVS_NAMESPACES {
    extern const char* SYSTEM;      // ota, net, reset, forceWakeUp, rtc_epoch
    extern const char* CONFIG;      // bouffe, remoteVars, gpio, temp_lastValid
    extern const char* STATE;       // actSnap, actState, pendingSync
    extern const char* LOGS;        // diagnostics, alerts, crash
    // NOTE: TIME et SENSORS supprimés (fusionnés dans SYSTEM et CONFIG)
}

// Types d'erreurs NVS
enum class NVSError {
    SUCCESS = 0,
    NAMESPACE_NOT_FOUND = 1,
    KEY_NOT_FOUND = 2,
    INSUFFICIENT_SPACE = 3,
    CORRUPTION_DETECTED = 4,
    INVALID_KEY = 5,
    INVALID_VALUE = 6,
    WRITE_FAILED = 7,
    READ_FAILED = 8
};

// Statistiques d'utilisation NVS (simplifiées)
struct NVSUsageStats {
    size_t totalBytes;
    size_t usedBytes;
    size_t freeBytes;
    float usagePercent;
    size_t namespaceCount;
    size_t keyCount;
    
    NVSUsageStats() : totalBytes(0), usedBytes(0), freeBytes(0), 
                      usagePercent(0.0f), namespaceCount(0), keyCount(0) {}
};

class NVSManager {
private:
    friend class NVSLockGuard;
    Preferences _preferences;
    SemaphoreHandle_t _mutex;
    bool _initialized;
    
    // Méthodes privées
    NVSError openNamespace(const char* ns, bool readOnly = true);
    void closeNamespace();
    NVSError validateKey(const char* key);  // Validation simplifiée: longueur max 15 chars
    void logError(NVSError error, const char* context,
                  const char* ns = nullptr, const char* key = nullptr);
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();
    
public:
    NVSManager();
    ~NVSManager();
    
    // Initialisation
    bool begin();
    void end();
    
    // Opérations de base
    NVSError saveString(const char* ns, const char* key, const char* value);
    NVSError loadString(const char* ns, const char* key, char* value,
                       size_t valueSize, const char* defaultValue = "");
    NVSError saveBool(const char* ns, const char* key, bool value);
    NVSError loadBool(const char* ns, const char* key, bool& value, bool defaultValue = false);
    NVSError saveInt(const char* ns, const char* key, int value);
    NVSError loadInt(const char* ns, const char* key, int& value, int defaultValue = 0);
    NVSError saveFloat(const char* ns, const char* key, float value);
    NVSError loadFloat(const char* ns, const char* key, float& value, float defaultValue = 0.0f);
    NVSError saveULong(const char* ns, const char* key, unsigned long value);
    NVSError loadULong(const char* ns, const char* key, unsigned long& value,
                      unsigned long defaultValue = 0);
    
    // Utilitaires
    bool keyExists(const char* ns, const char* key);
    NVSError removeKey(const char* ns, const char* key);
    NVSError clearNamespace(const char* ns);
    
    // Migration depuis l'ancien système
    NVSError migrateFromOldSystem();
    
    // Statistiques et monitoring (simplifiées)
    NVSUsageStats getUsageStats();
    void logUsageStats();
    
    // Debug
    void printNamespaceContents(const char* ns);
    
    // Vérification d'initialisation
    bool isInitialized() const { return _initialized; }
};

// Instance globale
extern NVSManager g_nvsManager;
