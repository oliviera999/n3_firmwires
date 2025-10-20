#pragma once
#include <Preferences.h>
#include <Arduino.h>
#include <vector>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Gestionnaire NVS Centralisé - Phase 1 Optimisation
 * 
 * Consolidation des namespaces NVS de 14 à 6 pour réduire la fragmentation
 * Gestion d'erreurs centralisée avec validation des données
 * 
 * Version: 11.80
 */

// Namespaces consolidés (réduction de 14 à 6)
namespace NVS_NAMESPACES {
    extern const char* SYSTEM;      // ota, net, reset
    extern const char* CONFIG;      // bouffe, remoteVars, gpio
    extern const char* TIME;        // rtc, timeDrift
    extern const char* STATE;       // actSnap, actState, pendingSync
    extern const char* LOGS;        // diagnostics, cmdLog, alerts
    extern const char* SENSORS;     // waterTemp, digest
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

// Structure pour le cache NVS
struct NVSCacheEntry {
    String key;
    String value;
    time_t timestamp;
    uint32_t checksum;
    bool dirty; // Indique si la valeur a été modifiée
    
    NVSCacheEntry() : timestamp(0), checksum(0), dirty(false) {}
    NVSCacheEntry(const String& k, const String& v) 
        : key(k), value(v), timestamp(millis()), checksum(0), dirty(false) {
        calculateChecksum();
    }
    
    void calculateChecksum() {
        checksum = 0;
        for (size_t i = 0; i < value.length(); i++) {
            checksum = checksum * 31 + value.charAt(i);
        }
    }
    
    bool isValid() const {
        uint32_t currentChecksum = 0;
        for (size_t i = 0; i < value.length(); i++) {
            currentChecksum = currentChecksum * 31 + value.charAt(i);
        }
        return currentChecksum == checksum;
    }
};

// Statistiques d'utilisation NVS
struct NVSUsageStats {
    size_t totalBytes;
    size_t usedBytes;
    size_t freeBytes;
    float usagePercent;
    size_t namespaceCount;
    size_t keyCount;
    size_t cacheEntries;
    
    NVSUsageStats() : totalBytes(0), usedBytes(0), freeBytes(0), 
                      usagePercent(0.0f), namespaceCount(0), keyCount(0), cacheEntries(0) {}
};

class NVSManager {
private:
    Preferences _preferences;
    SemaphoreHandle_t _mutex;
    std::map<String, std::vector<NVSCacheEntry>> _cache;
    bool _initialized;
    size_t _maxCacheSize;
    
    // Phase 2: Cache unifié avec flush différé
    bool _deferredFlushEnabled;
    unsigned long _flushIntervalMs;
    unsigned long _lastFlushTime;
    std::vector<String> _dirtyKeys;
    
    // Phase 3: Variables pour nettoyage automatique
    unsigned long _lastCleanupTime;
    unsigned long _cleanupIntervalMs; // Clés modifiées depuis le dernier flush
    
    // Méthodes privées
    NVSError openNamespace(const char* ns, bool readOnly = true);
    void closeNamespace();
    NVSError validateKey(const char* key);
    NVSError validateValue(const String& value);
    uint32_t calculateChecksum(const String& value);
    void logError(NVSError error, const char* context, const char* ns = nullptr, const char* key = nullptr);
    
    // Phase 2: Méthodes privées pour flush différé
    void addDirtyKey(const String& ns, const char* key);
    void removeDirtyKey(const String& ns, const char* key);
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();
    
public:
    NVSManager();
    ~NVSManager();
    
    // Initialisation
    bool begin();
    void end();
    
    // Opérations de base
    NVSError saveString(const char* ns, const char* key, const String& value);
    NVSError loadString(const char* ns, const char* key, String& value, const String& defaultValue = "");
    NVSError saveBool(const char* ns, const char* key, bool value);
    NVSError loadBool(const char* ns, const char* key, bool& value, bool defaultValue = false);
    NVSError saveInt(const char* ns, const char* key, int value);
    NVSError loadInt(const char* ns, const char* key, int& value, int defaultValue = 0);
    NVSError saveFloat(const char* ns, const char* key, float value);
    NVSError loadFloat(const char* ns, const char* key, float& value, float defaultValue = 0.0f);
    NVSError saveULong(const char* ns, const char* key, unsigned long value);
    NVSError loadULong(const char* ns, const char* key, unsigned long& value, unsigned long defaultValue = 0);
    
    // Phase 2: Compression JSON
    NVSError saveJsonCompressed(const char* ns, const char* key, const String& json);
    NVSError loadJsonDecompressed(const char* ns, const char* key, String& json, const String& defaultValue = "");
    String compressJson(const String& json);
    String decompressJson(const String& compressed);
    
    // Gestion du cache
    void flushCache();
    void clearCache();
    bool isCached(const char* ns, const char* key);
    
    // Phase 2: Cache unifié avec flush différé
    void enableDeferredFlush(bool enable = true);
    void setFlushInterval(unsigned long intervalMs = 5000); // 5 secondes par défaut
    void forceFlush(); // Force le flush immédiat
    void checkDeferredFlush(); // Vérification périodique du flush
    bool isDeferredFlushEnabled() const;
    unsigned long getLastFlushTime() const;
    
    // Validation et maintenance
    NVSError validateNamespace(const char* ns);
    NVSError repairNamespace(const char* ns);
    void cleanupCorruptedData();
    
    // Statistiques et monitoring
    NVSUsageStats getUsageStats();
    void logUsageStats();
    bool isSpaceLow(float threshold = 80.0f);
    
    // Utilitaires
    bool keyExists(const char* ns, const char* key);
    NVSError removeKey(const char* ns, const char* key);
    NVSError clearNamespace(const char* ns);
    
    // Migration depuis l'ancien système
    NVSError migrateFromOldSystem();
    
    // Phase 3: Rotation automatique des logs et nettoyage
    NVSError rotateLogs(const char* ns, size_t maxEntries = 50);
    NVSError cleanupOldData(const char* ns, unsigned long maxAgeMs = 604800000UL); // 7 jours par défaut
    NVSError cleanupObsoleteKeys();
    void schedulePeriodicCleanup();
    bool shouldPerformCleanup();
    
    // Debug
    void printCacheStatus();
    void printNamespaceContents(const char* ns);
};

// Instance globale
extern NVSManager g_nvsManager;
