#include "ota_manager.h"
#include "ota_url.h"  // downgradeToHttp (extrait, testé nativement)
#include "ota_artifact_select.h"  // readIntegrityFields (sha256/signature, v14.17)
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
#include "boot_log.h"
#include "mailer.h"
#include "automatism.h"
#include "diagnostics.h"
#include "task_monitor.h"
#include "tls_mutex.h"
#include "display_view.h"


namespace {
bool hasOtaPartition() {
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

#if defined(BOARD_WROOM) || defined(BOARD_S3)
// downgradeToHttp extrait dans ota_url.h (testé nativement, audit §3.8).
using OtaUrl::downgradeToHttp;
#endif
} // namespace

OTAManager::OTAManager() 
    : m_otaLock(false)
    , m_lastCheck(0)
    , m_checkInterval(TimingConfig::OTA_CHECK_INTERVAL_MS) // 2 heures par défaut
    , m_firmwareSize(0)
    , m_filesystemSize(0)
    , m_updateTaskHandle(nullptr)
    , m_httpClient(nullptr) {
    m_currentVersion[0] = '\0';
    m_remoteVersion[0] = '\0';
    m_firmwareUrl[0] = '\0';
    m_firmwareMD5[0] = '\0';
    m_firmwareSha256[0] = '\0';
    m_firmwareSignature[0] = '\0';
    m_filesystemUrl[0] = '\0';
    m_filesystemMD5[0] = '\0';
}

OTAManager::~OTAManager() {
    // Nettoyer les ressources
    if (m_httpClient) {
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
    }
    
    if (m_updateTaskHandle) {
        vTaskDelete(m_updateTaskHandle);
        m_updateTaskHandle = nullptr;
    }
}

void OTAManager::log(const char* message) {
    OTA_LOG("%s", message ? message : "(null)");
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}

void OTAManager::logError(const char* error) {
    OTA_LOG("ERR %s", error ? error : "(null)");
    if (m_errorCallback) {
        m_errorCallback(error);
    }
}

void OTAManager::logProgress(int progress, size_t downloaded, size_t total, float speed) {
    char downloadedBuf[16], totalBuf[16], speedBuf[16];
    formatBytes(downloaded, downloadedBuf, sizeof(downloadedBuf));
    formatBytes(total, totalBuf, sizeof(totalBuf));
    formatSpeed(speed, speedBuf, sizeof(speedBuf));
    Serial.printf("[OTA] 📊 Progression: %d%% (%s/%s) - Vitesse: %s\n", 
                 progress, downloadedBuf, totalBuf, speedBuf);
    if ((progress % 10) == 0) {
        Serial.printf("[Event] OTA progress %d%%\n", progress);
    }
    if (m_progressCallback) {
        m_progressCallback(progress);
    }
}

void OTAManager::setProgressCallback(std::function<void(int)> callback) {
    m_progressCallback = callback;
}

void OTAManager::setStatusCallback(std::function<void(const char*)> callback) {
    m_statusCallback = callback;
}

void OTAManager::setErrorCallback(std::function<void(const char*)> callback) {
    m_errorCallback = callback;
}

void OTAManager::setCheckInterval(unsigned long interval) {
    m_checkInterval = interval;
}

void OTAManager::setCurrentVersion(const char* version) {
    if (version) {
        strncpy(m_currentVersion, version, sizeof(m_currentVersion) - 1);
        m_currentVersion[sizeof(m_currentVersion) - 1] = '\0';
    } else {
        m_currentVersion[0] = '\0';
    }
}

const char* OTAManager::getCurrentVersion() const {
    return m_currentVersion;
}

const char* OTAManager::getRemoteVersion() const {
    return m_remoteVersion;
}

bool OTAManager::checkForUpdate() {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT avant opérations longues (évite TWDT si TLS/metadata lent)
    }
    if (m_otaLock) {
        log("⚠️ Mode OTA exclusif déjà actif");
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        logError("WiFi non connecté");
        return false;
    }
    
    log("✅ WiFi connecté");

    if (!hasOtaPartition()) {
        log("ℹ️ OTA désactivée: aucune partition OTA disponible");
        return false;
    }
    
    // Vérification de l'espace disponible
    if (!validateSpace(1024 * 1024)) { // Minimum 1MB
        return false;
    }
    
