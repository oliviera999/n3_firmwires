#include "nvs_manager.h"
#include "config.h"
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

class NVSLockGuard {
public:
    explicit NVSLockGuard(NVSManager& manager, TickType_t timeout = pdMS_TO_TICKS(100))
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
    , _mutex(xSemaphoreCreateRecursiveMutex()) {
    if (_mutex == nullptr) {
        Serial.println(F("[NVS] ❌ Échec création mutex"));
    }
}

NVSManager::~NVSManager() {
    if (_initialized) {
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

    // Marquer initialisé avant la création des namespaces (openNamespace en dépend)
    _initialized = true;

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

NVSError NVSManager::saveString(const char* ns, const char* key, const char* value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    if (value == nullptr) {
        value = "";
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveString", ns, key);
        return keyError;
    }
    
    // Vérifier si la valeur a changé avant d'écrire (évite écritures inutiles)
    // Buffer sur la stack - taille modérée (384 bytes en test), mutex NVS protège l'accès
    NVSError openError = openNamespace(ns, true);  // Ouvrir en lecture d'abord
    if (openError == NVSError::SUCCESS) {
        char currentValue[NVSConfig::MAX_INSPECTED_STRING_BYTES];
        size_t len = _preferences.getString(key, currentValue, sizeof(currentValue));
        if (len < sizeof(currentValue)) {
            currentValue[len] = '\0';
        } else {
            currentValue[sizeof(currentValue) - 1] = '\0';
        }
        closeNamespace();
        if (strcmp(currentValue, value) == 0) {
            // Valeur inchangée, pas besoin d'écrire
            return NVSError::SUCCESS;
        }
    }
    
    // Écrire la nouvelle valeur
    openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }
    
    bool success = _preferences.putString(key, value);
    closeNamespace();
    
    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveString", ns, key);
        return NVSError::WRITE_FAILED;
    }
    
    return NVSError::SUCCESS;
}

NVSError NVSManager::loadString(const char* ns, const char* key, char* value, size_t valueSize, const char* defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return NVSError::READ_FAILED;
    }

    if (value == nullptr || valueSize == 0) {
        return NVSError::INVALID_VALUE;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadString", ns, key);
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return keyError;
    }
    
    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return openError;
    }
    
    // NOTE: Preferences::getString() retourne String Arduino (limitation de l'API ESP32)
    // La String est copiée immédiatement dans le buffer et détruite pour minimiser fragmentation
    String tempValueStr = _preferences.getString(key, defaultValue ? defaultValue : "");
    if (tempValueStr.length() > 0) {
      size_t copyLen = tempValueStr.length() < valueSize - 1 ? tempValueStr.length() : valueSize - 1;
      strncpy(value, tempValueStr.c_str(), copyLen);
      value[copyLen] = '\0';
    } else {
      value[0] = '\0';
    }
    closeNamespace();
    
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

  // Vérifier si la valeur a réellement changé avant d'écrire
  NVSError openError = openNamespace(ns, true);
  if (openError == NVSError::SUCCESS) {
    bool current = _preferences.getBool(key, value);
    closeNamespace();
    if (current == value) {
      // Valeur inchangée, pas d'écriture pour préserver la flash
      return NVSError::SUCCESS;
    }
  }

  // Écrire uniquement si la valeur est différente ou si la lecture précédente a échoué
  openError = openNamespace(ns, false);
  if (openError != NVSError::SUCCESS) {
    return openError;
  }

  bool success = _preferences.putBool(key, value);
  closeNamespace();

  if (!success) {
    logError(NVSError::WRITE_FAILED, "saveBool", ns, key);
    return NVSError::WRITE_FAILED;
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

    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }

    value = _preferences.getBool(key, defaultValue);
    closeNamespace();

    return NVSError::SUCCESS;
}

NVSError NVSManager::saveInt(const char* ns, const char* key, int value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveInt", ns, key);
        return keyError;
    }

    // Convertir en String et appeler saveString()
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%d", value);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadInt(const char* ns, const char* key, int& value, int defaultValue) {
    char valueStr[16];
    char defaultValueStr[16];
    snprintf(defaultValueStr, sizeof(defaultValueStr), "%d", defaultValue);
    NVSError error = loadString(ns, key, valueStr, sizeof(valueStr), defaultValueStr);
    value = atoi(valueStr);
    return error;
}

