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


namespace {
bool hasOtaPartition() {
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}
} // namespace

OTAManager::OTAManager() 
    : m_isUpdating(false)
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
    Serial.printf("[OTA] %s\n", message);
    Serial.printf("[Event] OTA: %s\n", message);
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}

void OTAManager::logError(const char* error) {
    Serial.printf("[OTA] ❌ %s\n", error);
    Serial.printf("[Event] OTA ERROR: %s\n", error);
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

bool OTAManager::downloadMetadata(char* payload, size_t payloadSize) {
    log("🔍 Début de la vérification des mises à jour...");
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT avant GET métadonnées (TLS peut bloquer > 30s)
    }
    HTTPClient http;
    char metadataUrl[256];
    OTAConfig::getMetadataUrl(metadataUrl, sizeof(metadataUrl));
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "📡 URL métadonnées: %s", metadataUrl);
    log(logMsg);

    if (!http.begin(metadataUrl)) {
        logError("Échec initialisation HTTPClient");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);
    snprintf(logMsg, sizeof(logMsg), "⏱️ Timeout HTTP: %d ms", OTAConfig::HTTP_TIMEOUT);
    log(logMsg);

    const int MAX_METADATA_RETRIES = 2;  // 1 tentative initiale + 2 retries
    int code = -1;
    for (int attempt = 0; attempt <= MAX_METADATA_RETRIES; attempt++) {
        if (attempt > 0) {
            log("🔄 Retry GET metadata...");
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1s entre tentatives
        }
        code = http.GET();
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        snprintf(logMsg, sizeof(logMsg), "📡 Code de réponse HTTP: %d (tentative %d)", code, attempt + 1);
        log(logMsg);
        if (code == HTTP_CODE_OK) break;
        // Retry sur erreurs temporaires : 5xx, -1 (échec connexion), 0 (timeout)
        if (attempt < MAX_METADATA_RETRIES && (code >= 500 || code == -1 || code == 0)) {
            continue;
        }
        break;
    }

    if (code != HTTP_CODE_OK) {
        snprintf(logMsg, sizeof(logMsg), "Erreur GET métadonnées: %d", code);
        logError(logMsg);
        http.end();
        return false;
    }

    // Lecture du payload dans un buffer statique
    const size_t MAX_PAYLOAD_SIZE = 4096;
    char tempPayload[MAX_PAYLOAD_SIZE];
    WiFiClient* stream = http.getStreamPtr();
    size_t payloadLen = 0;
    if (stream) {
      // v11.178: Ajout timeout pour éviter blocage infini (audit bugs-high)
      unsigned long streamStart = millis();
      const unsigned long STREAM_TIMEOUT_MS = 5000;
      while (stream->available() && payloadLen < MAX_PAYLOAD_SIZE - 1 
             && (millis() - streamStart) < STREAM_TIMEOUT_MS) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();
        }
        size_t bytesRead = stream->readBytes(tempPayload + payloadLen, MAX_PAYLOAD_SIZE - payloadLen - 1);
        payloadLen += bytesRead;
      }
      tempPayload[payloadLen] = '\0';
      // Détection troncation : buffer plein et données restantes
      if (payloadLen >= MAX_PAYLOAD_SIZE - 1 && stream->available() > 0) {
          log("⚠️ Payload metadata tronqué (taille max dépassée), JSON peut être invalide");
      }
    } else {
      // v11.180: Suppression getString() - cause crashes LoadProhibited dans destructeur String
      log("⚠️ Pas de stream HTTP disponible");
      tempPayload[0] = '\0';
      payloadLen = 0;
    }
    http.end();
    
    if (payloadLen >= payloadSize) {
        payloadLen = payloadSize - 1;
        log("⚠️ Payload metadata tronqué (buffer sortie insuffisant)");
    }
    strncpy(payload, tempPayload, payloadLen);
    payload[payloadLen] = '\0';
    
    snprintf(logMsg, sizeof(logMsg), "📄 Taille payload: %zu bytes", payloadLen);
    log(logMsg);
    snprintf(logMsg, sizeof(logMsg), "📄 Payload: %s", payload);
    log(logMsg);

    return true;
}

