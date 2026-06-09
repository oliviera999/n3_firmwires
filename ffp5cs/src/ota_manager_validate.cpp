// ota_manager_validate.cpp — Méthodes de validation OTAManager (métadonnées,
// tailles firmware/filesystem, espace, sélection d'artefact). Extrait de
// ota_manager.cpp (audit optimisation v13.93) : définitions de méthodes membres
// réparties sur plusieurs TU, classe et comportement inchangés.
#include "ota_manager.h"
#include "nvs_manager.h" // v11.109
#include "nvs_keys.h"
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <limits.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include "config.h"
#include "mailer.h"
#include "automatism.h"
#include "diagnostics.h"
#include "task_monitor.h"
#include "tls_mutex.h"
#include "display_view.h"

bool OTAManager::validateMetadata(const JsonDocument& doc) {
    // Accepte soit le schéma legacy (version/bin_url au niveau racine),
    // soit le schéma channels {prod/test}->{model/default}
    if (doc["channels"].is<JsonObject>()) {
        return true;
    }
    bool hasTopVersion = doc["version"].is<const char*>();
    bool hasTopUrl = doc["bin_url"].is<const char*>();
    if (!hasTopVersion || !hasTopUrl) {
        logError("Métadonnées invalides: ni 'channels' ni 'version/bin_url' valides");
        return false;
    }
    return true;
}

bool OTAManager::validateFirmwareSize(size_t expected, size_t actual) {
    if (actual <= 0) {
        logError("Taille du firmware invalide");
        return false;
    }
    
    if (expected > 0 && actual != expected) {
        logError("Taille du firmware ne correspond pas à celle attendue");
        return false;
    }
    
    return true;
}

bool OTAManager::validateFilesystemSize(size_t expected, size_t actual) {
    if (actual <= 0) {
        logError("Taille du filesystem invalide");
        return false;
    }
    
    if (actual > OTAConfig::MAX_FILESYSTEM_SIZE) {
        char actualBuf[16], maxBuf[16];
        formatBytes(actual, actualBuf, sizeof(actualBuf));
        formatBytes(OTAConfig::MAX_FILESYSTEM_SIZE, maxBuf, sizeof(maxBuf));
        char errorMsg[128];
        snprintf(errorMsg, sizeof(errorMsg), "Taille du filesystem trop importante: %s > %s", actualBuf, maxBuf);
        logError(errorMsg);
        return false;
    }
    
    if (expected > 0 && actual != expected) {
        logError("Taille du filesystem ne correspond pas à celle attendue");
        return false;
    }
    
    return true;
}

bool OTAManager::validateSpace(size_t required) {
    size_t freeSpace = ESP.getFreeSketchSpace();
    size_t freeHeap = ESP.getFreeHeap();
    
    if (freeSpace == 0) {
        log("ℹ️ OTA désactivée: aucune partition OTA (free sketch space=0)");
        return false;
    }

    char freeSpaceBuf[16], freeHeapBuf[16];
    formatBytes(freeSpace, freeSpaceBuf, sizeof(freeSpaceBuf));
    formatBytes(freeHeap, freeHeapBuf, sizeof(freeHeapBuf));
    char msg[128];
    snprintf(msg, sizeof(msg), "📊 Espace libre sketch: %s", freeSpaceBuf);
    log(msg);
    snprintf(msg, sizeof(msg), "📊 Heap libre: %s", freeHeapBuf);
    log(msg);
    
    if (freeSpace < required) {
        char requiredBuf[16];
        formatBytes(required, requiredBuf, sizeof(requiredBuf));
        snprintf(msg, sizeof(msg), "Espace insuffisant: %s < %s", freeSpaceBuf, requiredBuf);
        logError(msg);
        return false;
    }
    
    if (freeHeap < HeapConfig::MIN_HEAP_OTA) {
        char heapBuf[16];
        formatBytes(freeHeap, heapBuf, sizeof(heapBuf));
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Heap insuffisant: %s (< %u bytes)", heapBuf, (unsigned)HeapConfig::MIN_HEAP_OTA);
        logError(errorMsg);
        log("[OTA] Reporté au prochain cycle (heap)");
        return false;
    }
    
    return true;
}