NVSError NVSManager::saveFloat(const char* ns, const char* key, float value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveFloat", ns, key);
        return keyError;
    }

    // Convertir en String et appeler saveString()
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%.2f", value);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadFloat(const char* ns, const char* key, float& value, float defaultValue) {
    char valueStr[16];
    char defaultValueStr[16];
    snprintf(defaultValueStr, sizeof(defaultValueStr), "%.2f", defaultValue);
    NVSError error = loadString(ns, key, valueStr, sizeof(valueStr), defaultValueStr);
    value = atof(valueStr);
    return error;
}

NVSError NVSManager::saveULong(const char* ns, const char* key, unsigned long value) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        return NVSError::WRITE_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "saveULong", ns, key);
        return keyError;
    }

    // Convertir en String et appeler saveString()
    char valueStr[21];
    snprintf(valueStr, sizeof(valueStr), "%lu", value);
    return saveString(ns, key, valueStr);
}

NVSError NVSManager::loadULong(const char* ns, const char* key, unsigned long& value, unsigned long defaultValue) {
    char valueStr[21];
    char defaultValueStr[21];
    snprintf(defaultValueStr, sizeof(defaultValueStr), "%lu", defaultValue);
    NVSError error = loadString(ns, key, valueStr, sizeof(valueStr), defaultValueStr);
    value = strtoul(valueStr, nullptr, 10);
    return error;
}


NVSUsageStats NVSManager::getUsageStats() {
    NVSLockGuard guard(*this);
    NVSUsageStats stats;

    if (!guard.locked()) {
        return stats;
    }

  // Utiliser les statistiques réelles fournies par l'ESP-IDF
  nvs_stats_t rawStats;
  esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &rawStats);
  if (err == ESP_OK) {
    stats.totalBytes = rawStats.total_entries;
    stats.usedBytes = rawStats.used_entries;
    stats.freeBytes = rawStats.free_entries;
    stats.namespaceCount = rawStats.namespace_count;
    stats.keyCount = rawStats.used_entries;
    if (rawStats.total_entries > 0) {
      stats.usagePercent = (100.0f * static_cast<float>(rawStats.used_entries)) /
                           static_cast<float>(rawStats.total_entries);
    } else {
      stats.usagePercent = 0.0f;
    }
  } else {
    Serial.printf("[NVS] ⚠️ Impossible de lire les statistiques NVS (err=%d)\n", static_cast<int>(err));
  }

    return stats;
}

void NVSManager::logUsageStats() {
    NVSUsageStats stats = getUsageStats();
    
    Serial.println(F("========================================"));
    Serial.println(F("[NVS] 📊 Statistiques d'utilisation"));
    Serial.println(F("========================================"));
  Serial.printf("[NVS] Namespaces: %zu\n", stats.namespaceCount);
  Serial.printf("[NVS] Entrées totales: %zu, utilisées: %zu, libres: %zu (%.1f%%)\n",
                stats.totalBytes, stats.usedBytes, stats.freeBytes, stats.usagePercent);
  Serial.printf("[NVS] Clés utilisées (approx.): %zu\n", stats.keyCount);
    Serial.println(F("========================================"));
}

bool NVSManager::keyExists(const char* ns, const char* key) {
    char value[1] = {0};
    NVSError error = loadString(ns, key, value, sizeof(value), "");
    return (error == NVSError::SUCCESS && strlen(value) > 0);
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
    
    Serial.printf("[NVS] 🗑️ Clé supprimée: %s::%s\n", ns, key);
    return NVSError::SUCCESS;
}