// Nouvelle méthode utilisant esp_http_client pour plus de stabilité
bool OTAManager::downloadFirmwareModern(const char* url, size_t expectedSize) {
    log("📥 Début du téléchargement moderne du firmware...");
    
    // Configuration du client HTTP moderne - CORRIGÉE
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = NetworkConfig::OTA_CONNECT_TIMEOUT_MS; // 10s phase connexion (sous TWDT), fallback si échec
    config.buffer_size = BufferConfig::HTTP_BUFFER_SIZE; // Buffers augmentés pour débit
    config.buffer_size_tx = BufferConfig::HTTP_TX_BUFFER_SIZE;
    config.skip_cert_common_name_check = true; // Tolérer écart nom commun (ex. IP vs hostname)
    // Vérification serveur (requis ESP-IDF 5.x). Arduino-ESP32 : arduino_esp_crt_bundle_attach ; IDF/S3 : esp_crt_bundle_attach
#if defined(BOARD_S3)
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#endif
    config.disable_auto_redirect = false; // Autoriser les redirections
    config.max_redirection_count = 3; // Max 3 redirections
    config.user_data = NULL;
    config.user_agent = NetworkConfig::HTTP_USER_AGENT;
    
    // Vérification de la connectivité WiFi avant d'initialiser
    if (WiFi.status() != WL_CONNECTED) {
        logError("WiFi non connecté pour esp_http_client");
        return false;
    }
    
    // Diagnostic de la connectivité
    log("📡 Diagnostic WiFi:");
    char logMsg[128];
    char ssidBuf[33];
    WiFiHelpers::getSSID(ssidBuf, sizeof(ssidBuf));
    snprintf(logMsg, sizeof(logMsg), "  - SSID: %s", ssidBuf);
    log(logMsg);
    snprintf(logMsg, sizeof(logMsg), "  - RSSI: %d dBm", WiFi.RSSI());
    log(logMsg);
    IPAddress localIP = WiFi.localIP();
    snprintf(logMsg, sizeof(logMsg), "  - IP: %d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
    log(logMsg);
    IPAddress gatewayIP = WiFi.gatewayIP();
    snprintf(logMsg, sizeof(logMsg), "  - Gateway: %d.%d.%d.%d", gatewayIP[0], gatewayIP[1], gatewayIP[2], gatewayIP[3]);
    log(logMsg);
    IPAddress dnsIP = WiFi.dnsIP();
    snprintf(logMsg, sizeof(logMsg), "  - DNS: %d.%d.%d.%d", dnsIP[0], dnsIP[1], dnsIP[2], dnsIP[3]);
    log(logMsg);
    snprintf(logMsg, sizeof(logMsg), "  - Heap libre: %u bytes", ESP.getFreeHeap());
    log(logMsg);
    
    // Nettoyer l'ancien client s'il existe
    if (m_httpClient) {
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
    }
    
    m_httpClient = esp_http_client_init(&config);
    if (!m_httpClient) {
        logError("Échec initialisation esp_http_client");
        return false;
    }
    
    // Configuration des headers AVANT l'ouverture
    esp_http_client_set_header(m_httpClient, "User-Agent", NetworkConfig::HTTP_USER_AGENT);
    esp_http_client_set_header(m_httpClient, "Accept", "application/octet-stream");
    esp_http_client_set_header(m_httpClient, "Connection", "keep-alive");
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT avant open (TLS handshake peut bloquer)
    }
    // Début de la requête avec gestion d'erreur améliorée
    esp_err_t err = esp_http_client_open(m_httpClient, 0);
    if (err != ESP_OK) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur ouverture HTTP: %s (code: %d)", esp_err_to_name(err), err);
        logError(errorMsg);
        snprintf(errorMsg, sizeof(errorMsg), "URL: %s", url);
        logError(errorMsg);
        snprintf(errorMsg, sizeof(errorMsg), "WiFi status: %d", WiFi.status());
        logError(errorMsg);
        snprintf(errorMsg, sizeof(errorMsg), "Heap libre: %u", ESP.getFreeHeap());
        logError(errorMsg);
        
        // Fallback vers HTTPClient classique
        log("🔄 Fallback vers HTTPClient classique...");
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return downloadFirmware(url, expectedSize);
    }
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT après open, avant fetch_headers
    }
    // Récupération des informations de la réponse
    int statusCode = esp_http_client_fetch_headers(m_httpClient);
    if (statusCode != NetworkConfig::HTTP_OK_CODE) {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur HTTP: %d", statusCode);
        logError(errorMsg);
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return false;
    }
    
    int contentLength = esp_http_client_get_content_length(m_httpClient);
    char sizeBuf[16];
    formatBytes(contentLength, sizeBuf, sizeof(sizeBuf));
    snprintf(logMsg, sizeof(logMsg), "📊 Taille réelle du firmware (entête): %s", sizeBuf);
    log(logMsg);
    
    // Validation de la taille (désactivable)
    if (contentLength <= 0) {
        if (OTAConfig::OTA_UNSAFE_FORCE) {
            log("⚠️ OTA_UNSAFE_FORCE: Content-Length manquant/0, on continue en mode stream continu");
            contentLength = INT_MAX; // boucle jusqu'à fin de stream
        } else {
            char errorMsg[64];
            snprintf(errorMsg, sizeof(errorMsg), "Taille de contenu invalide: %d", contentLength);
            logError(errorMsg);
            esp_http_client_cleanup(m_httpClient);
            m_httpClient = nullptr;
            return false;
        }
    }
    
    // IMPORTANT: Sauvegarder la partition cible AVANT Update.begin() pour garantir l'alternance
    const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);
    if (!target_partition) {
        logError("Impossible de trouver la partition OTA pour la mise à jour");
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return false;
    }
    snprintf(logMsg, sizeof(logMsg), "📍 Partition cible pour mise à jour: %s (0x%x)", target_partition->label, target_partition->address);
    log(logMsg);
    
    // Initialisation de la mise à jour
    log("🔧 Initialisation de la mise à jour...");
    size_t beginSize = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)contentLength;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    if (!Update.begin(beginSize)) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Échec Update.begin(): %s", updateErrorBuf);
        logError(errorMsg);
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return false;
    }
    log("✅ Mise à jour initialisée avec succès");
    // MD5: ignoré si OTA_UNSAFE_FORCE
    if (!OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        log("⚠️ OTA_UNSAFE_FORCE: vérification MD5 ignorée");
    }
    
    // Buffer pour la lecture
    uint8_t buffer[1024];
    size_t totalWritten = 0;
    unsigned long startTime = millis();
    unsigned long lastProgressTime = startTime;
    int progressCounter = 0;
    
    log("📥 Téléchargement en cours...");
    
    while (totalWritten < (size_t)contentLength) {
        // Reset watchdog pour éviter les timeouts
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        
        // Lecture par chunks
        int bytesRead = esp_http_client_read(m_httpClient, (char*)buffer, sizeof(buffer));
        
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                log("📥 Fin du stream atteinte");
                break;
            } else {
                if (OTAConfig::OTA_UNSAFE_FORCE) {
                    char logMsg[128];
                    snprintf(logMsg, sizeof(logMsg), "⚠️ OTA_UNSAFE_FORCE: lecture terminée avec code %d, on finalise avec ce qui a été reçu", bytesRead);
                    log(logMsg);
                    break;
                }
                char errorMsg[64];
                snprintf(errorMsg, sizeof(errorMsg), "Erreur lecture HTTP: %d", bytesRead);
                logError(errorMsg);
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
        }
        
        // Écriture dans la partition OTA
        size_t written = Update.write(buffer, bytesRead);
        
        if (written != bytesRead) {
            char errorMsg[64];
            snprintf(errorMsg, sizeof(errorMsg), "Erreur écriture: %zu/%d bytes", written, bytesRead);
            logError(errorMsg);
            esp_http_client_cleanup(m_httpClient);
            m_httpClient = nullptr;
            return false;
        }
        
        totalWritten += written;
        progressCounter++;
        
        // Affichage de progression toutes les 2 secondes
        unsigned long currentTime = millis();
        if (currentTime - lastProgressTime >= TimingConfig::OTA_PROGRESS_UPDATE_INTERVAL_MS) {
            int progress;
            if (contentLength == INT_MAX) {
                // Si la taille n'est pas connue, estimer basé sur la vitesse
                unsigned long elapsed = (currentTime - startTime) / 1000;
                if (elapsed > 0) {
                    float speed = (totalWritten / 1024.0) / elapsed; // KB/s
                    // Estimation basée sur une taille moyenne de firmware (typiquement 1-2 MB)
                    size_t estimatedSize = speed > 0 ? (size_t)(speed * 120) : 1500; // Estimation conservatrice
                    progress = (int)((totalWritten * 100) / estimatedSize);
                    if (progress > 95) progress = 95; // Limiter à 95% si estimation
                } else {
                    progress = 0;
                }
            } else {
                progress = (int)((totalWritten * 100) / contentLength);
            }
            unsigned long elapsed = (currentTime - startTime) / 1000;
            float speed = (totalWritten / 1024.0) / elapsed; // KB/s
            
            logProgress(progress, totalWritten, contentLength == INT_MAX ? 0 : (size_t)contentLength, speed);
            lastProgressTime = currentTime;
        }
        
        // Vérification périodique de la mémoire
        if (progressCounter % 100 == 0) {
            size_t currentHeap = ESP.getFreeHeap();
            char heapBuf[16];
            formatBytes(currentHeap, heapBuf, sizeof(heapBuf));
            char logMsg[64];
            snprintf(logMsg, sizeof(logMsg), "📊 Heap libre: %s", heapBuf);
            log(logMsg);
            
            if (currentHeap < BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES) {
                log("⚠️ Heap critique détecté");
            }
        }
        
        // Petit délai pour éviter les blocages - utiliser vTaskDelay() dans tâche OTA
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Vérification du timeout
        if (currentTime - startTime > NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS) { // 5 minutes max
            logError("Timeout du téléchargement");
            esp_http_client_cleanup(m_httpClient);
            m_httpClient = nullptr;
            return false;
        }
    }

    esp_http_client_cleanup(m_httpClient);
    m_httpClient = nullptr;
    
    char totalBuf[16], contentBuf[16];
    formatBytes(totalWritten, totalBuf, sizeof(totalBuf));
    if (contentLength == INT_MAX) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "📥 Téléchargement terminé: %s/inconnu", totalBuf);
        log(logMsg);
    } else {
        formatBytes(contentLength, contentBuf, sizeof(contentBuf));
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "📥 Téléchargement terminé: %s/%s", totalBuf, contentBuf);
        log(logMsg);
    }
    
    // Forcer l'affichage à 100% à la fin du téléchargement
    logProgress(100, totalWritten, contentLength == INT_MAX ? 0 : (size_t)contentLength, 0);
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (contentLength != INT_MAX && totalWritten != (size_t)contentLength) {
            formatBytes(totalWritten, totalBuf, sizeof(totalBuf));
            formatBytes(contentLength, contentBuf, sizeof(contentBuf));
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Téléchargement incomplet: %s/%s", totalBuf, contentBuf);
            logError(errorMsg);
            return false;
        }
    } else {
        if (contentLength == INT_MAX) {
            formatBytes(totalWritten, totalBuf, sizeof(totalBuf));
            char logMsg[128];
            snprintf(logMsg, sizeof(logMsg), "⚠️ OTA_UNSAFE_FORCE: Content-Length inconnu, téléchargement finalisé avec %s", totalBuf);
            log(logMsg);
        } else if (totalWritten != (size_t)contentLength) {
            char logMsg[128];
            snprintf(logMsg, sizeof(logMsg), "⚠️ OTA_UNSAFE_FORCE: écart de taille ignoré: %zu/%d", totalWritten, contentLength);
            log(logMsg);
        }
    }

    // Finalisation de la mise à jour
    log("🔧 Finalisation de la mise à jour...");
    bool endOk = Update.end(OTAConfig::OTA_UNSAFE_FORCE);
    if (!endOk) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Erreur Update.end(): %s", updateErrorBuf);
        logError(errorMsg);
        return false;
    }

    // Validation de la mise à jour
    if (Update.hasError() && !OTAConfig::OTA_UNSAFE_FORCE) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Erreur de mise à jour: %s", updateErrorBuf);
        logError(errorMsg);
        return false;
    }

    // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
    // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
    if (target_partition) {
        char bootLogMsg3[128];
        snprintf(bootLogMsg3, sizeof(bootLogMsg3), "🔄 Marquage de la partition mise à jour comme boot: %s", target_partition->label);
        log(bootLogMsg3);
        esp_err_t err = esp_ota_set_boot_partition(target_partition);
        if (err != ESP_OK) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Erreur marquage partition boot: %s", esp_err_to_name(err));
            logError(errorMsg);
            return false;
        }
        snprintf(bootLogMsg3, sizeof(bootLogMsg3), "✅ Partition %s marquée comme boot avec succès", target_partition->label);
        log(bootLogMsg3);
    } else {
        logError("Partition cible non disponible pour marquage boot");
        return false;
    }

    log("✅ Mise à jour réussie !");
    return true;
}

