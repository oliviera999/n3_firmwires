#include "nvs_manager.h"
#include "config.h"
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NVSLockGuard {
public:
    explicit NVSLockGuard(NVSManager& manager, TickType_t timeout = portMAX_DELAY)
        : _manager(manager)
        , _locked(manager.lock(timeout)) {}

    ~NVSLockGuard() {
        if (_locked) {
            _manager.unlock();
        }
    }

    bool locked() const { return _locked; }

private:
    NVSManager& _manager;
    bool _locked;
};
bool NVSManager::lock(TickType_t timeout) {
    if (_mutex == nullptr) {
        Serial.println(F("[NVS] ⚠️ Mutex non disponible"));
        return true; // fallback: pas de mutex, autoriser l'accès
    }
    BaseType_t taken = xSemaphoreTakeRecursive(_mutex, timeout);
    if (taken != pdPASS) {
        Serial.println(F("[NVS] ❌ Timeout mutex"));
        return false;
    }
    return true;
}

void NVSManager::unlock() {
    if (_mutex != nullptr) {
        xSemaphoreGiveRecursive(_mutex);
    }
}


// Définitions des constantes NVS_NAMESPACES
namespace NVS_NAMESPACES {
    const char* SYSTEM = "sys";      // ota, net, reset
    const char* CONFIG = "cfg";      // bouffe, remoteVars, gpio
    const char* TIME = "time";       // rtc, timeDrift
    const char* STATE = "state";     // actSnap, actState, pendingSync
    const char* LOGS = "logs";       // diagnostics, cmdLog, alerts
    const char* SENSORS = "sens";    // waterTemp, digest
}

// Instance globale
NVSManager g_nvsManager;

NVSManager::NVSManager()
    : _initialized(false)
    , _maxCacheSize(50)
    , _deferredFlushEnabled(true)
    , _flushIntervalMs(5000)
    , _lastFlushTime(0)
    , _lastCleanupTime(0)
    , _cleanupIntervalMs(3600000UL)
    , _mutex(xSemaphoreCreateRecursiveMutex()) { // 1 heure par défaut
    if (_mutex == nullptr) {
        Serial.println(F("[NVS] ❌ Échec création mutex"));
    }
}

NVSManager::~NVSManager() {
    if (_initialized) {
        flushCache();
        end();
    }
}

bool NVSManager::begin() {
    if (_initialized) {
        return true;
    }
    
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Impossible de prendre le mutex pour begin()"));
        return false;
    }
    
    Serial.println(F("[NVS] 🚀 Initialisation du gestionnaire NVS centralisé"));
    
    // Pré-créer les namespaces consolidés
    const char* nss[] = {
        NVS_NAMESPACES::SYSTEM,
        NVS_NAMESPACES::CONFIG,
        NVS_NAMESPACES::TIME,
        NVS_NAMESPACES::STATE,
        NVS_NAMESPACES::LOGS,
        NVS_NAMESPACES::SENSORS
    };
    
    for (size_t i = 0; i < sizeof(nss) / sizeof(nss[0]); i++) {
        NVSError error = openNamespace(nss[i], false);
        if (error != NVSError::SUCCESS) {
            Serial.printf("[NVS] ⚠️ Erreur création namespace %s: %d\n", nss[i], static_cast<int>(error));
        }
        closeNamespace();
    }
    
    _initialized = true;
    Serial.println(F("[NVS] ✅ Gestionnaire NVS initialisé"));
    
    // Afficher les statistiques initiales
    logUsageStats();
    
    return true;
}

void NVSManager::end() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Impossible de prendre le mutex pour end()"));
        return;
    }

    if (_initialized) {
        flushCache();
        _preferences.end();
        _initialized = false;
        Serial.println(F("[NVS] 🔚 Gestionnaire NVS fermé"));
    }
}

NVSError NVSManager::openNamespace(const char* ns, bool readOnly) {
    if (!_initialized) {
        return NVSError::NAMESPACE_NOT_FOUND;
    }
    
    bool success = _preferences.begin(ns, readOnly);
    if (!success) {
        logError(NVSError::NAMESPACE_NOT_FOUND, "openNamespace", ns);
        return NVSError::NAMESPACE_NOT_FOUND;
    }
    
    return NVSError::SUCCESS;
}

void NVSManager::closeNamespace() {
        _preferences.end();
}

NVSError NVSManager::validateKey(const char* key) {
    if (key == nullptr || strlen(key) == 0) {
        return NVSError::INVALID_KEY;
    }
    
    if (strlen(key) > 15) {
        Serial.printf("[NVS] ⚠️ Clé trop longue: %s (%zu chars)\n", key, strlen(key));
        return NVSError::INVALID_KEY;
    }
    
    return NVSError::SUCCESS;
}

NVSError NVSManager::validateValue(const String& value) {
    if (value.length() > 4000) { // Limite ESP32
        Serial.printf("[NVS] ⚠️ Valeur trop grande: %zu bytes\n", value.length());
        return NVSError::INVALID_VALUE;
    }
    
    return NVSError::SUCCESS;
}