    // Téléchargement des métadonnées (lecture directe dans payload, pas de buffer intermédiaire stack)
    // Taille alignée sur metadata.json servi (~2.7KB) pour éviter troncature → IncompleteInput
    char payload[BufferConfig::OTA_METADATA_PAYLOAD_BUFFER_SIZE];
    if (!downloadMetadata(payload, sizeof(payload))) {
        return false;
    }
    
    // Parsing JSON (buffer dédié metadata ~1129 bytes, JSON_DOCUMENT_SIZE 1024 insuffisant WROOM)
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE_OTA_METADATA> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        char errorMsg[160];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur parsing JSON metadata: %s (taille payload: %zu)",
                 error.c_str(), strlen(payload));
        logError(errorMsg);
        return false;
    }
    log("✅ JSON parsé avec succès");
    
    // Validation des métadonnées (accepte channels ou schéma legacy)
    if (!validateMetadata(doc)) {
        return false;
    }
    
    // Sélection de l'artefact selon env/modèle avec fallbacks
    char selVersion[32], selUrl[256], selMD5[33];
    int selSize = 0;
    if (!selectArtifactFromMetadata(doc, selVersion, sizeof(selVersion), selUrl, sizeof(selUrl), selSize, selMD5, sizeof(selMD5))) {
        logError("Aucun artefact OTA valide trouvé dans les métadonnées");
        return false;
    }

    strncpy(m_remoteVersion, selVersion, sizeof(m_remoteVersion) - 1);
    m_remoteVersion[sizeof(m_remoteVersion) - 1] = '\0';
    strncpy(m_firmwareUrl, selUrl, sizeof(m_firmwareUrl) - 1);
    m_firmwareUrl[sizeof(m_firmwareUrl) - 1] = '\0';
    m_firmwareSize = selSize;
    strncpy(m_firmwareMD5, selMD5, sizeof(m_firmwareMD5) - 1);
    m_firmwareMD5[sizeof(m_firmwareMD5) - 1] = '\0';

    // v14.17 — Champs d'authenticité optionnels (sha256 + signature ECDSA), même cascade
    // que la sélection d'artefact. Absents => vides => fallback MD5 seul (rétro-compatible).
    {
        const char* envNameSig = "prod";
        #if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
            envNameSig = "test";
        #endif
        const char* modelNameSig = "esp32-wroom";
        #if defined(BOARD_S3)
            modelNameSig = "esp32-s3";
        #endif
        OtaArtifactSelect::readIntegrityFields(doc, envNameSig, modelNameSig,
                                               m_firmwareSha256, sizeof(m_firmwareSha256),
                                               m_firmwareSignature, sizeof(m_firmwareSignature));
        if (strlen(m_firmwareSha256) > 0) {
            char logSha[96];
            snprintf(logSha, sizeof(logSha), "🔐 sha256 metadata: '%s'", m_firmwareSha256);
            log(logSha);
        }
        log(strlen(m_firmwareSignature) > 0
                ? "🔐 Signature ECDSA présente dans metadata"
                : "ℹ️ Pas de signature ECDSA dans metadata (vérif MD5 seule)");
    }

#if defined(BOARD_WROOM) || defined(BOARD_S3)
    // OTA WROOM/S3 en HTTP: convertir les URLs HTTPS → HTTP.
    // Intégrité garantie par vérification MD5 du binaire.
    downgradeToHttp(m_firmwareUrl, sizeof(m_firmwareUrl));