bool OTAManager::selectArtifactFromMetadata(const JsonDocument& doc, char* outVersion, size_t versionSize, char* outUrl, size_t urlSize, int& outSize, char* outMD5, size_t md5Size) {
    // Déterminer environnement et modèle
    const char* envName = "prod";
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
        envName = "test";
    #elif defined(PROFILE_PROD)
        envName = "prod";
    #else
        envName = "prod"; // défaut sécurisé
    #endif
    const char* modelName = "esp32-wroom";
    #if defined(BOARD_S3)
    modelName = "esp32-s3";
    #endif

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "🔎 Sélection OTA: env=%s, model=%s", envName, modelName);
    log(logMsg);

    auto tryFillFrom = [&](JsonVariantConst v) -> bool {
        if (v.isNull()) return false;

        const char* urlStr = v["bin_url"].as<const char*>();
        const char* verStr = v["version"].as<const char*>();
        int size = v["size"].is<int>() ? v["size"].as<int>() : 0;
        const char* md5Str = v["md5"].as<const char*>();

        // Fallbacks depuis top-level si certains champs manquent
        if (!urlStr || strlen(urlStr) == 0) urlStr = doc["bin_url"].as<const char*>();
        if (!verStr || strlen(verStr) == 0) verStr = doc["version"].as<const char*>();
        if (size <= 0) size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
        if (!md5Str || strlen(md5Str) == 0) md5Str = doc["md5"].as<const char*>();

        // Nécessaire au minimum: une URL
        if (urlStr && strlen(urlStr) > 0) {
            strncpy(outUrl, urlStr, urlSize - 1);
            outUrl[urlSize - 1] = '\0';
            if (verStr) {
                strncpy(outVersion, verStr, versionSize - 1);
                outVersion[versionSize - 1] = '\0';
            } else {
                outVersion[0] = '\0';
            }
            outSize = size;
            if (md5Str) {
                strncpy(outMD5, md5Str, md5Size - 1);
                outMD5[md5Size - 1] = '\0';
            } else {
                outMD5[0] = '\0';
            }
            return true;
        }
        return false;
    };

    // 1) channels[env][model]
    if (!doc["channels"].isNull()) {
        JsonVariantConst v1 = doc["channels"][envName][modelName];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test channels[%s][%s]: %s", envName, modelName, v1.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v1)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection via channels[%s][%s]", envName, modelName);
            log(logMsg);
            return true;
        }
        // 2) channels[env][default]
        JsonVariantConst v2 = doc["channels"][envName]["default"];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test channels[%s][default]: %s", envName, v2.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v2)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection via channels[%s][default]", envName);
            log(logMsg);
            return true;
        }
        // 3) channels[prod][model]
        JsonVariantConst v3 = doc["channels"]["prod"][modelName];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test channels[prod][%s]: %s", modelName, v3.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v3)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection via channels[prod][%s]", modelName);
            log(logMsg);
            return true;
        }
        // 4) channels[prod][default]
        JsonVariantConst v4 = doc["channels"]["prod"]["default"];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test channels[prod][default]: %s", v4.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v4)) { 
            log("✅ Sélection via channels[prod][default]");
            return true;
        }
    }

    // 5) Fallback legacy top-level (direct)
    log("🔎 Test fallback top-level");
    const char* urlStr = doc["bin_url"].as<const char*>();
    const char* verStr = doc["version"].as<const char*>();
    int size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
    const char* md5Str = doc["md5"].as<const char*>();
    if (urlStr && strlen(urlStr) > 0 && verStr && strlen(verStr) > 0) {
        strncpy(outUrl, urlStr, urlSize - 1);
        outUrl[urlSize - 1] = '\0';
        strncpy(outVersion, verStr, versionSize - 1);
        outVersion[versionSize - 1] = '\0';
        outSize = size;
        if (md5Str) {
            strncpy(outMD5, md5Str, md5Size - 1);
            outMD5[md5Size - 1] = '\0';
        } else {
            outMD5[0] = '\0';
        }
        return true;
    }
    return false;
}

