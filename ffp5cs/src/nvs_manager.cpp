#include "nvs_manager.h"
#include "config.h"
#include "boot_log.h"  // BOOT_LOG pour S3 PSRAM (Serial non démarré au boot)
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Mutex non disponible\n");
#else
        Serial.println(F("[NVS] ⚠️ Mutex non disponible"));
#endif
        return true; // fallback: pas de mutex, autoriser l'accès
    }
    BaseType_t taken = xSemaphoreTakeRecursive(_mutex, timeout);
    if (taken != pdPASS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Timeout mutex\n");
#else
        Serial.println(F("[NVS] ❌ Timeout mutex"));
#endif
        return false;
    }
    return true;
}

void NVSManager::unlock() {
    if (_mutex != nullptr) {
        xSemaphoreGiveRecursive(_mutex);
    }
}


// Définitions des constantes NVS_NAMESPACES (4 namespaces consolidés)
namespace NVS_NAMESPACES {
    const char* SYSTEM = "sys";      // ota, net, reset, force_wake_up, rtc_epoch
    const char* CONFIG = "cfg";      // bouffe, remoteVars, gpio, email, temp_last_ok
    const char* STATE = "state";     // actSnap, actState, pendingSync
    const char* LOGS = "logs";       // diagnostics, alerts, crash
    // NOTE: TIME et SENSORS supprimés - fusionnés dans SYSTEM et CONFIG
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
        NVSLockGuard guard(*this);
        if (guard.locked()) {
            _preferences.end();
            _initialized = false;
        }
    }
}

bool NVSManager::begin() {
    if (_initialized) {
        return true;
    }
    
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Impossible de prendre le mutex pour begin()\n");
#else
        Serial.println(F("[NVS] ❌ Impossible de prendre le mutex pour begin()"));
#endif
        return false;
    }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    BOOT_LOG("[NVS] Initialisation du gestionnaire NVS centralise\n");
#else
    Serial.println(F("[NVS] 🚀 Initialisation du gestionnaire NVS centralisé"));
#endif

    // Marquer initialisé avant la création des namespaces (openNamespace en dépend)
    _initialized = true;

    // Pré-créer les namespaces consolidés (4 namespaces)
    const char* nss[] = {
        NVS_NAMESPACES::SYSTEM,
        NVS_NAMESPACES::CONFIG,
        NVS_NAMESPACES::STATE,
        NVS_NAMESPACES::LOGS
    };
    
    for (size_t i = 0; i < sizeof(nss) / sizeof(nss[0]); i++) {
        NVSError error = openNamespace(nss[i], false);
        if (error != NVSError::SUCCESS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            BOOT_LOG("[NVS] Erreur creation namespace %s: %d\n", nss[i], static_cast<int>(error));
#else
            Serial.printf("[NVS] ⚠️ Erreur création namespace %s: %d\n", nss[i], static_cast<int>(error));
#endif
        }
        closeNamespace();
    }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    BOOT_LOG("[NVS] Gestionnaire NVS initialise\n");
#else
    Serial.println(F("[NVS] ✅ Gestionnaire NVS initialisé"));