#endif

    // Sélection du filesystem (optionnel)
    char selFilesystemUrl[256], selFilesystemMD5[33];
    int selFilesystemSize = 0;
    if (selectFilesystemFromMetadata(doc, selFilesystemUrl, sizeof(selFilesystemUrl), selFilesystemSize, selFilesystemMD5, sizeof(selFilesystemMD5))) {
        strncpy(m_filesystemUrl, selFilesystemUrl, sizeof(m_filesystemUrl) - 1);
        m_filesystemUrl[sizeof(m_filesystemUrl) - 1] = '\0';
#if defined(BOARD_WROOM) || defined(BOARD_S3)
        downgradeToHttp(m_filesystemUrl, sizeof(m_filesystemUrl));
#endif
        m_filesystemSize = selFilesystemSize;
        strncpy(m_filesystemMD5, selFilesystemMD5, sizeof(m_filesystemMD5) - 1);
        m_filesystemMD5[sizeof(m_filesystemMD5) - 1] = '\0';
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "📁 Filesystem trouvé: '%s'", m_filesystemUrl);
        log(logMsg);
        char sizeBuf[16];
        formatBytes(m_filesystemSize, sizeBuf, sizeof(sizeBuf));
        snprintf(logMsg, sizeof(logMsg), "📁 Taille filesystem: %s", sizeBuf);
        log(logMsg);
        if (strlen(m_filesystemMD5) > 0) {
            char logMsgMD5[64];
            snprintf(logMsgMD5, sizeof(logMsgMD5), "🔐 MD5 filesystem: '%s'", m_filesystemMD5);
            log(logMsgMD5);
        }
    } else {
        m_filesystemUrl[0] = '\0';
        m_filesystemSize = 0;
        m_filesystemMD5[0] = '\0';
        log("ℹ️ Aucun filesystem à mettre à jour");
    }
    
    char logMsgVersion[128];
    snprintf(logMsgVersion, sizeof(logMsgVersion), "📋 Version distante: '%s'", m_remoteVersion);
    log(logMsgVersion);
    snprintf(logMsgVersion, sizeof(logMsgVersion), "📋 Version locale: '%s'", m_currentVersion);
    log(logMsgVersion);
    snprintf(logMsgVersion, sizeof(logMsgVersion), "📋 URL firmware: '%s'", m_firmwareUrl);
    log(logMsgVersion);
    char sizeBufFw[16];
    formatBytes(m_firmwareSize, sizeBufFw, sizeof(sizeBufFw));
    snprintf(logMsgVersion, sizeof(logMsgVersion), "📋 Taille firmware: %s", sizeBufFw);
    log(logMsgVersion);
    if (strlen(m_firmwareMD5) > 0) {
        snprintf(logMsgVersion, sizeof(logMsgVersion), "🔐 MD5: '%s'", m_firmwareMD5);
        log(logMsgVersion);
    }
    
    // Comparaison de version
    int versionCompare = compareVersions(m_remoteVersion, m_currentVersion);
    snprintf(logMsgVersion, sizeof(logMsgVersion), "🔄 Résultat comparaison: %d (0=égal, >0=nouvelle, <0=ancienne)", versionCompare);
    log(logMsgVersion);
    
    if (versionCompare <= 0) {
        snprintf(logMsgVersion, sizeof(logMsgVersion), "✅ Aucune mise à jour: distant=%s, local=%s",
                 m_remoteVersion, m_currentVersion);
        log(logMsgVersion);
        return false;
    }

    char logMsgNewVer[128];
    snprintf(logMsgNewVer, sizeof(logMsgNewVer), "🆕 Nouvelle version %s trouvée (courante %s)", m_remoteVersion, m_currentVersion);
    log(logMsgNewVer);
    
    // Vérification de la taille
    if (m_firmwareSize <= 0) {
        log("⚠️ Taille firmware non spécifiée dans metadata, utilisation taille partition OTA");
        m_firmwareSize = static_cast<int>(OTAConfig::OTA_APP_PARTITION_SIZE);
    }
    
    if (!validateSpace(m_firmwareSize)) {
        return false;
    }
    
    char freeSpaceBuf[16], firmwareSizeBuf2[16];
    formatBytes(ESP.getFreeSketchSpace(), freeSpaceBuf, sizeof(freeSpaceBuf));
    formatBytes(m_firmwareSize, firmwareSizeBuf2, sizeof(firmwareSizeBuf2));
    char logMsgSpace[128];
    snprintf(logMsgSpace, sizeof(logMsgSpace), "✅ Espace suffisant: %s >= %s", freeSpaceBuf, firmwareSizeBuf2);
    log(logMsgSpace);
    
    m_otaLock = true;
    log("🔒 Mode OTA exclusif activé (nouvelle version détectée)");
    m_lastCheck = millis();
    return true;
}

