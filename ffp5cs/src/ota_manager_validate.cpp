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
#include "ota_artifact_select.h"  // logique pure extraite (testée nativement, audit §3.8)
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

    // v14.x (audit §3.8) : logique de cascade extraite en fonction pure
    // OtaArtifactSelect::selectArtifact (ota_artifact_select.h), testée nativement
    // (test/test_ota_select). Comportement de sélection inchangé ; seuls les logs
    // de debug par-branche sont consolidés en un résumé succès/échec.
    bool ok = OtaArtifactSelect::selectArtifact(doc, envName, modelName,
                                                outVersion, versionSize,
                                                outUrl, urlSize,
                                                outSize,
                                                outMD5, md5Size);
    if (ok) {
        snprintf(logMsg, sizeof(logMsg), "✅ Artefact OTA sélectionné: %s", outUrl);
        log(logMsg);
    } else {
        log("❌ Aucun artefact OTA trouvé dans le manifeste");
    }
    return ok;
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

    // v14.x (audit §3.8) : cascade extraite en fonction pure OtaArtifactSelect::selectFilesystem
    // (ota_artifact_select.h), testée nativement. Sélection inchangée ; logs consolidés.
    bool ok = OtaArtifactSelect::selectFilesystem(doc, envName, modelName,
                                                  outUrl, urlSize, outSize, outMD5, md5Size);
    if (ok) {
        snprintf(logMsg, sizeof(logMsg), "✅ Image filesystem sélectionnée: %s", outUrl);
        log(logMsg);
    } else {
        log("ℹ️ Aucune image filesystem dans le manifeste (optionnel)");
    }
    return ok;
}