#endif
    
    // Afficher les statistiques initiales
    logUsageStats();
    
    return true;
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
    // v11.168: API NVS bas niveau pour éviter Preferences.getString() qui log NOT_FOUND
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) == ESP_OK) {
        size_t requiredSize = 0;
        esp_err_t err = nvs_get_str(handle, key, nullptr, &requiredSize);
        if (err == ESP_OK && requiredSize > 0 && requiredSize <= NVSConfig::MAX_INSPECTED_STRING_BYTES) {
            char currentValue[NVSConfig::MAX_INSPECTED_STRING_BYTES];
            size_t actualSize = sizeof(currentValue);
            err = nvs_get_str(handle, key, currentValue, &actualSize);
            nvs_close(handle);
            if (err == ESP_OK && strcmp(currentValue, value) == 0) {
                return NVSError::SUCCESS;  // Valeur inchangée
            }
        } else {
            nvs_close(handle);
        }
    }
    
    // Écrire la nouvelle valeur
    NVSError openErr = openNamespace(ns, false);
    if (openErr != NVSError::SUCCESS) {
        return openErr;
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
    
    // v11.166: Utilise API NVS bas niveau pour eviter String Arduino (audit fragmentation heap)
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return NVSError::NAMESPACE_NOT_FOUND;
    }
    
    // Obtenir la taille necessaire puis lire directement dans le buffer
    size_t requiredSize = 0;
    err = nvs_get_str(handle, key, nullptr, &requiredSize);
    
    if (err == ESP_ERR_NVS_NOT_FOUND || requiredSize == 0) {
        // Clé non trouvée, utiliser valeur par défaut
        nvs_close(handle);
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return NVSError::SUCCESS;  // Pas une erreur, juste pas de valeur stockée
    }
    
    if (err != ESP_OK) {
        nvs_close(handle);
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return NVSError::READ_FAILED;
    }
    
    // Lire la valeur directement dans le buffer fourni
    size_t actualSize = valueSize;
    err = nvs_get_str(handle, key, value, &actualSize);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        if (defaultValue) {
            strncpy(value, defaultValue, valueSize - 1);
            value[valueSize - 1] = '\0';
        } else {
            value[0] = '\0';
        }
        return NVSError::READ_FAILED;
    }
    
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

    // Éviter getBool quand la clé n'existe pas (Preferences logue NOT_FOUND en [V])
    if (!_preferences.isKey(key)) {
        value = defaultValue;
        closeNamespace();
        return NVSError::SUCCESS;
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

    // Vérifier si la valeur a changé avant d'écrire (préserve flash)
    NVSError openError = openNamespace(ns, true);
    if (openError == NVSError::SUCCESS) {
        int current = _preferences.getInt(key, value + 1); // +1 pour détecter si clé absente
        closeNamespace();
        if (current == value) {
            return NVSError::SUCCESS; // Valeur inchangée
        }
    }

    // Écrire la nouvelle valeur avec API native
    openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }

    bool success = _preferences.putInt(key, value);
    closeNamespace();

    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveInt", ns, key);
        return NVSError::WRITE_FAILED;
    }

    return NVSError::SUCCESS;
}

NVSError NVSManager::loadInt(const char* ns, const char* key, int& value, int defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        value = defaultValue;
        return NVSError::READ_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadInt", ns, key);
        value = defaultValue;
        return keyError;
    }

    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }

    value = _preferences.getInt(key, defaultValue);
    closeNamespace();

    return NVSError::SUCCESS;
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

    // v11.179: Rejeter les valeurs NaN (invalides pour stockage)
    if (isnan(value)) {
        Serial.printf("[NVS] saveFloat: valeur NaN ignorée pour %s:%s\n", ns, key);
        return NVSError::INVALID_VALUE;
    }

    // Vérifier si la valeur a changé avant d'écrire (préserve flash)
    // Note: comparaison float avec tolérance pour éviter faux positifs
    NVSError openError = openNamespace(ns, true);
    if (openError == NVSError::SUCCESS) {
        float current = _preferences.getFloat(key, NAN);
        closeNamespace();
        // Si current est NaN, on doit écrire la nouvelle valeur
        if (!isnan(current)) {
            float diff = fabsf(current - value);
            if (diff < 0.001f) {
                return NVSError::SUCCESS; // Valeur inchangée
            }
        }
    }

    // Écrire la nouvelle valeur avec API native
    openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }

    bool success = _preferences.putFloat(key, value);
    closeNamespace();

    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveFloat", ns, key);
        return NVSError::WRITE_FAILED;
    }

    return NVSError::SUCCESS;
}

NVSError NVSManager::loadFloat(const char* ns, const char* key, float& value, float defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        value = defaultValue;
        return NVSError::READ_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadFloat", ns, key);
        value = defaultValue;
        return keyError;
    }

    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }

    // Éviter getFloat quand la clé n'existe pas : Preferences logue NOT_FOUND en interne (ex. temp_last_ok au 1er boot).
    // Premier boot : clé absente, NOT_FOUND attendu ; créée à la première sauvegarde température valide (WaterTemp).
    if (!_preferences.isKey(key)) {
        value = defaultValue;
        closeNamespace();
        return NVSError::SUCCESS;
    }
    value = _preferences.getFloat(key, defaultValue);
    closeNamespace();

    return NVSError::SUCCESS;
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

    // Vérifier si la valeur a changé avant d'écrire (préserve flash)
    NVSError openError = openNamespace(ns, true);
    if (openError == NVSError::SUCCESS) {
        unsigned long current = _preferences.getULong(key, value + 1);
        closeNamespace();
        if (current == value) {
            return NVSError::SUCCESS; // Valeur inchangée
        }
    }

    // Écrire la nouvelle valeur avec API native
    openError = openNamespace(ns, false);
    if (openError != NVSError::SUCCESS) {
        return openError;
    }

    bool success = _preferences.putULong(key, value);
    closeNamespace();

    if (!success) {
        logError(NVSError::WRITE_FAILED, "saveULong", ns, key);
        return NVSError::WRITE_FAILED;
    }

    return NVSError::SUCCESS;
}