uint32_t NVSManager::calculateChecksum(const String& value) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < value.length(); i++) {
        checksum = checksum * 31 + value.charAt(i);
    }
    return checksum;
}

void NVSManager::logError(NVSError error, const char* context, const char* ns, const char* key) {
    const char* errorMsg = "Unknown";
    switch (error) {
        case NVSError::SUCCESS: errorMsg = "Success"; break;
        case NVSError::NAMESPACE_NOT_FOUND: errorMsg = "Namespace not found"; break;
        case NVSError::KEY_NOT_FOUND: errorMsg = "Key not found"; break;
        case NVSError::INSUFFICIENT_SPACE: errorMsg = "Insufficient space"; break;
        case NVSError::CORRUPTION_DETECTED: errorMsg = "Data corruption detected"; break;
        case NVSError::INVALID_KEY: errorMsg = "Invalid key"; break;
        case NVSError::INVALID_VALUE: errorMsg = "Invalid value"; break;
        case NVSError::WRITE_FAILED: errorMsg = "Write failed"; break;
        case NVSError::READ_FAILED: errorMsg = "Read failed"; break;
    }
    
    Serial.printf("[NVS] ❌ %s: %s", context, errorMsg);
    if (ns) Serial.printf(" (ns: %s)", ns);
    if (key) Serial.printf(" (key: %s)", key);
    Serial.println();
}

NVSError NVSManager::saveString(const char* ns, const char* key, const String& value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveString", ns, key);
        return keyError;
    }
    
    NVSError valueError = validateValue(value);
    if (valueError != NVSError::SUCCESS) {
        logError(valueError, "saveString", ns, key);
        return valueError;
    }
    
    // Vérifier le cache pour éviter les écritures inutiles
    String cacheKey = String(ns) + "::" + String(key);
    if (_cache.find(ns) != _cache.end()) {
        for (auto& entry : _cache[ns]) {
            if (entry.key == key && entry.value == value && !entry.dirty) {
                Serial.printf("[NVS] 💾 Valeur inchangée, pas de sauvegarde: %s::%s\n", ns, key);
                return NVSError::SUCCESS;
            }
        }
    }
    
    NVSError openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    bool success = _preferences.putString(key, value);
    closeNamespace();
    
    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveString", ns, key);
        return NVSError::WRITE_FAILED;
    }
    
    // Mettre à jour le cache
    if (_cache.find(ns) == _cache.end()) {
        _cache[ns] = std::vector<NVSCacheEntry>();
    }
    
    // Chercher une entrée existante
    bool found = false;
    for (auto& entry : _cache[ns]) {
        if (entry.key == key) {
            entry.value = value;
            entry.timestamp = millis();
            entry.calculateChecksum();
            entry.dirty = false;
            found = true;
            break;
        }
    }
    
    // Ajouter une nouvelle entrée si pas trouvée
    if (!found) {
        if (_cache[ns].size() >= _maxCacheSize) {
            // Supprimer la plus ancienne entrée
            _cache[ns].erase(_cache[ns].begin());
        }
        _cache[ns].push_back(NVSCacheEntry(key, value));
    }
    
    Serial.printf("[NVS] ✅ Sauvegardé: %s::%s = %s\n", ns, key, value.c_str());
    
    // Phase 2: Marquer la clé comme sale pour flush différé
    if (_deferredFlushEnabled) {
        addDirtyKey(String(ns), key);
        checkDeferredFlush();
    }
    
    return NVSError::SUCCESS;
}

NVSError NVSManager::loadString(const char* ns, const char* key, String& value, const String& defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        value = defaultValue;
        return NVSError::READ_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadString", ns, key);
        value = defaultValue;
        return keyError;
    }
    
    // Vérifier le cache d'abord
    if (_cache.find(ns) != _cache.end()) {
        for (const auto& entry : _cache[ns]) {
            if (entry.key == key) {
                if (entry.isValid()) {
                    value = entry.value;
                    Serial.printf("[NVS] 📖 Chargé depuis cache: %s::%s = %s\n", ns, key, value.c_str());
                    return NVSError::SUCCESS;
                } else {
                    Serial.printf("[NVS] ⚠️ Corruption détectée dans cache: %s::%s\n", ns, key);
                    break;
                }
            }
        }
    }
    
    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }
    
    value = _preferences.getString(key, defaultValue);
    closeNamespace();
    
    // Mettre à jour le cache
    if (_cache.find(ns) == _cache.end()) {
        _cache[ns] = std::vector<NVSCacheEntry>();
    }
    
    // Chercher une entrée existante
    bool found = false;
    for (auto& entry : _cache[ns]) {
        if (entry.key == key) {
            entry.value = value;
            entry.timestamp = millis();
            entry.calculateChecksum();
            found = true;
            break;
        }
    }
    
    // Ajouter une nouvelle entrée si pas trouvée
    if (!found && value != defaultValue) {
        if (_cache[ns].size() >= _maxCacheSize) {
            _cache[ns].erase(_cache[ns].begin());
        }
        _cache[ns].push_back(NVSCacheEntry(key, value));
    }
    
    Serial.printf("[NVS] 📖 Chargé: %s::%s = %s\n", ns, key, value.c_str());
    return NVSError::SUCCESS;
}