// validateNamespace, repairNamespace, cleanupCorruptedData supprimés (non utilisés, validation redondante avec ESP-IDF)

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

            char newKey[128];
            snprintf(newKey, sizeof(newKey), "%s%s", rule.keyPrefix ? rule.keyPrefix : "", info.key);
            if (strlen(newKey) > 15) {
                Serial.printf("[NVS] ⚠️ Clé ignorée (nom >15 caractères): %s/%s -> %s\n",
                              rule.oldNamespace, info.key, newKey);
                findErr = nvs_entry_next(&it);
                continue;
            }

            if (keyExists(newHandle, newKey, info.type)) {
                findErr = nvs_entry_next(&it);
                continue; // Donnée déjà migrée
            }

            bool copied = false;
            esp_err_t readErr = ESP_FAIL;

            switch (info.type) {
                case NVS_TYPE_U8: {
                    uint8_t value = 0;
                    readErr = nvs_get_u8(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u8(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I8: {
                    int8_t value = 0;
                    readErr = nvs_get_i8(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i8(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U16: {
                    uint16_t value = 0;
                    readErr = nvs_get_u16(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u16(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I16: {
                    int16_t value = 0;
                    readErr = nvs_get_i16(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i16(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U32: {
                    uint32_t value = 0;
                    readErr = nvs_get_u32(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u32(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I32: {
                    int32_t value = 0;
                    readErr = nvs_get_i32(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i32(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_U64: {
                    uint64_t value = 0;
                    readErr = nvs_get_u64(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_u64(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_I64: {
                    int64_t value = 0;
                    readErr = nvs_get_i64(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_i64(newHandle, newKey, value) == ESP_OK) {
                        copied = true;
                    }
                    break;
                }
                case NVS_TYPE_STR: {
                    size_t length = 0;
                    readErr = nvs_get_str(oldHandle, info.key, nullptr, &length);
                    if (readErr == ESP_OK && length > 0 && length < NVSConfig::MAX_INSPECTED_STRING_BYTES) {
                        char buffer[NVSConfig::MAX_INSPECTED_STRING_BYTES];
                        readErr = nvs_get_str(oldHandle, info.key, buffer, &length);
                        if (readErr == ESP_OK && nvs_set_str(newHandle, newKey, buffer) == ESP_OK) {
                            copied = true;
                        }
                    } else if (readErr == ESP_OK && length >= NVSConfig::MAX_INSPECTED_STRING_BYTES) {
                        Serial.printf("[NVS] ⚠️ Migration STR %s trop longue (%zu), ignorée\n", info.key, length);
                    }
                    break;
                }
                case NVS_TYPE_BLOB: {
                    static constexpr size_t MAX_MIGRATION_BLOB = 2048;
                    size_t length = 0;
                    readErr = nvs_get_blob(oldHandle, info.key, nullptr, &length);
                    if (readErr == ESP_OK) {
                        if (length == 0) {
                            if (nvs_set_blob(newHandle, newKey, nullptr, 0) == ESP_OK) {
                                copied = true;
                            }
                        } else if (length <= MAX_MIGRATION_BLOB) {
                            uint8_t buffer[MAX_MIGRATION_BLOB];
                            readErr = nvs_get_blob(oldHandle, info.key, buffer, &length);
                            if (readErr == ESP_OK &&
                                nvs_set_blob(newHandle, newKey, buffer, length) == ESP_OK) {
                                copied = true;
                            }
                        } else {
                            Serial.printf("[NVS] ⚠️ Migration BLOB %s trop gros (%zu), ignoré\n", info.key, length);
                        }
                    }
                    break;
                }
#ifdef NVS_TYPE_DOUBLE
                case NVS_TYPE_DOUBLE: {
                    double value = 0.0;
                    readErr = nvs_get_double(oldHandle, info.key, &value);
                    if (readErr == ESP_OK && nvs_set_double(newHandle, newKey, value) == ESP_OK) {
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

// Méthodes obsolètes supprimées: saveJsonCompressed, loadJsonDecompressed, compressJson, decompressJson,
// enableDeferredFlush, setFlushInterval, forceFlush, checkDeferredFlush, isDeferredFlushEnabled,
// getLastFlushTime, rotateLogs, cleanupOldData, cleanupObsoleteKeys, schedulePeriodicCleanup,
// shouldPerformCleanup, addDirtyKey, removeDirtyKey, shouldFlush
// (cache, flush différé, compression JSON, rotation automatique supprimés pour simplifier)