// Méthode de fallback utilisant HTTPClient classique
bool OTAManager::downloadFirmware(const char* url, size_t expectedSize) {
    log("📥 Début du téléchargement du firmware (mode fallback)...");
    
    HTTPClient http;
    if (!http.begin(url)) {
        logError("Échec initialisation HTTPClient pour firmware");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);

    int code = -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            log("🔄 Retry GET firmware...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        code = http.GET();
        char logMsg2[128];
        snprintf(logMsg2, sizeof(logMsg2), "📡 Code de réponse firmware: %d (tentative %d)", code, attempt + 1);
        log(logMsg2);
        if (code == HTTP_CODE_OK) break;
        if (attempt == 0 && (code >= 500 || code == -1 || code == 0)) continue;
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur GET firmware: %d", code);
        logError(errorMsg);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    char logMsg2[128];
    char sizeBuf2[16];
    formatBytes(contentLength, sizeBuf2, sizeof(sizeBuf2));
    snprintf(logMsg2, sizeof(logMsg2), "📊 Taille réelle du firmware: %s", sizeBuf2);
    log(logMsg2);
    
    if (!OTAConfig::OTA_UNSAFE_FORCE && !validateFirmwareSize(expectedSize, contentLength)) {
        http.end();
        return false;
    }
    if (OTAConfig::OTA_UNSAFE_FORCE && expectedSize > 0 && expectedSize != contentLength) {
        snprintf(logMsg2, sizeof(logMsg2), "⚠️ OTA_UNSAFE_FORCE: taille inattendue ignorée: attendu=%zu, réel=%d", expectedSize, contentLength);
        log(logMsg2);
    }

    // IMPORTANT: Sauvegarder la partition cible AVANT Update.begin() pour garantir l'alternance
    const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);
    if (!target_partition) {
        logError("Impossible de trouver la partition OTA pour la mise à jour");
        http.end();
        return false;
    }
    snprintf(logMsg2, sizeof(logMsg2), "📍 Partition cible pour mise à jour: %s (0x%x)", target_partition->label, target_partition->address);
    log(logMsg2);

    // Initialisation de la mise à jour
    log("🔧 Initialisation de la mise à jour...");
    size_t beginSize2 = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)contentLength;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    
    if (!Update.begin(beginSize2)) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Erreur Update.begin(): %s", updateErrorBuf);
        logError(errorMsg);
        http.end();
        return false;
    }
    
    log("✅ Mise à jour initialisée avec succès");
    if (!OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        log("⚠️ OTA_UNSAFE_FORCE: vérification MD5 ignorée");
    }

    // Téléchargement par chunks
    WiFiClient* stream = http.getStreamPtr();
    size_t totalWritten = 0;
    uint8_t buffer[1024];
    int bytesRead;
    int progressCounter = 0;
    unsigned long startTime = millis();
    unsigned long lastProgressTime = startTime;
    // v11.178: Timeout global pour éviter blocage infini (audit bugs-high)
    const unsigned long OTA_DOWNLOAD_TIMEOUT_MS = NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS;

    log("📥 Téléchargement en cours...");
    
    while (totalWritten < contentLength && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0
           && (millis() - startTime) < OTA_DOWNLOAD_TIMEOUT_MS) {
        // Reset watchdog pour éviter les timeouts
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        
        size_t written = Update.write(buffer, bytesRead);
        
        if (written != bytesRead) {
            char errorMsg[64];
            snprintf(errorMsg, sizeof(errorMsg), "Erreur écriture: %zu/%d bytes", written, bytesRead);
            logError(errorMsg);
            http.end();
            return false;
        }
        
        totalWritten += written;
        progressCounter++;
        
        // Affichage de progression toutes les 2 secondes
        unsigned long currentTime = millis();
        if (currentTime - lastProgressTime >= TimingConfig::OTA_PROGRESS_UPDATE_INTERVAL_MS) {
            int progress = (totalWritten * 100) / contentLength;
            unsigned long elapsed = (currentTime - startTime) / 1000;
            float speed = (totalWritten / 1024.0) / elapsed; // KB/s
            
            logProgress(progress, totalWritten, contentLength, speed);
            lastProgressTime = currentTime;
        }
        
        // Vérification périodique de la mémoire
        if (progressCounter % 100 == 0) {
            size_t currentHeap = ESP.getFreeHeap();
            char heapBuf3[16];
            formatBytes(currentHeap, heapBuf3, sizeof(heapBuf3));
            char logMsg3[64];
            snprintf(logMsg3, sizeof(logMsg3), "📊 Heap libre: %s", heapBuf3);
            log(logMsg3);
            
            if (currentHeap < BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES) {
                log("⚠️ Heap critique détecté");
            }
        }
        
        // Petit délai pour éviter les blocages - utiliser vTaskDelay() dans tâche OTA
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Vérification du timeout
        if (currentTime - startTime > NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS) { // 5 minutes max
            logError("Timeout du téléchargement");
            http.end();
            return false;
        }
    }

    http.end();
    
    char totalBuf3[16], contentBuf3[16];
    formatBytes(totalWritten, totalBuf3, sizeof(totalBuf3));
    formatBytes(contentLength, contentBuf3, sizeof(contentBuf3));
    char logMsg3[128];
    snprintf(logMsg3, sizeof(logMsg3), "📥 Téléchargement terminé: %s/%s", totalBuf3, contentBuf3);
    log(logMsg3);
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (totalWritten != contentLength) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Téléchargement incomplet: %s/%s", totalBuf3, contentBuf3);
            logError(errorMsg);
            return false;
        }
    } else {
        if (totalWritten != contentLength) {
            snprintf(logMsg3, sizeof(logMsg3), "⚠️ OTA_UNSAFE_FORCE: écart de taille ignoré: %zu/%d", totalWritten, contentLength);
            log(logMsg3);
        }
    }

    // Finalisation de la mise à jour
    log("🔧 Finalisation de la mise à jour...");
    bool endOk2 = Update.end(OTAConfig::OTA_UNSAFE_FORCE);
    
    if (!endOk2) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Erreur Update.end(): %s", updateErrorBuf);
        logError(errorMsg);
        return false;
    }

    // Validation de la mise à jour
    if (Update.hasError() && !OTAConfig::OTA_UNSAFE_FORCE) {
        char errorMsg[128];
        char updateErrorBuf[64];
        strncpy(updateErrorBuf, Update.errorString(), sizeof(updateErrorBuf) - 1);
        updateErrorBuf[sizeof(updateErrorBuf) - 1] = '\0';
        snprintf(errorMsg, sizeof(errorMsg), "Erreur de mise à jour: %s", updateErrorBuf);
        logError(errorMsg);
        return false;
    }

    // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
    // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
    if (target_partition) {
        char bootLogMsg2[128];
        snprintf(bootLogMsg2, sizeof(bootLogMsg2), "🔄 Marquage de la partition mise à jour comme boot: %s", target_partition->label);
        log(bootLogMsg2);
        esp_err_t err = esp_ota_set_boot_partition(target_partition);
        if (err != ESP_OK) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Erreur marquage partition boot: %s", esp_err_to_name(err));
            logError(errorMsg);
            return false;
        }
        snprintf(bootLogMsg2, sizeof(bootLogMsg2), "✅ Partition %s marquée comme boot avec succès", target_partition->label);
        log(bootLogMsg2);
    } else {
        logError("Partition cible non disponible pour marquage boot");
        return false;
    }

    log("✅ Mise à jour réussie !");
    return true;
}