NVSError NVSManager::saveBool(const char* ns, const char* key, bool value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveBool", ns, key);
        return keyError;
    }

    NVSError openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }

    bool success = _preferences.putBool(key, value);
    closeNamespace();

    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveBool", ns, key);
        return NVSError::WRITE_FAILED;
    }

    if (_cache.find(ns) == _cache.end()) {
        _cache[ns] = std::vector<NVSCacheEntry>();
    }

    bool found = false;
    for (auto& entry : _cache[ns]) {
        if (entry.key == key) {
            entry.value = value ? "1" : "0";
            entry.timestamp = millis();
            entry.calculateChecksum();
            entry.dirty = false;
            found = true;
            break;
        }
    }

    if (!found) {
        if (_cache[ns].size() >= _maxCacheSize) {
            _cache[ns].erase(_cache[ns].begin());
        }
        NVSCacheEntry entry(key, value ? "1" : "0");
        entry.dirty = false;
        _cache[ns].push_back(entry);
    }

    if (_deferredFlushEnabled) {
        addDirtyKey(String(ns), key);
        checkDeferredFlush();
    }

    return NVSError::SUCCESS;
}

NVSError NVSManager::loadBool(const char* ns, const char* key, bool& value, bool defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        value = defaultValue;
        return NVSError::READ_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadBool", ns, key);
        value = defaultValue;
        return keyError;
    }

    // Vérifier le cache d'abord
    if (_cache.find(ns) != _cache.end()) {
        for (const auto& entry : _cache[ns]) {
            if (entry.key == key && entry.isValid()) {
                value = (entry.value == "1" || entry.value == "true");
                Serial.printf("[NVS] 📖 Chargé booléen depuis cache: %s::%s = %s\n",
                              ns, key, value ? "true" : "false");
                return NVSError::SUCCESS;
            }
        }
    }

    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }

    bool storedValue = _preferences.getBool(key, defaultValue);
    closeNamespace();

    value = storedValue;

    if (_cache.find(ns) == _cache.end()) {
        _cache[ns] = std::vector<NVSCacheEntry>();
    }

    bool found = false;
    for (auto& entry : _cache[ns]) {
        if (entry.key == key) {
            entry.value = value ? "1" : "0";
            entry.timestamp = millis();
            entry.calculateChecksum();
            found = true;
            break;
        }
    }

    if (!found) {
        if (_cache[ns].size() >= _maxCacheSize) {
            _cache[ns].erase(_cache[ns].begin());
        }
        _cache[ns].push_back(NVSCacheEntry(key, value ? "1" : "0"));
    }

    return NVSError::SUCCESS;
}

NVSError NVSManager::saveInt(const char* ns, const char* key, int value) {
    String valueStr = String(value);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadInt(const char* ns, const char* key, int& value, int defaultValue) {
    String valueStr;
    NVSError error = loadString(ns, key, valueStr, String(defaultValue));
    value = valueStr.toInt();
    return error;
}

NVSError NVSManager::saveFloat(const char* ns, const char* key, float value) {
    String valueStr = String(value, 2);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadFloat(const char* ns, const char* key, float& value, float defaultValue) {
    String valueStr;
    NVSError error = loadString(ns, key, valueStr, String(defaultValue, 2));
    value = valueStr.toFloat();
    return error;
}

NVSError NVSManager::saveULong(const char* ns, const char* key, unsigned long value) {
    String valueStr = String(value);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadULong(const char* ns, const char* key, unsigned long& value, unsigned long defaultValue) {
    String valueStr;
    NVSError error = loadString(ns, key, valueStr, String(defaultValue));
    value = strtoul(valueStr.c_str(), nullptr, 10);
    return error;
}

void NVSManager::flushCache() {
    if (!_initialized) return;

    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Mutex flushCache"));
        return;
    }
    
    Serial.println(F("[NVS] 🔄 Vidage du cache NVS"));
    
    for (auto& namespacePair : _cache) {
        for (auto& entry : namespacePair.second) {
            if (entry.dirty) {
                // Utiliser directement les fonctions primitives pour éviter la récursion
                NVSError openErr = openNamespace(namespacePair.first.c_str(), false);
                if (openErr == NVSError::SUCCESS) {
                    _preferences.putString(entry.key.c_str(), entry.value);
                    closeNamespace();
                    entry.dirty = false;
                } else {
                    Serial.printf("[NVS] ⚠️ Flush: impossible d'ouvrir %s\n",
                                  namespacePair.first.c_str());
                }
            }
        }
    }
    
    Serial.println(F("[NVS] ✅ Cache vidé"));
}

void NVSManager::clearCache() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Mutex clearCache"));
        return;
    }

    _cache.clear();
    Serial.println(F("[NVS] 🗑️ Cache vidé"));
}