bool OTAManager::selectFilesystemFromMetadata(const JsonDocument& doc, char* outUrl, size_t urlSize, int& outSize, char* outMD5, size_t md5Size) {
    // Déterminer environnement et modèle
    const char* envName = "prod";
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
        envName = "test";
    #elif defined(PROFILE_PROD)
        envName = "prod";
    #else
        envName = "prod"; // défaut sécurisé
    #endif
    const char* modelName = "esp32-wroom";
    #if defined(BOARD_S3)
    modelName = "esp32-s3";
    #endif

    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "🔎 Sélection Filesystem OTA: env=%s, model=%s", envName, modelName);
    log(logMsg);

    auto tryFillFrom = [&](JsonVariantConst v) -> bool {
        if (v.isNull()) return false;

        const char* urlStr = v["filesystem_url"].as<const char*>();
        int size = v["filesystem_size"].is<int>() ? v["filesystem_size"].as<int>() : 0;
        const char* md5Str = v["filesystem_md5"].as<const char*>();

        // Fallbacks depuis top-level si certains champs manquent
        if (!urlStr || strlen(urlStr) == 0) urlStr = doc["filesystem_url"].as<const char*>();
        if (size <= 0) size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
        if (!md5Str || strlen(md5Str) == 0) md5Str = doc["filesystem_md5"].as<const char*>();

        // Nécessaire au minimum: une URL
        if (urlStr && strlen(urlStr) > 0) {
            strncpy(outUrl, urlStr, urlSize - 1);
            outUrl[urlSize - 1] = '\0';
            outSize = size;
            if (md5Str) {
                strncpy(outMD5, md5Str, md5Size - 1);
                outMD5[md5Size - 1] = '\0';
            } else {
                outMD5[0] = '\0';
            }
            return true;
        }
        return false;
    };

    // 1) channels[env][model]
    if (!doc["channels"].isNull()) {
        JsonVariantConst v1 = doc["channels"][envName][modelName];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test filesystem channels[%s][%s]: %s", envName, modelName, v1.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v1)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection filesystem via channels[%s][%s]", envName, modelName);
            log(logMsg);
            return true;
        }
        // 2) channels[env][default]
        JsonVariantConst v2 = doc["channels"][envName]["default"];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test filesystem channels[%s][default]: %s", envName, v2.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v2)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection filesystem via channels[%s][default]", envName);
            log(logMsg);
            return true;
        }
        // 3) channels[prod][model]
        JsonVariantConst v3 = doc["channels"]["prod"][modelName];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test filesystem channels[prod][%s]: %s", modelName, v3.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v3)) { 
            snprintf(logMsg, sizeof(logMsg), "✅ Sélection filesystem via channels[prod][%s]", modelName);
            log(logMsg);
            return true;
        }
        // 4) channels[prod][default]
        JsonVariantConst v4 = doc["channels"]["prod"]["default"];
        snprintf(logMsg, sizeof(logMsg), "🔎 Test filesystem channels[prod][default]: %s", v4.isNull() ? "absent" : "ok");
        log(logMsg);
        if (tryFillFrom(v4)) { 
            log("✅ Sélection filesystem via channels[prod][default]");
            return true;
        }
    }

    // 5) Fallback legacy top-level (direct)
    log("🔎 Test fallback filesystem top-level");
    const char* urlStr = doc["filesystem_url"].as<const char*>();
    int size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
    const char* md5Str = doc["filesystem_md5"].as<const char*>();
    if (urlStr && strlen(urlStr) > 0) {
        strncpy(outUrl, urlStr, urlSize - 1);
        outUrl[urlSize - 1] = '\0';
        outSize = size;
        if (md5Str) {
            strncpy(outMD5, md5Str, md5Size - 1);
            outMD5[md5Size - 1] = '\0';
        } else {
            outMD5[0] = '\0';
        }
        return true;
    }
    return false;
}