NVSError NVSManager::loadULong(const char* ns, const char* key, unsigned long& value, unsigned long defaultValue) {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
        value = defaultValue;
        return NVSError::READ_FAILED;
    }

    NVSError keyError = validateKey(key);
    if (keyError != NVSError::SUCCESS) {
        logError(keyError, "loadULong", ns, key);
        value = defaultValue;
        return keyError;
    }

    NVSError openError = openNamespace(ns, true);
    if (openError != NVSError::SUCCESS) {
        value = defaultValue;
        return openError;
    }

    // Éviter getULong quand la clé n'existe pas (Preferences logue NOT_FOUND en [V], ex. diag_otaOk)
    if (!_preferences.isKey(key)) {
        value = defaultValue;
        closeNamespace();
        return NVSError::SUCCESS;
    }
    value = _preferences.getULong(key, defaultValue);
    closeNamespace();

    return NVSError::SUCCESS;
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
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    BOOT_LOG("[NVS] Impossible de lire les statistiques NVS (err=%d)\n", static_cast<int>(err));
#else
    Serial.printf("[NVS] ⚠️ Impossible de lire les statistiques NVS (err=%d)\n", static_cast<int>(err));
#endif
  }

    return stats;
}

void NVSManager::logUsageStats() {
    NVSUsageStats stats = getUsageStats();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    BOOT_LOG("========================================\n");
    BOOT_LOG("[NVS] Statistiques d'utilisation\n");
    BOOT_LOG("========================================\n");
    BOOT_LOG("[NVS] Namespaces: %u\n", (unsigned)stats.namespaceCount);
    BOOT_LOG("[NVS] Entrees totales: %u, utilisees: %u, libres: %u (%.1f%%)\n",
             (unsigned)stats.totalBytes, (unsigned)stats.usedBytes, (unsigned)stats.freeBytes, stats.usagePercent);
    BOOT_LOG("[NVS] Cles utilisees (approx.): %u\n", (unsigned)stats.keyCount);
    BOOT_LOG("========================================\n");
#else
    Serial.println(F("========================================"));
    Serial.println(F("[NVS] 📊 Statistiques d'utilisation"));
    Serial.println(F("========================================"));
  Serial.printf("[NVS] Namespaces: %zu\n", stats.namespaceCount);
  Serial.printf("[NVS] Entrées totales: %zu, utilisées: %zu, libres: %zu (%.1f%%)\n",
                stats.totalBytes, stats.usedBytes, stats.freeBytes, stats.usagePercent);
    Serial.printf("[NVS] Clés utilisées (approx.): %zu\n", stats.keyCount);
    Serial.println(F("========================================"));
#endif
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
    
    // v11.190: Remove idempotent — si la clé n'existe pas, succès (évite log NOT_FOUND)
    if (!_preferences.isKey(key)) {
        closeNamespace();
        return NVSError::SUCCESS;
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

NVSError NVSManager::migrateFromOldSystem() {
    NVSLockGuard guard(*this);
    if (!guard.locked()) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Mutex migrateFromOldSystem\n");
#else
        Serial.println(F("[NVS] ❌ Mutex migrateFromOldSystem"));
#endif
        return NVSError::WRITE_FAILED;
    }

    static constexpr const char* MIGRATION_FLAG_KEY = "mig1180_done";

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    BOOT_LOG("[NVS] Migration depuis l'ancien systeme NVS\n");
#else
    Serial.println(F("[NVS] 🔄 Migration depuis l'ancien système NVS"));
#endif
    // Éviter les migrations répétées à chaque boot
    nvs_handle_t systemHandle = 0;
    esp_err_t systemOpenErr = nvs_open(NVS_NAMESPACES::SYSTEM, NVS_READWRITE, &systemHandle);
    if (systemOpenErr == ESP_OK) {
        uint8_t doneFlag = 0;
        esp_err_t flagErr = nvs_get_u8(systemHandle, MIGRATION_FLAG_KEY, &doneFlag);
        if (flagErr == ESP_OK && doneFlag == 1) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            BOOT_LOG("[NVS] Migration deja effectuee - aucune action requise\n");
#else
            Serial.println(F("[NVS] ✅ Migration déjà effectuée - aucune action requise"));
#endif
            nvs_close(systemHandle);
            return NVSError::SUCCESS;
        }
    } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Impossible d'acceder au namespace %s pour le flag de migration (err=%d)\n",
                 NVS_NAMESPACES::SYSTEM, systemOpenErr);