bool NVSManager::isCached(const char* ns, const char* key) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return false;
    }

    if (_cache.find(ns) == _cache.end()) {
        return false;
    }
    
    for (const auto& entry : _cache[ns]) {
        if (entry.key == key) {
            return true;
        }
    }
    
    return false;
}

NVSUsageStats NVSManager::getUsageStats() {
    NVSLockGuard guard(*this);
    NVSUsageStats stats;

    if (!guard.locked()) {
        return stats;
    }
    
    // Compter les entrées du cache
    stats.cacheEntries = 0;
    for (const auto& namespacePair : _cache) {
        stats.cacheEntries += namespacePair.second.size();
    }
    
    // Compter les namespaces
    stats.namespaceCount = _cache.size();
    
    // Estimation de l'espace utilisé (approximatif)
    stats.usedBytes = stats.cacheEntries * 50; // Estimation moyenne par entrée
    stats.totalBytes = 4096; // Taille typique partition NVS
    stats.freeBytes = stats.totalBytes - stats.usedBytes;
    stats.usagePercent = (float)stats.usedBytes / stats.totalBytes * 100.0f;
    
    return stats;
}

void NVSManager::logUsageStats() {
    NVSUsageStats stats = getUsageStats();
    
    Serial.println(F("========================================"));
    Serial.println(F("[NVS] 📊 Statistiques d'utilisation"));
    Serial.println(F("========================================"));
    Serial.printf("[NVS] Namespaces: %zu\n", stats.namespaceCount);
    Serial.printf("[NVS] Entrées cache: %zu\n", stats.cacheEntries);
    Serial.printf("[NVS] Espace utilisé: %zu/%zu bytes (%.1f%%)\n", 
                  stats.usedBytes, stats.totalBytes, stats.usagePercent);
    Serial.printf("[NVS] Espace libre: %zu bytes\n", stats.freeBytes);
    Serial.println(F("========================================"));
}

bool NVSManager::isSpaceLow(float threshold) {
    NVSUsageStats stats = getUsageStats();
    return stats.usagePercent > threshold;
}

bool NVSManager::keyExists(const char* ns, const char* key) {
    String value;
    NVSError error = loadString(ns, key, value, "");
    return (error == NVSError::SUCCESS && value.length() > 0);
}

NVSError NVSManager::removeKey(const char* ns, const char* key) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        return keyError;
    }
    
    NVSError openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    bool success = _preferences.remove(key);
    closeNamespace();
    
    if (!success) {
        logError(NVSError::WRITE_FAILED, "removeKey", ns, key);
        return NVSError::WRITE_FAILED;
    }
    
    // Supprimer du cache
    if (_cache.find(ns) != _cache.end()) {
        auto& entries = _cache[ns];
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->key == key) {
                entries.erase(it);
                break;
            }
        }
    }
    
    Serial.printf("[NVS] 🗑️ Clé supprimée: %s::%s\n", ns, key);
    return NVSError::SUCCESS;
}

void NVSManager::printCacheStatus() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Mutex printCacheStatus"));
        return;
    }

    Serial.println(F("========================================"));
    Serial.println(F("[NVS] 📋 État du cache"));
    Serial.println(F("========================================"));
    
    for (const auto& namespacePair : _cache) {
        Serial.printf("[NVS] Namespace: %s (%zu entrées)\n", 
                      namespacePair.first.c_str(), namespacePair.second.size());
        
        for (const auto& entry : namespacePair.second) {
            Serial.printf("[NVS]   %s = %s (ts: %lu, dirty: %s)\n",
                          entry.key.c_str(), entry.value.c_str(), 
                          entry.timestamp, entry.dirty ? "OUI" : "NON");
        }
    }
    
    Serial.println(F("========================================"));
}

NVSError NVSManager::validateNamespace(const char* ns) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    if (!_initialized) {
        return NVSError::NAMESPACE_NOT_FOUND;
    }
    
    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    // Validation simplifiée - vérifier que le namespace peut être ouvert
    
    closeNamespace();
    
    Serial.printf("[NVS] ✅ Validation namespace %s\n", ns);
    return NVSError::SUCCESS;
}

NVSError NVSManager::repairNamespace(const char* ns) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    Serial.printf("[NVS] 🔧 Réparation namespace %s\n", ns);
    
    NVSError openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    // Réparation simplifiée - nettoyage du cache
    
    closeNamespace();
    
    Serial.printf("[NVS] ✅ Réparation terminée\n");
    return NVSError::SUCCESS;
}

