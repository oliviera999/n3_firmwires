// nvs_manager_typed.cpp — accesseurs typés NVSManager (save/load String/Bool/Int/Float/ULong).
// Extrait de nvs_manager.cpp (audit : découpe). Méthodes membres, comportement identique.
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
    // Valider le buffer AVANT tout déréférencement (le chemin !guard.locked() écrivait
    // dans `value` sans avoir vérifié value/valueSize).
    if (value == nullptr || valueSize == 0) {
        return NVSError::INVALID_VALUE;
    }

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

  // Vérifier si la valeur a réellement changé avant d'écrire.
  // v13.46: Ne pas utiliser getBool(key, value) pour une clé absente : Preferences
  // renvoie alors la valeur par défaut (= value), ce qui fausse « inchangé » et
  // empêche la toute première persistance (ex. snap_pending / snap_aqua avant veille).
  NVSError openError = openNamespace(ns, true);
  if (openError == NVSError::SUCCESS) {
    bool skipWrite = false;
    if (_preferences.isKey(key)) {
      bool current = _preferences.getBool(key, false);
      skipWrite = (current == value);
    }
    closeNamespace();
    if (skipWrite) {
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

    // v13.70 (audit): même pattern que saveBool v13.46 - ne comparer que si la clé existe.
    // L'ancien `getInt(key, value+1)` reposait sur "+1" comme sentinelle, ce qui échoue
    // si la valeur réelle est exactement value+1 (coïncidence rare mais possible).
    NVSError openError = openNamespace(ns, true);
    if (openError == NVSError::SUCCESS) {
        bool skipWrite = false;
        if (_preferences.isKey(key)) {
            int current = _preferences.getInt(key, 0);
            skipWrite = (current == value);
        }
        closeNamespace();
        if (skipWrite) {
            return NVSError::SUCCESS;
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

    // v13.70 (audit): même pattern que saveBool v13.46 - ne comparer que si la clé existe
    // (la sentinelle "+1" pouvait coïncider avec une valeur réelle, faussant skipWrite).
    NVSError openError = openNamespace(ns, true);
    if (openError == NVSError::SUCCESS) {
        bool skipWrite = false;
        if (_preferences.isKey(key)) {
            unsigned long current = _preferences.getULong(key, 0);
            skipWrite = (current == value);
        }
        closeNamespace();
        if (skipWrite) {
            return NVSError::SUCCESS;
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