#else
        Serial.printf("[NVS] ⚠️ Impossible d'accéder au namespace %s pour le flag de migration (err=%d)\n",
                      NVS_NAMESPACES::SYSTEM, systemOpenErr);
#endif
    }

    struct MigrationRule {
        const char* oldNamespace;
        const char* newNamespace;
        const char* keyPrefix;
    };
    
    // Migration rules: ancien namespace -> nouveau namespace + préfixe
    // NOTE: drift_, cmd_, digest_ supprimés (code mort, jamais utilisés)
    const MigrationRule rules[] = {
        {"bouffe",      NVS_NAMESPACES::CONFIG,  "bouffe_"},
        {"ota",         NVS_NAMESPACES::SYSTEM,  "ota_"},
        {"remoteVars",  NVS_NAMESPACES::CONFIG,  "remote_"},
        {"rtc",         NVS_NAMESPACES::SYSTEM,  "rtc_"},      // TIME supprimé -> SYSTEM
        {"diagnostics", NVS_NAMESPACES::LOGS,    "diag_"},
        {"alerts",      NVS_NAMESPACES::LOGS,    "alert_"},
        {"gpio",        NVS_NAMESPACES::CONFIG,  "gpio_"},
        {"actSnap",     NVS_NAMESPACES::STATE,   "snap_"},
        {"actState",    NVS_NAMESPACES::STATE,   "state_"},
        {"pendingSync", NVS_NAMESPACES::STATE,   "sync_"},
        {"waterTemp",   NVS_NAMESPACES::CONFIG,  "temp_"},     // SENSORS supprimé -> CONFIG
        {"reset",       NVS_NAMESPACES::SYSTEM,  "reset_"},
        {"net",         NVS_NAMESPACES::SYSTEM,  "net_"}
    };

    auto copyNvsEntryByType = [](nvs_handle_t oldHandle, nvs_handle_t newHandle,
                                 const char* oldKey, const char* newKey, nvs_type_t type,
                                 const char* oldNsForLog) -> bool {
        static constexpr size_t MAX_MIGRATION_BLOB = 2048;
        bool copied = false;
        esp_err_t readErr = ESP_FAIL;
        switch (type) {
            case NVS_TYPE_U8: {
                uint8_t value = 0;
                readErr = nvs_get_u8(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_u8(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_I8: {
                int8_t value = 0;
                readErr = nvs_get_i8(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_i8(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_U16: {
                uint16_t value = 0;
                readErr = nvs_get_u16(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_u16(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_I16: {
                int16_t value = 0;
                readErr = nvs_get_i16(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_i16(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_U32: {
                uint32_t value = 0;
                readErr = nvs_get_u32(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_u32(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_I32: {
                int32_t value = 0;
                readErr = nvs_get_i32(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_i32(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_U64: {
                uint64_t value = 0;
                readErr = nvs_get_u64(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_u64(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_I64: {
                int64_t value = 0;
                readErr = nvs_get_i64(oldHandle, oldKey, &value);
                if (readErr == ESP_OK && nvs_set_i64(newHandle, newKey, value) == ESP_OK) copied = true;
                break;
            }
            case NVS_TYPE_STR: {
                size_t length = 0;
                readErr = nvs_get_str(oldHandle, oldKey, nullptr, &length);
                if (readErr == ESP_OK && length > 0 && length < NVSConfig::MAX_INSPECTED_STRING_BYTES) {
                    char buffer[NVSConfig::MAX_INSPECTED_STRING_BYTES];
                    readErr = nvs_get_str(oldHandle, oldKey, buffer, &length);
                    if (readErr == ESP_OK && nvs_set_str(newHandle, newKey, buffer) == ESP_OK) copied = true;
                } else if (readErr == ESP_OK && length >= NVSConfig::MAX_INSPECTED_STRING_BYTES) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                    BOOT_LOG("[NVS] Migration STR %s trop longue (%zu), ignoree\n", oldKey, length);
#else
                    Serial.printf("[NVS] ⚠️ Migration STR %s trop longue (%zu), ignorée\n", oldKey, length);
#endif
                }
                break;
            }
            case NVS_TYPE_BLOB: {
                size_t length = 0;
                readErr = nvs_get_blob(oldHandle, oldKey, nullptr, &length);
                if (readErr == ESP_OK) {
                    if (length == 0) {
                        if (nvs_set_blob(newHandle, newKey, nullptr, 0) == ESP_OK) copied = true;
                    } else if (length <= MAX_MIGRATION_BLOB) {
                        uint8_t buffer[MAX_MIGRATION_BLOB];
                        readErr = nvs_get_blob(oldHandle, oldKey, buffer, &length);
                        if (readErr == ESP_OK &&
                            nvs_set_blob(newHandle, newKey, buffer, length) == ESP_OK) copied = true;
                    } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                        BOOT_LOG("[NVS] Migration BLOB %s trop gros (%zu), ignore\n", oldKey, length);
#else
                        Serial.printf("[NVS] ⚠️ Migration BLOB %s trop gros (%zu), ignoré\n", oldKey, length);
#endif
                    }
                }
                break;
            }
            default:
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                BOOT_LOG("[NVS] Type non pris en charge pour %s/%s (type=%d)\n",
                         oldNsForLog, oldKey, static_cast<int>(type));
#else
                Serial.printf("[NVS] ⚠️ Type non pris en charge pour %s/%s (type=%d)\n",
                              oldNsForLog, oldKey, static_cast<int>(type));
#endif
                return false;
        }
        if (!copied && readErr != ESP_OK) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            BOOT_LOG("[NVS] Lecture impossible pour %s/%s (err=%d)\n", oldNsForLog, oldKey, readErr);
#else
            Serial.printf("[NVS] ⚠️ Lecture impossible pour %s/%s (err=%d)\n", oldNsForLog, oldKey, readErr);
#endif
        }
        return copied;
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
            // Note: NVS_TYPE_DOUBLE n'existe pas dans ESP-IDF NVS API
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

        // v11.166: Compteur limite pour eviter boucle infinie (max 100 entries par namespace)
        constexpr size_t MAX_NVS_ENTRIES_PER_NAMESPACE = 100;
        size_t entryCount = 0;

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
        nvs_iterator_t it = nullptr;
        esp_err_t findErr = nvs_entry_find(NVS_DEFAULT_PART_NAME, rule.oldNamespace, NVS_TYPE_ANY, &it);

        while (findErr == ESP_OK && entryCount < MAX_NVS_ENTRIES_PER_NAMESPACE) {
            ++entryCount;
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            char newKey[128];
            snprintf(newKey, sizeof(newKey), "%s%s", rule.keyPrefix ? rule.keyPrefix : "", info.key);
            if (strlen(newKey) > 15) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                BOOT_LOG("[NVS] Cle ignoree (nom >15 caracteres): %s/%s -> %s\n",
                         rule.oldNamespace, info.key, newKey);
#else
                Serial.printf("[NVS] ⚠️ Clé ignorée (nom >15 caractères): %s/%s -> %s\n",
                              rule.oldNamespace, info.key, newKey);
#endif
                findErr = nvs_entry_next(&it);
                continue;
            }

            if (keyExists(newHandle, newKey, info.type)) {
                findErr = nvs_entry_next(&it);
                continue; // Donnée déjà migrée
            }

            bool copied = copyNvsEntryByType(oldHandle, newHandle, info.key, newKey, info.type, rule.oldNamespace);

            if (copied) {
                ruleMigrated = true;
                migratedSomething = true;
            }

            if (entryCount % 10 == 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            findErr = nvs_entry_next(&it);
        }

        if (it != nullptr) {
            nvs_release_iterator(it);
        }
#else
        // IDF 4.x: nvs_entry_find(3 args) retourne l'itérateur, nvs_entry_next(it) retourne le suivant
        nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, rule.oldNamespace, NVS_TYPE_ANY);

        while (it != nullptr && entryCount < MAX_NVS_ENTRIES_PER_NAMESPACE) {
            ++entryCount;
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            char newKey[128];
            snprintf(newKey, sizeof(newKey), "%s%s", rule.keyPrefix ? rule.keyPrefix : "", info.key);
            if (strlen(newKey) > 15) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                BOOT_LOG("[NVS] Cle ignoree (nom >15 caracteres): %s/%s -> %s\n",
                         rule.oldNamespace, info.key, newKey);
#else
                Serial.printf("[NVS] ⚠️ Clé ignorée (nom >15 caractères): %s/%s -> %s\n",
                              rule.oldNamespace, info.key, newKey);
#endif
                it = nvs_entry_next(it);
                continue;
            }

            if (keyExists(newHandle, newKey, info.type)) {
                it = nvs_entry_next(it);
                continue;
            }

            bool copied = copyNvsEntryByType(oldHandle, newHandle, info.key, newKey, info.type, rule.oldNamespace);

            if (copied) {
                ruleMigrated = true;
                migratedSomething = true;
            }

            if (entryCount % 10 == 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            it = nvs_entry_next(it);
        }

        if (it != nullptr) {
            nvs_release_iterator(it);
        }
#endif
        // Fin branchement IDF 4/5

        if (entryCount >= MAX_NVS_ENTRIES_PER_NAMESPACE) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            BOOT_LOG("[NVS] Limite migration atteinte (%zu entries) pour %s, arret premature\n",
                     entryCount, rule.oldNamespace);
#else
            Serial.printf("[NVS] ⚠️ Limite migration atteinte (%zu entries) pour %s, arret premature\n",
                          entryCount, rule.oldNamespace);
#endif
        }

        if (ruleMigrated) {
            esp_err_t commitErr = nvs_commit(newHandle);
            if (commitErr != ESP_OK) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                BOOT_LOG("[NVS] Commit echoue pour %s (err=%d)\n", rule.newNamespace, commitErr);
#else
                Serial.printf("[NVS] ⚠️ Commit echoue pour %s (err=%d)\n", rule.newNamespace, commitErr);
#endif
            } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
                BOOT_LOG("[NVS] Donnees migrees de %s vers %s\n", rule.oldNamespace, rule.newNamespace);
#else
                Serial.printf("[NVS] ✅ Donnees migrees de %s vers %s\n",
                              rule.oldNamespace, rule.newNamespace);
#endif
            }
        }

        nvs_close(newHandle);
        nvs_close(oldHandle);
    }

    if (systemHandle == 0 && systemOpenErr != ESP_OK) {
        if (nvs_open(NVS_NAMESPACES::SYSTEM, NVS_READWRITE, &systemHandle) != ESP_OK) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            BOOT_LOG("[NVS] Impossible d'enregistrer le flag de migration\n");
#else
            Serial.println(F("[NVS] ⚠️ Impossible d'enregistrer le flag de migration"));
#endif
        }
    }

    if (systemHandle != 0) {
        nvs_set_u8(systemHandle, MIGRATION_FLAG_KEY, 1);
        nvs_commit(systemHandle);
        nvs_close(systemHandle);
    }

    if (!migratedSomething) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Aucun ancien namespace detecte ou donnees deja migrees\n");
#else
        Serial.println(F("[NVS] ℹ️ Aucun ancien namespace detecte ou donnees deja migrees"));
#endif
    } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        BOOT_LOG("[NVS] Migration des anciens namespaces terminee\n");
#else
        Serial.println(F("[NVS] ✅ Migration des anciens namespaces terminee"));
#endif
    }

    return NVSError::SUCCESS;
}

// Méthodes obsolètes supprimées: saveJsonCompressed, loadJsonDecompressed, compressJson, decompressJson,
// enableDeferredFlush, setFlushInterval, forceFlush, checkDeferredFlush, isDeferredFlushEnabled,
// getLastFlushTime, rotateLogs, cleanupOldData, cleanupObsoleteKeys, schedulePeriodicCleanup,
// shouldPerformCleanup, addDirtyKey, removeDirtyKey, shouldFlush
// (cache, flush différé, compression JSON, rotation automatique supprimés pour simplifier)