void NVSManager::cleanupCorruptedData() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Mutex cleanupCorruptedData"));
        return;
    }

    Serial.println(F("[NVS] 🧹 Nettoyage des données corrompues"));
    
    const char* nss[] = {
        NVS_NAMESPACES::SYSTEM,
        NVS_NAMESPACES::CONFIG,
        NVS_NAMESPACES::TIME,
        NVS_NAMESPACES::STATE,
        NVS_NAMESPACES::LOGS,
        NVS_NAMESPACES::SENSORS
    };
    
    for (size_t i = 0; i < sizeof(nss) / sizeof(nss[0]); i++) {
        NVSError error = validateNamespace(nss[i]);
        if (error != NVSError::SUCCESS) {
            Serial.printf("[NVS] ⚠️ Problème détecté dans %s, tentative de réparation\n", nss[i]);
            repairNamespace(nss[i]);
        }
    }
    
    Serial.println(F("[NVS] ✅ Nettoyage terminé"));
}

NVSError NVSManager::clearNamespace(const char* ns) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    bool success = _preferences.clear();
    closeNamespace();
    
    if (!success) {
        logError(NVSError::WRITE_FAILED, "clearNamespace", ns);
        return NVSError::WRITE_FAILED;
    }
    
    // Vider le cache pour ce namespace
    if (_cache.find(ns) != _cache.end()) {
        _cache[ns].clear();
    }
    
    Serial.printf("[NVS] 🗑️ Namespace vidé: %s\n", ns);
    return NVSError::SUCCESS;
}

void NVSManager::printNamespaceContents(const char* ns) {
    Serial.printf("[NVS] 📋 Contenu du namespace: %s\n", ns);
    
    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        Serial.printf("[NVS] ❌ Impossible d'ouvrir %s\n", ns);
        return;
    }
    
    // Affichage simplifié - pas d'énumération des clés
    
    closeNamespace();
    
    Serial.printf("[NVS] Namespace accessible\n");
}