bool OTAManager::performUpdate() {
    if (strlen(m_firmwareUrl) == 0) {
        logError("Aucune URL de firmware disponible");
        m_otaLock = false;
        return false;
    }
    
    if (m_updateTaskHandle != nullptr) {
        logError("Tâche OTA_Update déjà en cours");
        return false;
    }
    
  TaskMonitor::Snapshot prepareSnapshot = TaskMonitor::collectSnapshot();
  TaskMonitor::logSnapshot(prepareSnapshot, "ota-perform");
  TaskMonitor::detectAnomalies(prepareSnapshot, "ota-perform");
  OTA_LOG("perform start remote=%s url=%s size=%d",
          m_remoteVersion, m_firmwareUrl, m_firmwareSize);

    // WROOM : téléchargement firmware OTA en HTTP (pas mbedTLS) — seuil heap OTA, pas TLS.
#if defined(BOARD_WROOM)
    constexpr uint32_t kMinHeapForOtaTask = HeapConfig::MIN_HEAP_OTA;
#else
    constexpr uint32_t kMinHeapForOtaTask = TLS_MIN_HEAP_BYTES;
#endif
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < kMinHeapForOtaTask) {
        char heapBuf[16];
        formatBytes(freeHeap, heapBuf, sizeof(heapBuf));
        char logMsg[96];
        snprintf(logMsg, sizeof(logMsg),
                 "Heap insuffisant pour créer la tâche OTA: %s (< %u bytes requis)",
                 heapBuf,
                 (unsigned)kMinHeapForOtaTask);
        logError(logMsg);
        m_otaLock = false;
        return false;
    }

    // m_otaLock déjà levé par checkForUpdate() à la détection
    
    // Créer une tâche dédiée pour l'OTA
    BaseType_t result = xTaskCreatePinnedToCore(
        updateTask,           // Fonction de la tâche
        "OTA_Update",         // Nom de la tâche
        TaskConfig::OTA_TASK_STACK_SIZE,                // Taille de la stack (augmentée pour stabilité)
        this,                 // Paramètre passé à la tâche
        TaskConfig::OTA_TASK_PRIORITY_WHILE_RUNNING,    // Priorité absolue (otaTask + OTA_Update)
        &m_updateTaskHandle,  // Handle de la tâche
        1                     // Core 1 (WiFi tourne surtout sur core 0)
    );
    
    if (result != pdPASS) {
        logError("Échec création tâche OTA");
        m_otaLock = false;
        return false;
    }
    
    log("✅ Tâche OTA créée avec succès");
    return true;
}

bool OTAManager::isOtaExclusive() const {
    return m_otaLock;
}

bool OTAManager::isUpdating() const {
    return m_otaLock;
}

unsigned long OTAManager::getLastCheck() const {
    return m_lastCheck;
}

int OTAManager::getFirmwareSize() const {
    return m_firmwareSize;
}

const char* OTAManager::getFirmwareUrl() const {
    return m_firmwareUrl;
}

int OTAManager::compareVersions(const char* version1, const char* version2) {
    // Tableaux fixes (pas de heap) - versions type "1.2.3" => au plus 8 composants
    static constexpr size_t MAX_VERSION_PARTS = 8;
    int v1_parts[MAX_VERSION_PARTS];
    int v2_parts[MAX_VERSION_PARTS];
    size_t v1_count = 0, v2_count = 0;

    char v1_copy[32];
    strncpy(v1_copy, version1, sizeof(v1_copy) - 1);
    v1_copy[sizeof(v1_copy) - 1] = '\0';
    char* token = strtok(v1_copy, ".");
    while (token != NULL && v1_count < MAX_VERSION_PARTS) {
        v1_parts[v1_count++] = atoi(token);
        token = strtok(NULL, ".");
    }

    char v2_copy[32];
    strncpy(v2_copy, version2, sizeof(v2_copy) - 1);
    v2_copy[sizeof(v2_copy) - 1] = '\0';
    token = strtok(v2_copy, ".");
    while (token != NULL && v2_count < MAX_VERSION_PARTS) {
        v2_parts[v2_count++] = atoi(token);
        token = strtok(NULL, ".");
    }

    size_t maxParts = (v1_count > v2_count) ? v1_count : v2_count;
    for (size_t i = 0; i < maxParts; i++) {
        int v1_part = (i < v1_count) ? v1_parts[i] : 0;
        int v2_part = (i < v2_count) ? v2_parts[i] : 0;
        if (v1_part < v2_part) return -1;
        if (v1_part > v2_part) return 1;
    }
    return 0;
}

void OTAManager::formatBytes(size_t bytes, char* buffer, size_t bufferSize) {
    if (bytes < 1024) {
        snprintf(buffer, bufferSize, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, bufferSize, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buffer, bufferSize, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
}

void OTAManager::formatSpeed(float speed, char* buffer, size_t bufferSize) {
    if (speed < 1024) {
        snprintf(buffer, bufferSize, "%.1f KB/s", speed);
    } else {
        snprintf(buffer, bufferSize, "%.1f MB/s", speed / 1024.0);
    }
} 