// Tâche dédiée pour l'OTA
void OTAManager::updateTask(void* parameter) {
    OTAManager* ota = static_cast<OTAManager*>(parameter);
    ota->log("🚀 Démarrage tâche OTA");

    TaskMonitor::Snapshot baselineSnapshot = TaskMonitor::collectSnapshot();
    TaskMonitor::logSnapshot(baselineSnapshot, "ota-task-start");
    TaskMonitor::detectAnomalies(baselineSnapshot, "ota-task-start");
    
    // Diagnostic des partitions AVANT la mise à jour
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    
    ota->log("📊 État des partitions AVANT mise à jour:");
    char logMsgTask[128];
    if (running) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Partition en cours: %s (0x%x)", running->label, running->address);
        ota->log(logMsgTask);
    }
    if (boot) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Partition de boot: %s (0x%x)", boot->label, boot->address);
        ota->log(logMsgTask);
    }
    if (next) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Prochaine partition OTA: %s (0x%x)", next->label, next->address);
        ota->log(logMsgTask);
    }
    
    // Email de début d'OTA (serveur distant)
    extern Mailer mailer;
    extern Automatism g_autoCtrl;
    bool emailEnabled = g_autoCtrl.isEmailEnabled();
    const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
    char body[256];
    char sizeBufEmail[16];
    formatBytes(ota->getFirmwareSize(), sizeBufEmail, sizeof(sizeBufEmail));
    snprintf(body, sizeof(body),
             "OTA distant démarré\n\nVersion courante: %s\nVersion distante: %s\nEnvironnement: %s\nTaille firmware: %s",
             ota->getCurrentVersion(), ota->getRemoteVersion(), Utils::getProfileName(), sizeBufEmail);
    mailer.sendAlert("OTA début - Serveur distant", body, toEmail, true);
    
    // Ajouter cette tâche au TWDT et conserver le watchdog ACTIF pendant l'OTA
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();  // Démarrer la fenêtre TWDT (évite reset avant premier feed dans download)
    ota->log("🛡️ Watchdog actif pendant OTA (reset périodique)");

    // Persister l'ancienne version pour notification post-reboot
    {
        g_nvsManager.saveString(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_PREV_VER, ota->getCurrentVersion());
        // Clé harmonisée (snake_case)
        g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_IN_PROGRESS, true);
        // Migration: supprimer l'ancienne clé si elle existe
        g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "ota_inProgress");
    }
    
    // Essayer d'abord la méthode moderne, puis fallback si échec
    bool success = ota->downloadFirmwareModern(ota->m_firmwareUrl, ota->m_firmwareSize);
    
    // Si la méthode moderne échoue, essayer la méthode classique
    if (!success) {
        ota->log("🔄 Méthode moderne échouée, tentative avec HTTPClient classique...");
        success = ota->downloadFirmware(ota->m_firmwareUrl, ota->m_firmwareSize);
    }
    
    // Fallback 3 : méthode micro-chunks (retry par chunk)
    if (!success) {
        ota->log("🔄 Fallback micro-chunks...");
        success = ota->downloadFirmwareUltraRevolutionary(ota->m_firmwareUrl, ota->m_firmwareSize);
    }
    
    // Si le firmware a été mis à jour avec succès, essayer de mettre à jour le filesystem
    if (success) {
        ota->log("✅ Mise à jour firmware réussie, vérification du filesystem...");
        
        // Mise à jour du filesystem si disponible
        if (strlen(ota->m_filesystemUrl) > 0) {
            ota->log("📁 Mise à jour du filesystem en cours...");
            bool filesystemSuccess = ota->downloadFilesystem(ota->m_filesystemUrl, ota->m_filesystemSize, ota->m_filesystemMD5);
            if (filesystemSuccess) {
                ota->log("✅ Mise à jour filesystem réussie");
            } else {
                ota->log("⚠️ Échec mise à jour filesystem, mais firmware mis à jour");
                // On continue même si le filesystem échoue
            }
        } else {
            ota->log("ℹ️ Aucun filesystem à mettre à jour");
        }
    }
    
    if (success) {
        extern Diagnostics diag;
        diag.recordOtaResult(true, nullptr);
        ota->log("🎉 Mise à jour OTA réussie");
        
        // Diagnostic des partitions APRÈS la mise à jour
        const esp_partition_t* new_running = esp_ota_get_running_partition();
        const esp_partition_t* new_boot = esp_ota_get_boot_partition();
        const esp_partition_t* new_next = esp_ota_get_next_update_partition(NULL);
        
        ota->log("📊 État des partitions APRÈS mise à jour:");
        if (new_running) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Partition en cours: %s (0x%x)", new_running->label, new_running->address);
            ota->log(logMsgPart);
        }
        if (new_boot) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Partition de boot (prochaine): %s (0x%x)", new_boot->label, new_boot->address);
            ota->log(logMsgPart);
        }
        if (new_next) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Prochaine partition OTA: %s (0x%x)", new_next->label, new_next->address);
            ota->log(logMsgPart);
        }

        // OLED: masquer l'overlay et afficher 100% et partitions avant reboot (via display du contexte)
        if (ota->m_display && ota->m_display->isPresent()) {
            ota->m_display->hideOtaProgressOverlay();
            ota->m_display->lockScreen(2000);
            const esp_partition_t* prev_running = esp_ota_get_running_partition();
            const char* fromLbl = prev_running ? prev_running->label : "?";
            const char* toLbl   = new_boot ? new_boot->label : "?";
            const char* curV = ProjectConfig::VERSION;
            const char* newV = ota->getRemoteVersion();
            ota->m_display->showOtaProgressEx(100, fromLbl, toLbl, "Terminé", curV, newV, "OTA");
        }

        // Email de fin d'OTA (succès) avant reboot
        {
            extern Mailer mailer;
            extern Automatism g_autoCtrl;
            bool emailEnabled = g_autoCtrl.isEmailEnabled();
            const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
            char firmwareSizeBuf[16];
            formatBytes(ota->getFirmwareSize(), firmwareSizeBuf, sizeof(firmwareSizeBuf));
            char body[512];
            snprintf(body, sizeof(body),
                     "OTA distant terminé\n\nAncienne version: %s\nNouvelle version: %s\nEnvironnement: %s\nTaille firmware: %s",
                     ota->getCurrentVersion(), ota->getRemoteVersion(), Utils::getProfileName(), firmwareSizeBuf);
            mailer.sendAlert("OTA fin - Serveur distant", body, toEmail, true);
        }
        
        // Nettoyer le flag inProgress avant reboot
        {
            // Clé harmonisée (snake_case)
            g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_IN_PROGRESS, false);
            // Migration: supprimer l'ancienne clé si elle existe
            g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "ota_inProgress");
        }
        TaskMonitor::Snapshot successSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(successSnapshot, "ota-task-success");
        TaskMonitor::logDiff(baselineSnapshot, successSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(successSnapshot, "ota-task-success");
        Serial.printf("[Event] OTA success %s -> %s\n",
                       ota->getCurrentVersion(),
                       ota->getRemoteVersion());

        ota->log("🔄 Redémarrage dans 3 secondes...");
        // Utiliser vTaskDelay() avec reset watchdog pour respecter la règle "jamais bloquer > 3s"
        for (int i = 0; i < 6; i++) {
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP.restart();
    } else {
        extern Diagnostics diag;
        diag.recordOtaResult(false, "download/update failed");
        ota->log("❌ Échec mise à jour OTA");
        ota->m_isUpdating = false;
        TaskMonitor::Snapshot failureSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(failureSnapshot, "ota-task-failure");
        TaskMonitor::logDiff(baselineSnapshot, failureSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(failureSnapshot, "ota-task-failure");
        Serial.printf("[Event] OTA failure %s -> %s\n",
                       ota->getCurrentVersion(),
                       ota->getRemoteVersion());
        
        // Masquer l'overlay OTA en cas d'échec (via display du contexte)
        if (ota->m_display && ota->m_display->isPresent()) {
            ota->m_display->hideOtaProgressOverlay();
        }
    }
    
    vTaskDelete(NULL);
}

// Fallback micro-chunks : téléchargement par blocs 2KB avec retry par chunk
bool OTAManager::downloadFirmwareUltraRevolutionary(const char* url, size_t expectedSize) {
    log("📥 Début téléchargement fallback micro-chunks");
    char logMsgUltra[64];
    snprintf(logMsgUltra, sizeof(logMsgUltra), "📊 Taille attendue: %zu bytes", expectedSize);
    log(logMsgUltra);
    
    // Configuration micro-chunks
    const size_t MICRO_CHUNK_SIZE = 2048;  // Micro-chunks 2KB pour réduire overhead
    const int MAX_RETRIES = 3;             // Moins de retries pour accélérer l'échec
    const int MICRO_CHUNK_TIMEOUT = 8000;  // Timeout un peu plus long
    const int PAUSE_BETWEEN_CHUNKS = 10;   // Pause réduite
    // Buffer fixe réutilisé pour éviter la fragmentation du heap
    uint8_t buffer[MICRO_CHUNK_SIZE];
    
    // IMPORTANT: Sauvegarder la partition cible AVANT Update.begin() pour garantir l'alternance
    const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);
    if (!target_partition) {
        logError("Impossible de trouver la partition OTA pour la mise à jour");
        return false;
    }
    char logMsgPartition[128];
    snprintf(logMsgPartition, sizeof(logMsgPartition), "📍 Partition cible pour mise à jour: %s (0x%x)", target_partition->label, target_partition->address);
    log(logMsgPartition);
    
    // Initialisation de la mise à jour
    size_t beginSize3 = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)expectedSize;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    if (!Update.begin(beginSize3)) {
        logError("Échec initialisation Update");
        return false;
    }
    if (!OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && strlen(m_firmwareMD5) > 0) {
        log("⚠️ OTA_UNSAFE_FORCE: vérification MD5 ignorée");
    }
    
    HTTPClient http;
    http.setTimeout(MICRO_CHUNK_TIMEOUT);
    http.setReuse(true);
    http.addHeader("User-Agent", NetworkConfig::HTTP_USER_AGENT);
    http.addHeader("Accept", "application/octet-stream");
    http.addHeader("Connection", "keep-alive");
    http.addHeader("Cache-Control", "no-cache");
    
    // Début de la requête
    // v11.166: Verification retour http.begin() (audit robustesse)
    if (!http.begin(url)) {
        logError("Echec initialisation HTTPClient (micro-chunks)");
        return false;
    }
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur HTTP: %d", httpCode);
        logError(errorMsg);
        http.end();
        return false;
    }
    
    // Récupération de la taille réelle
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        logError("Taille de contenu invalide");
        http.end();
        return false;
    }
    
    char logMsgSize[64];
    snprintf(logMsgSize, sizeof(logMsgSize), "📊 Taille réelle: %d bytes", contentLength);
    log(logMsgSize);
    
    // Téléchargement par micro-chunks avec validation
    size_t totalDownloaded = 0;
    size_t chunkNumber = 0;
    bool downloadSuccess = true;
    
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        logError("Stream HTTP invalide");
        http.end();
        return false;
    }
    
    while (totalDownloaded < contentLength && downloadSuccess) {
        size_t chunkStart = totalDownloaded;
        size_t chunkEnd = std::min(chunkStart + MICRO_CHUNK_SIZE - 1, static_cast<size_t>(contentLength - 1));
        size_t chunkSize = chunkEnd - chunkStart + 1;
        
        char logMsgChunk[128];
        snprintf(logMsgChunk, sizeof(logMsgChunk), "📥 Micro-Chunk %d: %zu-%zu (%zu bytes)", chunkNumber, chunkStart, chunkEnd, chunkSize);
        log(logMsgChunk);
        
        // Tentatives multiples pour ce micro-chunk
        bool chunkSuccess = false;
        for (int retry = 0; retry < MAX_RETRIES && !chunkSuccess; retry++) {
            if (retry > 0) {
                snprintf(logMsgChunk, sizeof(logMsgChunk), "🔄 Retry %d pour micro-chunk %d", retry, chunkNumber);
                log(logMsgChunk);
                vTaskDelay(pdMS_TO_TICKS(500)); // Pause entre tentatives
            }
            
            // Reset watchdog pour ce micro-chunk
            if (esp_task_wdt_status(NULL) == ESP_OK) {
                esp_task_wdt_reset();
            }
            
            // Lecture du micro-chunk avec validation (buffer fixe réutilisé)
            size_t bytesRead = 0;
            unsigned long startTime = millis();
            
            // Lecture progressive avec validation
            while (bytesRead < chunkSize && (millis() - startTime) < MICRO_CHUNK_TIMEOUT) {
                if (stream->available()) {
                    int read = stream->read(buffer + bytesRead, chunkSize - bytesRead);
                    if (read > 0) {
                        bytesRead += read;
                        // Reset watchdog toutes les 256 bytes
                        if (bytesRead % 256 == 0 && esp_task_wdt_status(NULL) == ESP_OK) {
                            esp_task_wdt_reset();
                        }
                    } else if (read == 0) {
                        vTaskDelay(pdMS_TO_TICKS(5)); // Pause plus courte
                    } else {
                        break;
                    }
                } else {
                    delay(5); // Pause plus courte
                }
            }
            
            // Validation du micro-chunk
            if (bytesRead == chunkSize) {
                // Validation mémoire avant écriture
                if (ESP.getFreeHeap() < BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES) {
                    logError("Mémoire insuffisante pour écriture");
                    continue;
                }
                
                // Écriture du micro-chunk dans Update
                size_t written = Update.write(buffer, chunkSize);
                if (written == chunkSize) {
                    chunkSuccess = true;
                    totalDownloaded += chunkSize;
                    chunkNumber++;
                    
                    // Progression
                    int progress = (totalDownloaded * 100) / contentLength;
                    char logMsgProgress[128];
                    snprintf(logMsgProgress, sizeof(logMsgProgress), "✅ Micro-Chunk %d OK - Progression: %d%%", chunkNumber-1, progress);
                    log(logMsgProgress);
                    if (m_progressCallback) {
                        m_progressCallback(progress);
                    }
                    
                    // Validation mémoire périodique
                    if (chunkNumber % 20 == 0) {
                        char logMsgHeap[64];
                        snprintf(logMsgHeap, sizeof(logMsgHeap), "📊 Heap libre: %u bytes", ESP.getFreeHeap());
                        log(logMsgHeap);
                        // Pause de récupération
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                } else {
                    char errorMsg[64];
                    snprintf(errorMsg, sizeof(errorMsg), "Échec écriture micro-chunk: %zu/%zu", written, chunkSize);
                    logError(errorMsg);
                }
            } else {
                char errorMsg[64];
                snprintf(errorMsg, sizeof(errorMsg), "Échec lecture micro-chunk: %d/%zu", bytesRead, chunkSize);
                logError(errorMsg);
            }
            
            
            // Pause entre tentatives
            if (!chunkSuccess && retry < MAX_RETRIES - 1) {
                delay(PAUSE_BETWEEN_CHUNKS);
            }
        }
        
        if (!chunkSuccess) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Échec définitif micro-chunk %d après %d tentatives", chunkNumber, MAX_RETRIES);
            logError(errorMsg);
            downloadSuccess = false;
            break;
        }
        
        // Pause entre micro-chunks pour éviter la surcharge
        delay(PAUSE_BETWEEN_CHUNKS);
    }
    
    http.end();
    
    if (downloadSuccess && (OTAConfig::OTA_UNSAFE_FORCE || totalDownloaded == contentLength)) {
        char logMsgComplete[128];
        snprintf(logMsgComplete, sizeof(logMsgComplete), "📥 Téléchargement micro-chunks terminé: %zu bytes", totalDownloaded);
        log(logMsgComplete);
        if (!OTAConfig::OTA_UNSAFE_FORCE && totalDownloaded != contentLength) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Téléchargement incomplet: %zu/%d", totalDownloaded, contentLength);
            logError(errorMsg);
            return false;
        }
        // Finalisation avec validation
        if (Update.end(OTAConfig::OTA_UNSAFE_FORCE)) {
            log("✅ Mise à jour micro-chunks finalisée");
            
            // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
            // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
            if (target_partition) {
                char logMsgBoot[128];
                snprintf(logMsgBoot, sizeof(logMsgBoot), "🔄 Marquage de la partition mise à jour comme boot: %s", target_partition->label);
                log(logMsgBoot);
                esp_err_t err = esp_ota_set_boot_partition(target_partition);
                if (err != ESP_OK) {
                    char errorMsg[128];
                    snprintf(errorMsg, sizeof(errorMsg), "Erreur marquage partition boot: %s", esp_err_to_name(err));
                    logError(errorMsg);
                    return false;
                }
                snprintf(logMsgBoot, sizeof(logMsgBoot), "✅ Partition %s marquée comme boot avec succès", target_partition->label);
                log(logMsgBoot);
            } else {
                logError("Partition cible non disponible pour marquage boot");
                return false;
            }
            
            return true;
        } else {
            logError("Échec finalisation Update");
            return false;
        }
    } else {
        logError("Échec téléchargement micro-chunks");
        return false;
    }
}