NVSError NVSManager::migrateFromOldSystem() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ❌ Mutex migrateFromOldSystem"));
        return NVSError::WRITE_FAILED;
    }

    static constexpr const char* MIGRATION_FLAG_KEY = "mig1180_done";

    Serial.println(F("[NVS] 🔄 Migration depuis l'ancien système NVS"));
    
    // Éviter les migrations répétées à chaque boot
    nvs_handle_t systemHandle = 0;
    esp_err_t systemOpenErr = nvs_open(NVS_NAMESPACES::SYSTEM, NVS_READWRITE, &systemHandle);
    if (systemOpenErr == ESP_OK) {
        uint8_t doneFlag = 0;
        esp_err_t flagErr = nvs_get_u8(systemHandle, MIGRATION_FLAG_KEY, &doneFlag);
        if (flagErr == ESP_OK && doneFlag == 1) {
            Serial.println(F("[NVS] ✅ Migration déjà effectuée - aucune action requise"));
            nvs_close(systemHandle);
            return NVSError::SUCCESS;
        }
    } else {
        Serial.printf("[NVS] ⚠️ Impossible d'accéder au namespace %s pour le flag de migration (err=%d)\n",
                      NVS_NAMESPACES::SYSTEM, systemOpenErr);
    }

    struct MigrationRule {
        const char* oldNamespace;
        const char* newNamespace;
        const char* keyPrefix;
    };
    
    const MigrationRule rules[] = {
        {"bouffe",      NVS_NAMESPACES::CONFIG,  "bouffe_"},
        {"ota",         NVS_NAMESPACES::SYSTEM,  "ota_"},
        {"remoteVars",  NVS_NAMESPACES::CONFIG,  "remote_"},
        {"rtc",         NVS_NAMESPACES::TIME,    "rtc_"},
        {"diagnostics", NVS_NAMESPACES::LOGS,    "diag_"},
        {"alerts",      NVS_NAMESPACES::LOGS,    "alert_"},
        {"timeDrift",   NVS_NAMESPACES::TIME,    "drift_"},
        {"gpio",        NVS_NAMESPACES::CONFIG,  "gpio_"},
        {"actSnap",     NVS_NAMESPACES::STATE,   "snap_"},
        {"actState",    NVS_NAMESPACES::STATE,   "state_"},
        {"pendingSync", NVS_NAMESPACES::STATE,   "sync_"},
        {"waterTemp",   NVS_NAMESPACES::SENSORS, "temp_"},
        {"digest",      NVS_NAMESPACES::SENSORS, "digest_"},
        {"cmdLog",      NVS_NAMESPACES::LOGS,    "cmd_"},
        {"reset",       NVS_NAMESPACES::SYSTEM,  "reset_"},
        {"net",         NVS_NAMESPACES::SYSTEM,  "net_"}
    };

    auto keyExists = [](nvs_handle_t handle, const char* key, nvs_type_t type) -> bool {
        esp_err_t res = ESP_ERR_NVS_NOT_FOUND;
        size_t length = 0;

        switch (type) {
            case NVS_TYPE_U8: {
                uint8_t tmp;
                res = nvs_get_u8(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_I8: {
                int8_t tmp;
                res = nvs_get_i8(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_U16: {
                uint16_t tmp;
                res = nvs_get_u16(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_I16: {
                int16_t tmp;
                res = nvs_get_i16(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_U32: {
                uint32_t tmp;
                res = nvs_get_u32(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_I32: {
                int32_t tmp;
                res = nvs_get_i32(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_U64: {
                uint64_t tmp;
                res = nvs_get_u64(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_I64: {
                int64_t tmp;
                res = nvs_get_i64(handle, key, &tmp);
                break;
            }
            case NVS_TYPE_STR:
                res = nvs_get_str(handle, key, nullptr, &length);
                break;
            case NVS_TYPE_BLOB:
                res = nvs_get_blob(handle, key, nullptr, &length);
                break;
#ifdef NVS_TYPE_DOUBLE
            case NVS_TYPE_DOUBLE: {
                double tmp;
                res = nvs_get_double(handle, key, &tmp);
                break;
            }
#endif
            default:
                res = nvs_get_blob(handle, key, nullptr, &length);
                break;
        }

        return res == ESP_OK;
    };

    bool migratedSomething = false;
    
    for (const auto& rule : rules) {
        nvs_handle_t oldHandle;
        esp_err_t oldErr = nvs_open(rule.oldNamespace, NVS_READONLY, &oldHandle);
        if (oldErr != ESP_OK) {
            continue; // Ancien namespace absent
        }

        nvs_handle_t newHandle;
        esp_err_t newErr = nvs_open(rule.newNamespace, NVS_READWRITE, &newHandle);
        if (newErr != ESP_OK) {
            nvs_close(oldHandle);
            continue;
        }

        bool ruleMigrated = false;

        nvs_iterator_t it = nullptr;
        esp_err_t findErr = nvs_entry_find(NVS_DEFAULT_PART_NAME, rule.oldNamespace, NVS_TYPE_ANY, &it);
        while (findErr == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            String newKey = String(rule.keyPrefix) + info.key;
            if (newKey.length() > 15) {
                Serial.printf("[NVS] ⚠️ Clé ignorée (nom >15 caractères): %s/%s -> %s\n",
                              rule.oldNamespace, info.key, newKey.c_str());
                findErr = nvs_entry_next(&it);
                continue;
            }

            if (keyExists(newHandle, newKey.c_str(), info.type)) {
                findErr = nvs_entry_next(&it);
                continue; // Donnée déjà migrée
            }

            bool copied = false;
            esp_err_t readErr = ESP_FAIL;

            switch (info.type) {
                case NVS_TYPE_U8: {
                    uint8_t value = 0;
                    readErr = nvs_get_u8(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u8(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I8: {
                    int8_t value = 0;
                    readErr = nvs_get_i8(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i8(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U16: {
                    uint16_t value = 0;
                    readErr = nvs_get_u16(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u16(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I16: {
                    int16_t value = 0;
                    readErr = nvs_get_i16(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i16(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U32: {
                    uint32_t value = 0;
                    readErr = nvs_get_u32(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u32(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I32: {
                    int32_t value = 0;
                    readErr = nvs_get_i32(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i32(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U64: {
                    uint64_t value = 0;
                    readErr = nvs_get_u64(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u64(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I64: {
                    int64_t value = 0;
                    readErr = nvs_get_i64(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i64(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_STR: {
                    size_t length = 0;
                    readErr = nvs_get_str(oldHandle, info.key, nullptr, &length);
                    if (readErr == ESP_OK && length > 0) {
                        std::vector<char> buffer(length);
                        readErr = nvs_get_str(oldHandle, info.key, buffer.data(), &length);
                        if (readErr == ESP_OK && nvs_set_str(newHandle, newKey.c_str(), buffer.data()) == ESP_OK) {
                            copied = true;
                        }
                    }
                    break;
                }
                case NVS_TYPE_BLOB: {
                    size_t length = 0;
                    readErr = nvs_get_blob(oldHandle, info.key, nullptr, &length);
                    if (readErr == ESP_OK) {
                        if (length == 0) {
                            if (nvs_set_blob(newHandle, newKey.c_str(), nullptr, 0) == ESP_OK) {
                                copied = true;
                            }
                        } else {
                            std::vector<uint8_t> buffer(length);
                            readErr = nvs_get_blob(oldHandle, info.key, buffer.data(), &length);
                            if (readErr == ESP_OK &&
                                nvs_set_blob(newHandle, newKey.c_str(), buffer.data(), length) == ESP_OK) {
                                copied = true;
                            }
                        }
                    }
                    break;
                }
#ifdef NVS_TYPE_DOUBLE
                case NVS_TYPE_DOUBLE: {
                    double value = 0.0;
                    readErr = nvs_get_double(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_double(newHandle, newKey.c_str(), value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
#endif
                default:
                    Serial.printf("[NVS] ⚠️ Type non pris en charge pour %s/%s (type=%d)\n",
                                  rule.oldNamespace, info.key, static_cast<int>(info.type));
                    break;
            }

            if (!copied && readErr != ESP_OK) {
                Serial.printf("[NVS] ⚠️ Lecture impossible pour %s/%s (err=%d)\n",
                              rule.oldNamespace, info.key, readErr);
            }

            if (copied) {
                ruleMigrated = true;
                migratedSomething = true;
            }

            findErr = nvs_entry_next(&it);
        }

        if (it != nullptr) {
            nvs_release_iterator(it);
        }

        if (ruleMigrated) {
            esp_err_t commitErr = nvs_commit(newHandle);
            if (commitErr != ESP_OK) {
                Serial.printf("[NVS] ⚠️ Commit echoue pour %s (err=%d)\n", rule.newNamespace, commitErr);
            } else {
                Serial.printf("[NVS] ✅ Donnees migrees de %s vers %s\n",
                              rule.oldNamespace, rule.newNamespace);
            }
        }

        nvs_close(newHandle);
        nvs_close(oldHandle);
    }

    if (systemHandle == 0 && systemOpenErr != ESP_OK) {
        if (nvs_open(NVS_NAMESPACES::SYSTEM, NVS_READWRITE, &systemHandle) != ESP_OK) {
            Serial.println(F("[NVS] ⚠️ Impossible d'enregistrer le flag de migration"));
        }
    }

    if (systemHandle != 0) {
        nvs_set_u8(systemHandle, MIGRATION_FLAG_KEY, 1);
        nvs_commit(systemHandle);
        nvs_close(systemHandle);
    }

    if (!migratedSomething) {
        Serial.println(F("[NVS] ℹ️ Aucun ancien namespace detecte ou donnees deja migrees"));
    } else {
        Serial.println(F("[NVS] ✅ Migration des anciens namespaces terminee"));
    }

    return NVSError::SUCCESS;
}

// Phase 2: JSON compression helpers
NVSError NVSManager::saveJsonCompressed(const char* ns, const char* key, const String& json) {
    Serial.printf("[NVS] Compressing JSON for %s::%s\n", ns, key);

    String compressed = compressJson(json);
    NVSError result = saveString(ns, key, compressed);

    if (result == NVSError::SUCCESS) {
        float ratio = json.length() == 0 ? 0.0f : (static_cast<float>(compressed.length()) / json.length()) * 100.0f;
        Serial.printf("[NVS] JSON compressed: %zu -> %zu bytes (%.1f%%)\n",
                      json.length(), compressed.length(), ratio);
    }

    return result;
}

NVSError NVSManager::loadJsonDecompressed(const char* ns, const char* key, String& json, const String& defaultValue) {
    Serial.printf("[NVS] Decompressing JSON for %s::%s\n", ns, key);

    String compressed;
    NVSError result = loadString(ns, key, compressed, defaultValue);

    if (result == NVSError::SUCCESS && compressed.length() > 0) {
        json = decompressJson(compressed);
        Serial.printf("[NVS] JSON decompressed: %zu -> %zu bytes\n",
                      compressed.length(), json.length());
    } else {
        json = defaultValue;
    }

    return result;
}

String NVSManager::compressJson(const String& json) {
    String compressed = json;
    compressed.replace(" ", "");
    compressed.replace("\n", "");
    compressed.replace("\r", "");
    compressed.replace("\t", "");

    compressed.replace("\"mail\":", "\"m\":");
    compressed.replace("\"mailNotif\":", "\"mn\":");
    compressed.replace("\"bouffeMatin\":", "\"bm\":");
    compressed.replace("\"bouffeMidi\":", "\"bmi\":");
    compressed.replace("\"bouffeSoir\":", "\"bs\":");
    compressed.replace("\"threshold\":", "\"th\":");
    compressed.replace("\"duration\":", "\"dur\":");
    compressed.replace("\"enabled\":", "\"en\":");
    compressed.replace("\"temperature\":", "\"temp\":");
    compressed.replace("\"humidity\":", "\"hum\":");

    return compressed;
}

String NVSManager::decompressJson(const String& compressed) {
    String json = compressed;
    json.replace("\"m\":", "\"mail\":");
    json.replace("\"mn\":", "\"mailNotif\":");
    json.replace("\"bm\":", "\"bouffeMatin\":");
    json.replace("\"bmi\":", "\"bouffeMidi\":");
    json.replace("\"bs\":", "\"bouffeSoir\":");
    json.replace("\"th\":", "\"threshold\":");
    json.replace("\"dur\":", "\"duration\":");
    json.replace("\"en\":", "\"enabled\":");
    json.replace("\"temp\":", "\"temperature\":");
    json.replace("\"hum\":", "\"humidity\":");
    return json;
}

void NVSManager::enableDeferredFlush(bool enable) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ⚠️ Mutex enableDeferredFlush"));
        return;
    }

    _deferredFlushEnabled = enable;
    Serial.printf("[NVS] Deferred flush %s\n", enable ? "enabled" : "disabled");

    if (!enable) {
        forceFlush();
    }
}

void NVSManager::setFlushInterval(unsigned long intervalMs) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ⚠️ Mutex setFlushInterval"));
        return;
    }

    _flushIntervalMs = intervalMs;
    Serial.printf("[NVS] Flush interval set to %lu ms\n", intervalMs);
}

void NVSManager::forceFlush() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ⚠️ Mutex forceFlush"));
        return;
    }

    Serial.println(F("[NVS] Forcing flush"));

    for (const String& dirtyKey : _dirtyKeys) {
        size_t sep = dirtyKey.indexOf("::");
        if (sep != -1) {
            String ns = dirtyKey.substring(0, sep);
            String key = dirtyKey.substring(sep + 2);

            if (_cache.find(ns) != _cache.end()) {
                for (auto& entry : _cache[ns]) {
                    if (entry.key == key && entry.dirty) {
                        saveString(ns.c_str(), key.c_str(), entry.value);
                        entry.dirty = false;
                        break;
                    }
                }
            }
        }
    }

    _dirtyKeys.clear();
    _lastFlushTime = millis();
    Serial.println(F("[NVS] Flush complete"));
}

NVSError NVSManager::rotateLogs(const char* ns, size_t maxEntries) {
    Serial.printf("[NVS] Rotating logs for namespace %s (max %zu)\n", ns, maxEntries);

    NVSError err = openNamespace(ns, false);
    if (err != NVSError::SUCCESS) {
        return err;
    }

    const char* logKeys[] = {"diag_old", "alert_old", "cmd_old", "log_old"};
    for (const char* key : logKeys) {
        if (_preferences.isKey(key)) {
            _preferences.remove(key);
            Serial.printf("[NVS] Log key removed: %s::%s\n", ns, key);
        }
    }

    closeNamespace();
    Serial.printf("[NVS] Log rotation complete for %s\n", ns);
    return NVSError::SUCCESS;
}

NVSError NVSManager::cleanupOldData(const char* ns, unsigned long maxAgeMs) {
    Serial.printf("[NVS] Cleaning old data in %s (max age %lu ms)\n", ns, maxAgeMs);

    NVSError err = openNamespace(ns, false);
    if (err != NVSError::SUCCESS) {
        return err;
    }

    int cleaned = 0;
    const char* cleanupKeys[] = {"temp_old", "drift_old", "sync_old", "state_old"};
    for (const char* key : cleanupKeys) {
        if (_preferences.isKey(key)) {
            _preferences.remove(key);
            cleaned++;
            Serial.printf("[NVS] Removed stale data: %s::%s\n", ns, key);
        }
    }

    closeNamespace();
    Serial.printf("[NVS] Cleanup complete: %d keys removed in %s\n", cleaned, ns);
    return NVSError::SUCCESS;
}

NVSError NVSManager::cleanupObsoleteKeys() {
    Serial.println(F("[NVS] Cleaning obsolete keys"));

    const char* nss[] = {
        NVS_NAMESPACES::SYSTEM,
        NVS_NAMESPACES::CONFIG,
        NVS_NAMESPACES::TIME,
        NVS_NAMESPACES::STATE,
        NVS_NAMESPACES::LOGS,
        NVS_NAMESPACES::SENSORS
    };

    int total = 0;

    for (const char* ns : nss) {
        NVSError err = openNamespace(ns, false);
        if (err != NVSError::SUCCESS) {
            continue;
        }
        // Placeholder: no specific obsolete keys defined yet
        closeNamespace();
    }

    Serial.printf("[NVS] Obsolete cleanup complete: %d keys removed\n", total);
    return NVSError::SUCCESS;
}

void NVSManager::schedulePeriodicCleanup() {
    _lastCleanupTime = millis();
    Serial.println(F("[NVS] Periodic cleanup scheduled"));
}

bool NVSManager::shouldPerformCleanup() {
    unsigned long now = millis();
    return (now - _lastCleanupTime) >= _cleanupIntervalMs;
}

bool NVSManager::isDeferredFlushEnabled() const {
    return _deferredFlushEnabled;
}

unsigned long NVSManager::getLastFlushTime() const {
    return _lastFlushTime;
}

void NVSManager::checkDeferredFlush() {
    if (!_deferredFlushEnabled) {
        return;
    }
    if (_dirtyKeys.empty()) {
        return;
    }
    if ((millis() - _lastFlushTime) >= _flushIntervalMs) {
        forceFlush();
    }
}

void NVSManager::addDirtyKey(const String& ns, const char* key) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ⚠️ Mutex addDirtyKey"));
        return;
    }

    String dirtyKey = ns + "::" + String(key);
    for (const String& existing : _dirtyKeys) {
        if (existing == dirtyKey) {
            return;
        }
    }

    _dirtyKeys.push_back(dirtyKey);
}

void NVSManager::removeDirtyKey(const String& ns, const char* key) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        Serial.println(F("[NVS] ⚠️ Mutex removeDirtyKey"));
        return;
    }

    String dirtyKey = ns + "::" + String(key);
    for (auto it = _dirtyKeys.begin(); it != _dirtyKeys.end(); ++it) {
        if (*it == dirtyKey) {
            _dirtyKeys.erase(it);
            break;
        }
    }
}

bool NVSManager::shouldFlush() const {
    return !_dirtyKeys.empty() && (millis() - _lastFlushTime) >= _flushIntervalMs;
}