// Méthode pour télécharger et installer le filesystem LittleFS
bool OTAManager::downloadFilesystem(const char* url, size_t expectedSize, const char* expectedMD5) {
    log("📁 Début du téléchargement du filesystem...");
    
    if (!url || strlen(url) == 0) {
        log("ℹ️ Aucune URL de filesystem fournie, on passe");
        return true; // Pas d'erreur si pas de filesystem à mettre à jour
    }
    
    HTTPClient http;
    if (!http.begin(url)) {
        logError("Échec initialisation HTTPClient pour filesystem");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);
    
    int code = http.GET();
    char logMsgCode[64];
    snprintf(logMsgCode, sizeof(logMsgCode), "📡 Code de réponse filesystem: %d", code);
    log(logMsgCode);
    
    if (code != HTTP_CODE_OK) {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur GET filesystem: %d", code);
        logError(errorMsg);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    char sizeBufFs[16];
    formatBytes(contentLength, sizeBufFs, sizeof(sizeBufFs));
    char logMsgSize[128];
    snprintf(logMsgSize, sizeof(logMsgSize), "📊 Taille réelle du filesystem: %s", sizeBufFs);
    log(logMsgSize);
    
    if (!OTAConfig::OTA_UNSAFE_FORCE && !validateFilesystemSize(expectedSize, contentLength)) {
        http.end();
        return false;
    }
    if (OTAConfig::OTA_UNSAFE_FORCE && expectedSize > 0 && expectedSize != contentLength) {
        char logMsgForce[128];
        snprintf(logMsgForce, sizeof(logMsgForce), "⚠️ OTA_UNSAFE_FORCE: taille filesystem inattendue ignorée: attendu=%zu, réel=%d", expectedSize, contentLength);
        log(logMsgForce);
    }

    // Trouver la partition LittleFS - label "spiffs" (aligné table de partition et esp_littlefs)
    const esp_partition_t* spiffs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!spiffs_partition) {
        logError("Partition spiffs non trouvée");
        http.end();
        return false;
    }
    
    char sizeBufPart[16];
    formatBytes(spiffs_partition->size, sizeBufPart, sizeof(sizeBufPart));
    char logMsgPart[128];
    snprintf(logMsgPart, sizeof(logMsgPart), "📍 Partition spiffs trouvée: %s (0x%x, %s)", spiffs_partition->label, spiffs_partition->address, sizeBufPart);
    log(logMsgPart);

    // Vérifier que le nouveau filesystem tient dans la partition
    if (contentLength > spiffs_partition->size) {
        char sizeBufFs1[16], sizeBufFs2[16];
        formatBytes(contentLength, sizeBufFs1, sizeof(sizeBufFs1));
        formatBytes(spiffs_partition->size, sizeBufFs2, sizeof(sizeBufFs2));
        char errorMsg[128];
        snprintf(errorMsg, sizeof(errorMsg), "Filesystem trop grand pour la partition: %s > %s", sizeBufFs1, sizeBufFs2);
        logError(errorMsg);
        http.end();
        return false;
    }

    // Téléchargement par chunks
    WiFiClient* stream = http.getStreamPtr();
    size_t totalWritten = 0;
    uint8_t buffer[1024];
    int bytesRead;
    int progressCounter = 0;
    unsigned long startTime = millis();
    unsigned long lastProgressTime = startTime;
    // v11.178: Timeout global pour éviter blocage infini (audit bugs-high)
    const unsigned long FS_DOWNLOAD_TIMEOUT_MS = NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS;

    log("📥 Téléchargement filesystem en cours...");
    
    // Ouvrir la partition pour écriture
    const esp_partition_t* partition_handle = spiffs_partition;
    if (partition_handle == nullptr) {
        logError("Erreur ouverture partition: partition non trouvée");
        http.end();
        return false;
    }
    
    // Effacer la partition avant écriture.
    // RISQUE: Si le téléchargement échoue (timeout, déconnexion), la partition reste partiellement
    // écrite. LittleFS peut être invalide au prochain boot. Pas de buffer temporaire possible
    // (taille ~720KB dépasse heap ESP32). En cas d'échec partiel, re-flash manuel du filesystem
    // via uploadfs peut être nécessaire.
    log("🧹 Effacement de la partition spiffs...");
    esp_err_t err = esp_partition_erase_range(partition_handle, 0, spiffs_partition->size);
    if (err != ESP_OK) {
        char errorMsg[128];
        snprintf(errorMsg, sizeof(errorMsg), "Erreur effacement partition: %s", esp_err_to_name(err));
        logError(errorMsg);
        // Pas besoin de fermer la partition avec la nouvelle API
        http.end();
        return false;
    }
    
    while (totalWritten < contentLength && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0
           && (millis() - startTime) < FS_DOWNLOAD_TIMEOUT_MS) {
        // Reset watchdog pour éviter les timeouts
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        
        // Écriture dans la partition spiffs
        err = esp_partition_write(partition_handle, totalWritten, buffer, bytesRead);
        if (err != ESP_OK) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Erreur écriture filesystem: %s", esp_err_to_name(err));
            logError(errorMsg);
            // Pas besoin de fermer la partition avec la nouvelle API
            http.end();
            return false;
        }
        
        totalWritten += bytesRead;
        progressCounter++;
        
        // Affichage de progression toutes les 2 secondes
        unsigned long currentTime = millis();
        if (currentTime - lastProgressTime >= TimingConfig::OTA_PROGRESS_UPDATE_INTERVAL_MS) {
            int progress = (totalWritten * 100) / contentLength;
            unsigned long elapsed = (currentTime - startTime) / 1000;
            float speed = (totalWritten / 1024.0) / elapsed; // KB/s
            
            logProgress(progress, totalWritten, contentLength, speed);
            lastProgressTime = currentTime;
        }
        
        // Vérification périodique de la mémoire
        if (progressCounter % 100 == 0) {
            size_t currentHeap = ESP.getFreeHeap();
            char heapBufFs[16];
            formatBytes(currentHeap, heapBufFs, sizeof(heapBufFs));
            char logMsgHeap[64];
            snprintf(logMsgHeap, sizeof(logMsgHeap), "📊 Heap libre: %s", heapBufFs);
            log(logMsgHeap);
            
            if (currentHeap < BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES) {
                log("⚠️ Heap critique détecté");
            }
        }
        
        // Petit délai pour éviter les blocages - utiliser vTaskDelay() dans tâche OTA
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Vérification du timeout
        if (currentTime - startTime > NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS) { // 5 minutes max
            logError("Timeout du téléchargement filesystem");
            // Pas besoin de fermer la partition avec la nouvelle API
            http.end();
            return false;
        }
    }

    // Pas besoin de fermer la partition avec la nouvelle API
    http.end();
    
    char totalBufFs[16], contentBufFs[16];
    formatBytes(totalWritten, totalBufFs, sizeof(totalBufFs));
    formatBytes(contentLength, contentBufFs, sizeof(contentBufFs));
    char logMsgCompleteFs[128];
    snprintf(logMsgCompleteFs, sizeof(logMsgCompleteFs), "📥 Téléchargement filesystem terminé: %s/%s", totalBufFs, contentBufFs);
    log(logMsgCompleteFs);
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (totalWritten != contentLength) {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Téléchargement filesystem incomplet: %s/%s", totalBufFs, contentBufFs);
            logError(errorMsg);
            return false;
        }
    } else {
        if (totalWritten != contentLength) {
            char logMsgForceFs[128];
            snprintf(logMsgForceFs, sizeof(logMsgForceFs), "⚠️ OTA_UNSAFE_FORCE: écart de taille filesystem ignoré: %zu/%d", totalWritten, contentLength);
            log(logMsgForceFs);
        }
    }

    // MD5 filesystem : non implémenté (nécessiterait relecture complète partition).
    // On s'appuie sur la vérification de taille et l'intégrité HTTP.
    if (expectedMD5 && strlen(expectedMD5) > 0 && OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ OTA_UNSAFE_FORCE: validation MD5 filesystem désactivée");
    }

    log("✅ Mise à jour filesystem réussie !");
    return true;
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
    if (m_isUpdating) {
        log("⚠️ Mise à jour déjà en cours");
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
    
    // Téléchargement des métadonnées
    // Note: buffer sur la stack - la tache OTA a 8KB de stack, suffisant pour 2KB + overhead
    // v11.165: Réduit de 4KB à 2KB pour économiser DRAM (audit mémoire)
    char payload[2048];
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
    
    // Sélection du filesystem (optionnel)
    char selFilesystemUrl[256], selFilesystemMD5[33];
    int selFilesystemSize = 0;
    if (selectFilesystemFromMetadata(doc, selFilesystemUrl, sizeof(selFilesystemUrl), selFilesystemSize, selFilesystemMD5, sizeof(selFilesystemMD5))) {
        strncpy(m_filesystemUrl, selFilesystemUrl, sizeof(m_filesystemUrl) - 1);
        m_filesystemUrl[sizeof(m_filesystemUrl) - 1] = '\0';
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
    
    m_lastCheck = millis();
    return true;
}

bool OTAManager::performUpdate() {
    if (strlen(m_firmwareUrl) == 0) {
        logError("Aucune URL de firmware disponible");
        return false;
    }
    
    if (m_isUpdating) {
        logError("Mise à jour déjà en cours");
        return false;
    }
    
  TaskMonitor::Snapshot prepareSnapshot = TaskMonitor::collectSnapshot();
  TaskMonitor::logSnapshot(prepareSnapshot, "ota-perform");
  TaskMonitor::detectAnomalies(prepareSnapshot, "ota-perform");
  Serial.printf("[Event] OTA perform start remote=%s url=%s size=%d\n",
                 m_remoteVersion,
                 m_firmwareUrl,
                 m_firmwareSize);

    // Vérifier qu'il reste suffisamment de heap avant de lancer une tâche OTA dédiée.
    // Cela évite des échecs de création de tâche difficiles à diagnostiquer
    // quand le heap est très fragmenté après un long uptime.
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < TLS_MIN_HEAP_BYTES) {
        char heapBuf[16];
        formatBytes(freeHeap, heapBuf, sizeof(heapBuf));
        char logMsg[96];
        snprintf(logMsg, sizeof(logMsg),
                 "Heap insuffisant pour créer la tâche OTA: %s (< %u bytes requis)",
                 heapBuf,
                 TLS_MIN_HEAP_BYTES);
        logError(logMsg);
        return false;
    }

    m_isUpdating = true;
    
    // Créer une tâche dédiée pour l'OTA
    BaseType_t result = xTaskCreatePinnedToCore(
        updateTask,           // Fonction de la tâche
        "OTA_Update",         // Nom de la tâche
        TaskConfig::OTA_TASK_STACK_SIZE,                // Taille de la stack (augmentée pour stabilité)
        this,                 // Paramètre passé à la tâche
        5,                    // Priorité élevée pour favoriser le téléchargement
        &m_updateTaskHandle,  // Handle de la tâche
        1                     // Core 1 (WiFi tourne surtout sur core 0)
    );
    
    if (result != pdPASS) {
        logError("Échec création tâche OTA");
        m_isUpdating = false;
        return false;
    }
    
    log("✅ Tâche OTA créée avec succès");
    return true;
}

bool OTAManager::isUpdating() const {
    return m_isUpdating;
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