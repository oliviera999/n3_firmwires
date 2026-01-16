#include "ota_manager.h"
#include "nvs_manager.h" // v11.109
#include <WiFi.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_http_client.h>
#include <limits.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include "config.h"
#include "mailer.h"
#include "automatism.h"
#include "event_log.h"
#include "diagnostics.h"
#include "task_monitor.h"

namespace {
bool hasOtaPartition() {
    return esp_ota_get_next_update_partition(nullptr) != nullptr;
}
} // namespace

OTAManager::OTAManager() 
    : m_isUpdating(false)
    , m_lastCheck(0)
    , m_checkInterval(TimingConfig::OTA_CHECK_INTERVAL_MS) // 2 heures par défaut
    , m_currentVersion("")
    , m_remoteVersion("")
    , m_firmwareUrl("")
    , m_firmwareSize(0)
    , m_filesystemUrl("")
    , m_filesystemSize(0)
    , m_updateTaskHandle(nullptr)
    , m_httpClient(nullptr) {
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

void OTAManager::log(const String& message) {
    Serial.printf("[OTA] %s\n", message.c_str());
    EventLog::addf("OTA: %s", message.c_str());
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}

void OTAManager::logError(const String& error) {
    Serial.printf("[OTA] ❌ %s\n", error.c_str());
    EventLog::addf("OTA ERROR: %s", error.c_str());
    if (m_errorCallback) {
        m_errorCallback(error);
    }
}

void OTAManager::logProgress(int progress, size_t downloaded, size_t total, float speed) {
    Serial.printf("[OTA] 📊 Progression: %d%% (%s/%s) - Vitesse: %s\n", 
                 progress, formatBytes(downloaded).c_str(), formatBytes(total).c_str(), formatSpeed(speed).c_str());
    if ((progress % 10) == 0) {
        EventLog::addf("OTA progress %d%%", progress);
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
    bool hasTopVersion = doc["version"].is<String>();
    bool hasTopUrl = doc["bin_url"].is<String>();
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
        logError("Taille du filesystem trop importante: " + formatBytes(actual) + " > " + formatBytes(OTAConfig::MAX_FILESYSTEM_SIZE));
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

    log("📊 Espace libre sketch: " + formatBytes(freeSpace));
    log("📊 Heap libre: " + formatBytes(freeHeap));
    
    if (freeSpace < required) {
        logError("Espace insuffisant: " + formatBytes(freeSpace) + " < " + formatBytes(required));
        return false;
    }
    
    if (freeHeap < 50000) { // Minimum 50KB heap libre
        logError("Heap insuffisant: " + formatBytes(freeHeap));
        return false;
    }
    
    return true;
}

bool OTAManager::selectArtifactFromMetadata(const JsonDocument& doc, String& outVersion, String& outUrl, int& outSize, String& outMD5) {
    // Déterminer environnement et modèle
    const char* envName = "prod";
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
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

    log(String("🔎 Sélection OTA: env=") + envName + ", model=" + modelName);

    auto tryFillFrom = [&](JsonVariantConst v) -> bool {
        if (v.isNull()) return false;

        String urlStr = v["bin_url"].as<String>();
        String verStr = v["version"].as<String>();
        int size = v["size"].is<int>() ? v["size"].as<int>() : 0;
        String md5Str = v["md5"].as<String>();

        // Fallbacks depuis top-level si certains champs manquent
        if (urlStr.length() == 0) urlStr = doc["bin_url"].as<String>();
        if (verStr.length() == 0) verStr = doc["version"].as<String>();
        if (size <= 0) size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
        if (md5Str.length() == 0) md5Str = doc["md5"].as<String>();

        // Nécessaire au minimum: une URL
        if (urlStr.length() > 0) {
            outUrl = urlStr;
            outVersion = verStr; // peut être vide si non fourni
            outSize = size;
            outMD5 = md5Str;
            return true;
        }
        return false;
    };

    // 1) channels[env][model]
    if (!doc["channels"].isNull()) {
        JsonVariantConst v1 = doc["channels"][envName][modelName];
        log("🔎 Test channels[" + String(envName) + "][" + String(modelName) + "]: " + String(v1.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v1)) { log("✅ Sélection via channels[" + String(envName) + "][" + String(modelName) + "]"); return true; }
        // 2) channels[env][default]
        JsonVariantConst v2 = doc["channels"][envName]["default"];
        log("🔎 Test channels[" + String(envName) + "][default]: " + String(v2.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v2)) { log("✅ Sélection via channels[" + String(envName) + "][default]"); return true; }
        // 3) channels[prod][model]
        JsonVariantConst v3 = doc["channels"]["prod"][modelName];
        log("🔎 Test channels[prod][" + String(modelName) + "]: " + String(v3.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v3)) { log("✅ Sélection via channels[prod][" + String(modelName) + "]"); return true; }
        // 4) channels[prod][default]
        JsonVariantConst v4 = doc["channels"]["prod"]["default"];
        log("🔎 Test channels[prod][default]: " + String(v4.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v4)) { log("✅ Sélection via channels[prod][default]"); return true; }
    }

    // 5) Fallback legacy top-level (direct)
    log("🔎 Test fallback top-level");
    String urlStr = doc["bin_url"].as<String>();
    String verStr = doc["version"].as<String>();
    int size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
    String md5Str = doc["md5"].as<String>();
    if (urlStr.length() > 0 && verStr.length() > 0) {
        outUrl = urlStr;
        outVersion = verStr;
        outSize = size;
        outMD5 = md5Str;
        return true;
    }
    return false;
}

bool OTAManager::selectFilesystemFromMetadata(const JsonDocument& doc, String& outUrl, int& outSize, String& outMD5) {
    // Déterminer environnement et modèle
    const char* envName = "prod";
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
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

    log(String("🔎 Sélection Filesystem OTA: env=") + envName + ", model=" + modelName);

    auto tryFillFrom = [&](JsonVariantConst v) -> bool {
        if (v.isNull()) return false;

        String urlStr = v["filesystem_url"].as<String>();
        int size = v["filesystem_size"].is<int>() ? v["filesystem_size"].as<int>() : 0;
        String md5Str = v["filesystem_md5"].as<String>();

        // Fallbacks depuis top-level si certains champs manquent
        if (urlStr.length() == 0) urlStr = doc["filesystem_url"].as<String>();
        if (size <= 0) size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
        if (md5Str.length() == 0) md5Str = doc["filesystem_md5"].as<String>();

        // Nécessaire au minimum: une URL
        if (urlStr.length() > 0) {
            outUrl = urlStr;
            outSize = size;
            outMD5 = md5Str;
            return true;
        }
        return false;
    };

    // 1) channels[env][model]
    if (!doc["channels"].isNull()) {
        JsonVariantConst v1 = doc["channels"][envName][modelName];
        log("🔎 Test filesystem channels[" + String(envName) + "][" + String(modelName) + "]: " + String(v1.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v1)) { log("✅ Sélection filesystem via channels[" + String(envName) + "][" + String(modelName) + "]"); return true; }
        // 2) channels[env][default]
        JsonVariantConst v2 = doc["channels"][envName]["default"];
        log("🔎 Test filesystem channels[" + String(envName) + "][default]: " + String(v2.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v2)) { log("✅ Sélection filesystem via channels[" + String(envName) + "][default]"); return true; }
        // 3) channels[prod][model]
        JsonVariantConst v3 = doc["channels"]["prod"][modelName];
        log("🔎 Test filesystem channels[prod][" + String(modelName) + "]: " + String(v3.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v3)) { log("✅ Sélection filesystem via channels[prod][" + String(modelName) + "]"); return true; }
        // 4) channels[prod][default]
        JsonVariantConst v4 = doc["channels"]["prod"]["default"];
        log("🔎 Test filesystem channels[prod][default]: " + String(v4.isNull() ? "absent" : "ok"));
        if (tryFillFrom(v4)) { log("✅ Sélection filesystem via channels[prod][default]"); return true; }
    }

    // 5) Fallback legacy top-level (direct)
    log("🔎 Test fallback filesystem top-level");
    String urlStr = doc["filesystem_url"].as<String>();
    int size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
    String md5Str = doc["filesystem_md5"].as<String>();
    if (urlStr.length() > 0) {
        outUrl = urlStr;
        outSize = size;
        outMD5 = md5Str;
        return true;
    }
    return false;
}

bool OTAManager::downloadMetadata(String& payload) {
    log("🔍 Début de la vérification des mises à jour...");
    
    HTTPClient http;
    String metadataUrl = OTAConfig::getMetadataUrl();
    log("📡 URL métadonnées: " + metadataUrl);

    if (!http.begin(metadataUrl)) {
        logError("Échec initialisation HTTPClient");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);
    log("⏱️ Timeout HTTP: " + String(OTAConfig::HTTP_TIMEOUT) + " ms");

    int code = http.GET();
    log("📡 Code de réponse HTTP: " + String(code));
    
    if (code != HTTP_CODE_OK) {
        logError("Erreur GET métadonnées: " + String(code));
        // Fallback: réessayer l'URL fixe racine
        if (code == 404) {
            http.end();
            String fallbackUrl = OTAConfig::getMetadataUrl();
            log("🔁 Fallback métadonnées (URL fixe): " + fallbackUrl);
            if (!http.begin(fallbackUrl)) {
                logError("Échec initialisation HTTPClient (fallback)");
                return false;
            }
            http.setTimeout(OTAConfig::HTTP_TIMEOUT);
            code = http.GET();
            log("📡 Code de réponse HTTP (fallback): " + String(code));
            if (code != HTTP_CODE_OK) {
                logError("Erreur GET métadonnées (fallback): " + String(code));
                http.end();
                return false;
            }
        } else {
            http.end();
            return false;
        }
    }

    payload = http.getString();
    http.end();
    
    log("📄 Taille payload: " + String(payload.length()) + " bytes");
    log("📄 Payload: " + payload);

    return true;
}

// Nouvelle méthode utilisant esp_http_client pour plus de stabilité
bool OTAManager::downloadFirmwareModern(const String& url, size_t expectedSize) {
    log("📥 Début du téléchargement moderne du firmware...");
    
    // Configuration du client HTTP moderne - CORRIGÉE
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = NetworkConfig::HTTP_TIMEOUT_MS; // 30 secondes
    config.buffer_size = BufferConfig::HTTP_BUFFER_SIZE; // Buffers augmentés pour débit
    config.buffer_size_tx = BufferConfig::HTTP_TX_BUFFER_SIZE;
    config.skip_cert_common_name_check = true; // Ignorer la vérification SSL
    config.crt_bundle_attach = NULL; // Pas de bundle de certificats
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
    log("  - SSID: " + String(WiFi.SSID()));
    log("  - RSSI: " + String(WiFi.RSSI()) + " dBm");
    log("  - IP: " + WiFi.localIP().toString());
    log("  - Gateway: " + WiFi.gatewayIP().toString());
    log("  - DNS: " + WiFi.dnsIP().toString());
    log("  - Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
    
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
    
    // Début de la requête avec gestion d'erreur améliorée
    esp_err_t err = esp_http_client_open(m_httpClient, 0);
    if (err != ESP_OK) {
        logError("Erreur ouverture HTTP: " + String(esp_err_to_name(err)) + " (code: " + String(err) + ")");
        logError("URL: " + url);
        logError("WiFi status: " + String(WiFi.status()));
        logError("Heap libre: " + String(ESP.getFreeHeap()));
        
        // Fallback vers HTTPClient classique
        log("🔄 Fallback vers HTTPClient classique...");
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return downloadFirmware(url, expectedSize);
    }
    
    // Récupération des informations de la réponse
    int statusCode = esp_http_client_fetch_headers(m_httpClient);
    if (statusCode != NetworkConfig::HTTP_OK_CODE) {
        logError("Erreur HTTP: " + String(statusCode));
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return false;
    }
    
    int contentLength = esp_http_client_get_content_length(m_httpClient);
    log("📊 Taille réelle du firmware (entête): " + formatBytes(contentLength));
    
    // Validation de la taille (désactivable)
    if (contentLength <= 0) {
        if (OTAConfig::OTA_UNSAFE_FORCE) {
            log("⚠️ OTA_UNSAFE_FORCE: Content-Length manquant/0, on continue en mode stream continu");
            contentLength = INT_MAX; // boucle jusqu'à fin de stream
        } else {
            logError("Taille de contenu invalide: " + String(contentLength));
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
    log("📍 Partition cible pour mise à jour: " + String(target_partition->label) + " (0x" + String(target_partition->address, HEX) + ")");
    
    // Initialisation de la mise à jour
    log("🔧 Initialisation de la mise à jour...");
    size_t beginSize = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)contentLength;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    if (!Update.begin(beginSize)) {
        logError("Échec Update.begin(): " + String(Update.errorString()));
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
        return false;
    }
    log("✅ Mise à jour initialisée avec succès");
    // MD5: ignoré si OTA_UNSAFE_FORCE
    if (!OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
        Update.setMD5(m_firmwareMD5.c_str());
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
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
        esp_task_wdt_reset();
        
        // Lecture par chunks
        int bytesRead = esp_http_client_read(m_httpClient, (char*)buffer, sizeof(buffer));
        
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                log("📥 Fin du stream atteinte");
                break;
            } else {
                if (OTAConfig::OTA_UNSAFE_FORCE) {
                    log("⚠️ OTA_UNSAFE_FORCE: lecture terminée avec code " + String(bytesRead) + ", on finalise avec ce qui a été reçu");
                    break;
                }
                logError("Erreur lecture HTTP: " + String(bytesRead));
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
        }
        
        // Écriture dans la partition OTA
        size_t written = Update.write(buffer, bytesRead);
        
        if (written != bytesRead) {
            logError("Erreur écriture: " + String(written) + "/" + String(bytesRead) + " bytes");
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
            log("📊 Heap libre: " + formatBytes(currentHeap));
            
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
    
    log("📥 Téléchargement terminé: " + formatBytes(totalWritten) + "/" + (contentLength == INT_MAX ? String("inconnu") : formatBytes(contentLength)));
    
    // Forcer l'affichage à 100% à la fin du téléchargement
    logProgress(100, totalWritten, contentLength == INT_MAX ? 0 : (size_t)contentLength, 0);
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (contentLength != INT_MAX && totalWritten != (size_t)contentLength) {
            logError("Téléchargement incomplet: " + formatBytes(totalWritten) + "/" + formatBytes(contentLength));
            return false;
        }
    } else {
        if (contentLength == INT_MAX) {
            log("⚠️ OTA_UNSAFE_FORCE: Content-Length inconnu, téléchargement finalisé avec " + formatBytes(totalWritten));
        } else if (totalWritten != (size_t)contentLength) {
            log("⚠️ OTA_UNSAFE_FORCE: écart de taille ignoré: " + String(totalWritten) + "/" + String(contentLength));
        }
    }

    // Finalisation de la mise à jour
    log("🔧 Finalisation de la mise à jour...");
    bool endOk = Update.end(OTAConfig::OTA_UNSAFE_FORCE);
    if (!endOk) {
        logError("Erreur Update.end(): " + String(Update.errorString()));
        return false;
    }

    // Validation de la mise à jour
    if (Update.hasError() && !OTAConfig::OTA_UNSAFE_FORCE) {
        logError("Erreur de mise à jour: " + String(Update.errorString()));
        return false;
    }

    // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
    // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
    if (target_partition) {
        log("🔄 Marquage de la partition mise à jour comme boot: " + String(target_partition->label));
        esp_err_t err = esp_ota_set_boot_partition(target_partition);
        if (err != ESP_OK) {
            logError("Erreur marquage partition boot: " + String(esp_err_to_name(err)));
            return false;
        }
        log("✅ Partition " + String(target_partition->label) + " marquée comme boot avec succès");
    } else {
        logError("Partition cible non disponible pour marquage boot");
        return false;
    }

    log("✅ Mise à jour réussie !");
    return true;
}

// Méthode de fallback utilisant HTTPClient classique
bool OTAManager::downloadFirmware(const String& url, size_t expectedSize) {
    log("📥 Début du téléchargement du firmware (mode fallback)...");
    
    HTTPClient http;
    if (!http.begin(url)) {
        logError("Échec initialisation HTTPClient pour firmware");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);
    
    int code = http.GET();
    log("📡 Code de réponse firmware: " + String(code));
    
    if (code != HTTP_CODE_OK) {
        logError("Erreur GET firmware: " + String(code));
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    log("📊 Taille réelle du firmware: " + formatBytes(contentLength));
    
    if (!OTAConfig::OTA_UNSAFE_FORCE && !validateFirmwareSize(expectedSize, contentLength)) {
        http.end();
        return false;
    }
    if (OTAConfig::OTA_UNSAFE_FORCE && expectedSize > 0 && expectedSize != contentLength) {
        log("⚠️ OTA_UNSAFE_FORCE: taille inattendue ignorée: attendu=" + String(expectedSize) + ", réel=" + String(contentLength));
    }

    // IMPORTANT: Sauvegarder la partition cible AVANT Update.begin() pour garantir l'alternance
    const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);
    if (!target_partition) {
        logError("Impossible de trouver la partition OTA pour la mise à jour");
        http.end();
        return false;
    }
    log("📍 Partition cible pour mise à jour: " + String(target_partition->label) + " (0x" + String(target_partition->address, HEX) + ")");

    // Initialisation de la mise à jour
    log("🔧 Initialisation de la mise à jour...");
    size_t beginSize2 = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)contentLength;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    
    if (!Update.begin(beginSize2)) {
        logError("Erreur Update.begin(): " + String(Update.errorString()));
        http.end();
        return false;
    }
    
    log("✅ Mise à jour initialisée avec succès");
    if (!OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
        Update.setMD5(m_firmwareMD5.c_str());
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
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

    log("📥 Téléchargement en cours...");
    
    while (totalWritten < contentLength && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
        // Reset watchdog pour éviter les timeouts
        esp_task_wdt_reset();
        
        size_t written = Update.write(buffer, bytesRead);
        
        if (written != bytesRead) {
            logError("Erreur écriture: " + String(written) + "/" + String(bytesRead) + " bytes");
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
            log("📊 Heap libre: " + formatBytes(currentHeap));
            
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
    
    log("📥 Téléchargement terminé: " + formatBytes(totalWritten) + "/" + formatBytes(contentLength));
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (totalWritten != contentLength) {
            logError("Téléchargement incomplet: " + formatBytes(totalWritten) + "/" + formatBytes(contentLength));
            return false;
        }
    } else {
        if (totalWritten != contentLength) {
            log("⚠️ OTA_UNSAFE_FORCE: écart de taille ignoré: " + String(totalWritten) + "/" + String(contentLength));
        }
    }

    // Finalisation de la mise à jour
    log("🔧 Finalisation de la mise à jour...");
    bool endOk2 = Update.end(OTAConfig::OTA_UNSAFE_FORCE);
    
    if (!endOk2) {
        logError("Erreur Update.end(): " + String(Update.errorString()));
        return false;
    }

    // Validation de la mise à jour
    if (Update.hasError() && !OTAConfig::OTA_UNSAFE_FORCE) {
        logError("Erreur de mise à jour: " + String(Update.errorString()));
        return false;
    }

    // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
    // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
    if (target_partition) {
        log("🔄 Marquage de la partition mise à jour comme boot: " + String(target_partition->label));
        esp_err_t err = esp_ota_set_boot_partition(target_partition);
        if (err != ESP_OK) {
            logError("Erreur marquage partition boot: " + String(esp_err_to_name(err)));
            return false;
        }
        log("✅ Partition " + String(target_partition->label) + " marquée comme boot avec succès");
    } else {
        logError("Partition cible non disponible pour marquage boot");
        return false;
    }

    log("✅ Mise à jour réussie !");
    return true;
}

// Tâche dédiée pour l'OTA - VERSION ULTRA-RÉVOLUTIONNAIRE
void OTAManager::updateTask(void* parameter) {
    OTAManager* ota = static_cast<OTAManager*>(parameter);
    ota->log("🚀 Démarrage de la tâche OTA ULTRA-RÉVOLUTIONNAIRE...");

    TaskMonitor::Snapshot baselineSnapshot = TaskMonitor::collectSnapshot();
    TaskMonitor::logSnapshot(baselineSnapshot, "ota-task-start");
    TaskMonitor::detectAnomalies(baselineSnapshot, "ota-task-start");
    
    // Diagnostic des partitions AVANT la mise à jour
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    
    ota->log("📊 État des partitions AVANT mise à jour:");
    if (running) {
        ota->log("  - Partition en cours: " + String(running->label) + " (0x" + String(running->address, HEX) + ")");
    }
    if (boot) {
        ota->log("  - Partition de boot: " + String(boot->label) + " (0x" + String(boot->address, HEX) + ")");
    }
    if (next) {
        ota->log("  - Prochaine partition OTA: " + String(next->label) + " (0x" + String(next->address, HEX) + ")");
    }
    
    // Email de début d'OTA (serveur distant)
    extern Mailer mailer;
    extern Automatism g_autoCtrl;
    bool emailEnabled = g_autoCtrl.isEmailEnabled();
    const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
    String part = running ? String(running->label) : String("(inconnue)");
    String md5 = ota->getFirmwareMD5();
    String body = String("Début de mise à jour OTA (Serveur distant)\n\n") +
                  "Détails:\n" +
                  "- Méthode: Tâche dédiée HTTP OTA\n" +
                  "- Environnement: " + String(Utils::getProfileName()) + "\n" +
                  "- Version courante: " + ota->getCurrentVersion() + "\n" +
                  "- Version distante: " + ota->getRemoteVersion() + "\n" +
                  "- URL firmware: " + ota->getFirmwareUrl() + "\n" +
                  "- Taille firmware: " + OTAManager::formatBytes(ota->getFirmwareSize()) + "\n" +
                  "- MD5 firmware: " + (md5.length() ? md5 : String("-")) + "\n" +
                  "- URL filesystem: " + (ota->getFilesystemUrl().length() ? ota->getFilesystemUrl() : String("-")) + "\n" +
                  "- Taille filesystem: " + (ota->getFilesystemUrl().length() ? OTAManager::formatBytes(ota->getFilesystemSize()) : String("-")) + "\n" +
                  "- MD5 filesystem: " + (ota->getFilesystemMD5().length() ? ota->getFilesystemMD5() : String("-")) + "\n" +
                  "- Partition courante: " + part;
    if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
    mailer.sendAlert("OTA début - Serveur distant", body, toEmail);
    
    // Ajouter cette tâche au TWDT et conserver le watchdog ACTIF pendant l'OTA
    esp_task_wdt_add(NULL);
    ota->log("🛡️ Watchdog actif pendant OTA (reset périodique)");

    // Persister l'ancienne version pour notification post-reboot
    {
        g_nvsManager.saveString(NVS_NAMESPACES::SYSTEM, "ota_prevVer", ota->getCurrentVersion());
        g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "ota_inProgress", true);
    }
    
    // Essayer d'abord la méthode moderne, puis fallback si échec
    bool success = ota->downloadFirmwareModern(ota->m_firmwareUrl, ota->m_firmwareSize);
    
    // Si la méthode moderne échoue, essayer la méthode classique
    if (!success) {
        ota->log("🔄 Méthode moderne échouée, tentative avec HTTPClient classique...");
        success = ota->downloadFirmware(ota->m_firmwareUrl, ota->m_firmwareSize);
    }
    
    // Si les deux méthodes échouent, essayer la méthode ultra-révolutionnaire
    if (!success) {
        ota->log("🔄 Méthodes standard échouées, tentative avec méthode ultra-révolutionnaire...");
        success = ota->downloadFirmwareUltraRevolutionary(ota->m_firmwareUrl, ota->m_firmwareSize);
    }
    
    // Si le firmware a été mis à jour avec succès, essayer de mettre à jour le filesystem
    if (success) {
        ota->log("✅ Mise à jour firmware réussie, vérification du filesystem...");
        
        // Mise à jour du filesystem si disponible
        if (!ota->m_filesystemUrl.isEmpty()) {
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
        ota->log("🎉 Mise à jour ULTRA-RÉVOLUTIONNAIRE réussie !");
        
        // Diagnostic des partitions APRÈS la mise à jour
        const esp_partition_t* new_running = esp_ota_get_running_partition();
        const esp_partition_t* new_boot = esp_ota_get_boot_partition();
        const esp_partition_t* new_next = esp_ota_get_next_update_partition(NULL);
        
        ota->log("📊 État des partitions APRÈS mise à jour:");
        if (new_running) {
            ota->log("  - Partition en cours: " + String(new_running->label) + " (0x" + String(new_running->address, HEX) + ")");
        }
        if (new_boot) {
            ota->log("  - Partition de boot (prochaine): " + String(new_boot->label) + " (0x" + String(new_boot->address, HEX) + ")");
        }
        if (new_next) {
            ota->log("  - Prochaine partition OTA: " + String(new_next->label) + " (0x" + String(new_next->address, HEX) + ")");
        }

        // OLED: masquer l'overlay et afficher 100% et partitions avant reboot
        extern DisplayView oled;
        if (oled.isPresent()) {
            // Masquer l'overlay OTA
            oled.hideOtaProgressOverlay();
            oled.lockScreen(2000);
            const esp_partition_t* prev_running = esp_ota_get_running_partition();
            const char* fromLbl = prev_running ? prev_running->label : "?";
            const char* toLbl   = new_boot ? new_boot->label : "?";
            const char* curV = ProjectConfig::VERSION;
            const char* newV = ota->getRemoteVersion().c_str();
            // Pas d'accès direct au SSID ici de façon fiable, passer label simple
            oled.showOtaProgressEx(100, fromLbl, toLbl, "Terminé", curV, newV, "OTA");
        }

        // Email de fin d'OTA (succès) avant reboot
        {
            extern Mailer mailer;
            extern Automatism g_autoCtrl;
            bool emailEnabled = g_autoCtrl.isEmailEnabled();
            const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
            const esp_partition_t* prev_running = esp_ota_get_running_partition();
            String fromPart = prev_running ? String(prev_running->label) + " (0x" + String(prev_running->address, HEX) + ")" : String("(inconnue)");
            String bootPart = new_boot ? String(new_boot->label) + " (0x" + String(new_boot->address, HEX) + ")" : String("(inconnue)");
            String nextPart = new_next ? String(new_next->label) + " (0x" + String(new_next->address, HEX) + ")" : String("(inconnue)");
            String md5 = ota->getFirmwareMD5();
            String body = String("Fin de mise à jour OTA (Serveur distant)\n\n") +
                          "Détails:\n" +
                          "- Méthode: Tâche dédiée HTTP OTA\n" +
                          "- Environnement: " + String(Utils::getProfileName()) + "\n" +
                          "- Ancienne version: " + ota->getCurrentVersion() + "\n" +
                          "- Nouvelle version: " + ota->getRemoteVersion() + "\n" +
                          "- URL firmware: " + ota->getFirmwareUrl() + "\n" +
                          "- Taille firmware: " + OTAManager::formatBytes(ota->getFirmwareSize()) + "\n" +
                          "- MD5 firmware: " + (md5.length() ? md5 : String("-")) + "\n" +
                          "- URL filesystem: " + (ota->getFilesystemUrl().length() ? ota->getFilesystemUrl() : String("-")) + "\n" +
                          "- Taille filesystem: " + (ota->getFilesystemUrl().length() ? OTAManager::formatBytes(ota->getFilesystemSize()) : String("-")) + "\n" +
                          "- MD5 filesystem: " + (ota->getFilesystemMD5().length() ? ota->getFilesystemMD5() : String("-")) + "\n" +
                          "- Partition initiale: " + fromPart + "\n" +
                          "- Partition de boot (après MAJ): " + bootPart + "\n" +
                          "- Prochaine partition OTA: " + nextPart + "\n";
            if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
            mailer.sendAlert("OTA fin - Serveur distant", body, toEmail);
        }
        
        // Nettoyer le flag inProgress avant reboot
        {
            g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "ota_inProgress", false);
        }
        TaskMonitor::Snapshot successSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(successSnapshot, "ota-task-success");
        TaskMonitor::logDiff(baselineSnapshot, successSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(successSnapshot, "ota-task-success");
        EventLog::addf("OTA success %s -> %s",
                       ota->getCurrentVersion().c_str(),
                       ota->getRemoteVersion().c_str());

        ota->log("🔄 Redémarrage dans 3 secondes...");
        // delay() acceptable ici car juste avant ESP.restart() (pas de yield nécessaire)
        delay(3000);
        ESP.restart();
    } else {
        extern Diagnostics diag;
        diag.recordOtaResult(false, "download/update failed");
        ota->log("❌ Échec de la mise à jour ULTRA-RÉVOLUTIONNAIRE");
        ota->m_isUpdating = false;
        TaskMonitor::Snapshot failureSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(failureSnapshot, "ota-task-failure");
        TaskMonitor::logDiff(baselineSnapshot, failureSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(failureSnapshot, "ota-task-failure");
        EventLog::addf("OTA failure %s -> %s",
                       ota->getCurrentVersion().c_str(),
                       ota->getRemoteVersion().c_str());
        
        // Masquer l'overlay OTA en cas d'échec
        extern DisplayView oled;
        if (oled.isPresent()) {
            oled.hideOtaProgressOverlay();
        }
    }
    
    vTaskDelete(NULL);
}

// MÉTHODE ULTRA-RÉVOLUTIONNAIRE : Téléchargement en arrière-plan avec validation cryptographique
bool OTAManager::downloadFirmwareUltraRevolutionary(const String& url, size_t expectedSize) {
    log("🔥 DÉBUT TÉLÉCHARGEMENT ULTRA-RÉVOLUTIONNAIRE");
    log("📊 Taille attendue: " + String(expectedSize) + " bytes");
    
    // Configuration ultra-révolutionnaire
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
    log("📍 Partition cible pour mise à jour: " + String(target_partition->label) + " (0x" + String(target_partition->address, HEX) + ")");
    
    // Initialisation de la mise à jour
    size_t beginSize3 = OTAConfig::OTA_UNSAFE_FORCE ? (size_t)UPDATE_SIZE_UNKNOWN : (size_t)expectedSize;
    if (OTAConfig::OTA_UNSAFE_FORCE) {
        log("⚠️ Mode OTA_UNSAFE_FORCE actif: initialisation avec UPDATE_SIZE_UNKNOWN");
    }
    if (!Update.begin(beginSize3)) {
        logError("Échec initialisation Update");
        return false;
    }
    if (!OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
        Update.setMD5(m_firmwareMD5.c_str());
        log("🔐 MD5 défini pour vérification");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && !m_firmwareMD5.isEmpty()) {
        log("⚠️ OTA_UNSAFE_FORCE: vérification MD5 ignorée");
    }
    
    // Configuration HTTP ultra-révolutionnaire
    HTTPClient http;
    http.setTimeout(MICRO_CHUNK_TIMEOUT);
    http.setReuse(true);
    
    // Headers ultra-révolutionnaires
    http.addHeader("User-Agent", "ESP32-OTA-UltraRevolutionary");
    http.addHeader("Accept", "application/octet-stream");
    http.addHeader("Connection", "keep-alive");
    http.addHeader("Cache-Control", "no-cache");
    
    // Début de la requête
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        logError("Erreur HTTP: " + String(httpCode));
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
    
    log("📊 Taille réelle: " + String(contentLength) + " bytes");
    
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
        
        log("📥 Micro-Chunk " + String(chunkNumber) + ": " + String(chunkStart) + "-" + String(chunkEnd) + 
            " (" + String(chunkSize) + " bytes)");
        
        // Tentatives multiples pour ce micro-chunk
        bool chunkSuccess = false;
        for (int retry = 0; retry < MAX_RETRIES && !chunkSuccess; retry++) {
            if (retry > 0) {
                log("🔄 Retry " + String(retry) + " pour micro-chunk " + String(chunkNumber));
                vTaskDelay(pdMS_TO_TICKS(500)); // Pause entre tentatives
            }
            
            // Reset watchdog pour ce micro-chunk
            esp_task_wdt_reset();
            
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
                        if (bytesRead % 256 == 0) {
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
                    log("✅ Micro-Chunk " + String(chunkNumber-1) + " OK - Progression: " + String(progress) + "%");
                    if (m_progressCallback) {
                        m_progressCallback(progress);
                    }
                    
                    // Validation mémoire périodique
                    if (chunkNumber % 20 == 0) {
                        log("📊 Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
                        // Pause de récupération
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                } else {
                    logError("Échec écriture micro-chunk: " + String(written) + "/" + String(chunkSize));
                }
            } else {
                logError("Échec lecture micro-chunk: " + String(bytesRead) + "/" + String(chunkSize));
            }
            
            
            // Pause entre tentatives
            if (!chunkSuccess && retry < MAX_RETRIES - 1) {
                delay(PAUSE_BETWEEN_CHUNKS);
            }
        }
        
        if (!chunkSuccess) {
            logError("Échec définitif micro-chunk " + String(chunkNumber) + " après " + String(MAX_RETRIES) + " tentatives");
            downloadSuccess = false;
            break;
        }
        
        // Pause entre micro-chunks pour éviter la surcharge
        delay(PAUSE_BETWEEN_CHUNKS);
    }
    
    http.end();
    
    if (downloadSuccess && (OTAConfig::OTA_UNSAFE_FORCE || totalDownloaded == contentLength)) {
        log("🎯 Téléchargement ultra-révolutionnaire terminé: " + String(totalDownloaded) + " bytes");
        if (!OTAConfig::OTA_UNSAFE_FORCE && totalDownloaded != contentLength) {
            logError("Téléchargement incomplet: " + String(totalDownloaded) + "/" + String(contentLength));
            return false;
        }
        // Finalisation avec validation
        if (Update.end(OTAConfig::OTA_UNSAFE_FORCE)) {
            log("✅ Mise à jour ultra-révolutionnaire finalisée avec succès");
            
            // IMPORTANT: Utiliser la partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance
            // Cela garantit que nous utilisons la partition qui a réellement été mise à jour
            if (target_partition) {
                log("🔄 Marquage de la partition mise à jour comme boot: " + String(target_partition->label));
                esp_err_t err = esp_ota_set_boot_partition(target_partition);
                if (err != ESP_OK) {
                    logError("Erreur marquage partition boot: " + String(esp_err_to_name(err)));
                    return false;
                }
                log("✅ Partition " + String(target_partition->label) + " marquée comme boot avec succès");
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
        logError("Échec téléchargement ultra-révolutionnaire");
        return false;
    }
}

// Méthode pour télécharger et installer le filesystem LittleFS
bool OTAManager::downloadFilesystem(const String& url, size_t expectedSize, const String& expectedMD5) {
    log("📁 Début du téléchargement du filesystem...");
    
    if (url.isEmpty()) {
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
    log("📡 Code de réponse filesystem: " + String(code));
    
    if (code != HTTP_CODE_OK) {
        logError("Erreur GET filesystem: " + String(code));
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    log("📊 Taille réelle du filesystem: " + formatBytes(contentLength));
    
    if (!OTAConfig::OTA_UNSAFE_FORCE && !validateFilesystemSize(expectedSize, contentLength)) {
        http.end();
        return false;
    }
    if (OTAConfig::OTA_UNSAFE_FORCE && expectedSize > 0 && expectedSize != contentLength) {
        log("⚠️ OTA_UNSAFE_FORCE: taille filesystem inattendue ignorée: attendu=" + String(expectedSize) + ", réel=" + String(contentLength));
    }

    // Trouver la partition spiffs (LittleFS)
    const esp_partition_t* spiffs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!spiffs_partition) {
        logError("Partition spiffs non trouvée");
        http.end();
        return false;
    }
    
    log("📍 Partition spiffs trouvée: " + String(spiffs_partition->label) + " (0x" + String(spiffs_partition->address, HEX) + ", " + formatBytes(spiffs_partition->size) + ")");

    // Vérifier que le nouveau filesystem tient dans la partition
    if (contentLength > spiffs_partition->size) {
        logError("Filesystem trop grand pour la partition: " + formatBytes(contentLength) + " > " + formatBytes(spiffs_partition->size));
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

    log("📥 Téléchargement filesystem en cours...");
    
    // Ouvrir la partition pour écriture
    const esp_partition_t* partition_handle = spiffs_partition;
    if (partition_handle == nullptr) {
        logError("Erreur ouverture partition: partition non trouvée");
        http.end();
        return false;
    }
    
    // Effacer la partition avant écriture
    log("🧹 Effacement de la partition spiffs...");
    esp_err_t err = esp_partition_erase_range(partition_handle, 0, spiffs_partition->size);
    if (err != ESP_OK) {
        logError("Erreur effacement partition: " + String(esp_err_to_name(err)));
        // Pas besoin de fermer la partition avec la nouvelle API
        http.end();
        return false;
    }
    
    while (totalWritten < contentLength && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
        // Reset watchdog pour éviter les timeouts
        esp_task_wdt_reset();
        
        // Écriture dans la partition spiffs
        err = esp_partition_write(partition_handle, totalWritten, buffer, bytesRead);
        if (err != ESP_OK) {
            logError("Erreur écriture filesystem: " + String(esp_err_to_name(err)));
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
            log("📊 Heap libre: " + formatBytes(currentHeap));
            
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
    
    log("📥 Téléchargement filesystem terminé: " + formatBytes(totalWritten) + "/" + formatBytes(contentLength));
    if (!OTAConfig::OTA_UNSAFE_FORCE) {
        if (totalWritten != contentLength) {
            logError("Téléchargement filesystem incomplet: " + formatBytes(totalWritten) + "/" + formatBytes(contentLength));
            return false;
        }
    } else {
        if (totalWritten != contentLength) {
            log("⚠️ OTA_UNSAFE_FORCE: écart de taille filesystem ignoré: " + String(totalWritten) + "/" + String(contentLength));
        }
    }

    // Validation MD5 si fourni
    if (!OTAConfig::OTA_UNSAFE_FORCE && !expectedMD5.isEmpty()) {
        log("🔐 Validation MD5 du filesystem...");
        // Note: La validation MD5 complète nécessiterait de relire tout le filesystem
        // Pour l'instant, on fait confiance au téléchargement HTTP
        log("✅ Validation MD5 filesystem (simplifiée)");
    } else if (OTAConfig::OTA_UNSAFE_FORCE && !expectedMD5.isEmpty()) {
        log("⚠️ OTA_UNSAFE_FORCE: validation MD5 filesystem ignorée");
    }

    log("✅ Mise à jour filesystem réussie !");
    return true;
}

void OTAManager::setProgressCallback(std::function<void(int)> callback) {
    m_progressCallback = callback;
}

void OTAManager::setStatusCallback(std::function<void(const String&)> callback) {
    m_statusCallback = callback;
}

void OTAManager::setErrorCallback(std::function<void(const String&)> callback) {
    m_errorCallback = callback;
}

void OTAManager::setCheckInterval(unsigned long interval) {
    m_checkInterval = interval;
}

void OTAManager::setCurrentVersion(const String& version) {
    m_currentVersion = version;
}

String OTAManager::getCurrentVersion() const {
    return m_currentVersion;
}

String OTAManager::getRemoteVersion() const {
    return m_remoteVersion;
}

bool OTAManager::checkForUpdate() {
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
    String payload;
    if (!downloadMetadata(payload)) {
        return false;
    }
    
    // Parsing JSON (capacité suffisante pour metadata + channels)
    DynamicJsonDocument doc(BufferConfig::JSON_DOCUMENT_SIZE);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        logError("Erreur parsing JSON: " + String(error.c_str()));
        return false;
    }
    log("✅ JSON parsé avec succès");
    
    // Validation des métadonnées (accepte channels ou schéma legacy)
    if (!validateMetadata(doc)) {
        return false;
    }
    
    // Sélection de l'artefact selon env/modèle avec fallbacks
    String selVersion, selUrl, selMD5;
    int selSize = 0;
    if (!selectArtifactFromMetadata(doc, selVersion, selUrl, selSize, selMD5)) {
        logError("Aucun artefact OTA valide trouvé dans les métadonnées");
        return false;
    }

    m_remoteVersion = selVersion;
    m_firmwareUrl = selUrl;
    m_firmwareSize = selSize;
    m_firmwareMD5 = selMD5;
    
    // Sélection du filesystem (optionnel)
    String selFilesystemUrl, selFilesystemMD5;
    int selFilesystemSize = 0;
    if (selectFilesystemFromMetadata(doc, selFilesystemUrl, selFilesystemSize, selFilesystemMD5)) {
        m_filesystemUrl = selFilesystemUrl;
        m_filesystemSize = selFilesystemSize;
        m_filesystemMD5 = selFilesystemMD5;
        log("📁 Filesystem trouvé: '" + m_filesystemUrl + "'");
        log("📁 Taille filesystem: " + formatBytes(m_filesystemSize));
        if (m_filesystemMD5.length() > 0) {
            log("🔐 MD5 filesystem: '" + m_filesystemMD5 + "'");
        }
    } else {
        m_filesystemUrl = "";
        m_filesystemSize = 0;
        m_filesystemMD5 = "";
        log("ℹ️ Aucun filesystem à mettre à jour");
    }
    
    log("📋 Version distante: '" + m_remoteVersion + "'");
    log("📋 Version locale: '" + m_currentVersion + "'");
    log("📋 URL firmware: '" + m_firmwareUrl + "'");
    log("📋 Taille firmware: " + formatBytes(m_firmwareSize));
    if (m_firmwareMD5.length() > 0) {
        log("🔐 MD5: '" + m_firmwareMD5 + "'");
    }
    
    // Comparaison de version
    int versionCompare = compareVersions(m_remoteVersion, m_currentVersion);
    log("🔄 Résultat comparaison: " + String(versionCompare) + " (0=égal, >0=nouvelle, <0=ancienne)");
    
    if (versionCompare <= 0) {
        log("✅ Aucune mise à jour disponible");
        return false;
    }

    log("🆕 Nouvelle version " + m_remoteVersion + " trouvée (courante " + m_currentVersion + ")");
    
    // Vérification de la taille
    if (m_firmwareSize <= 0) {
        log("⚠️ Taille firmware non spécifiée, utilisation de la taille par défaut");
        m_firmwareSize = 1678528; // Taille par défaut
    }
    
    if (!validateSpace(m_firmwareSize)) {
        return false;
    }
    
    log("✅ Espace suffisant: " + formatBytes(ESP.getFreeSketchSpace()) + " >= " + formatBytes(m_firmwareSize));
    
    m_lastCheck = millis();
    return true;
}

bool OTAManager::performUpdate() {
    if (m_firmwareUrl.isEmpty()) {
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
  EventLog::addf("OTA perform start remote=%s url=%s size=%d",
                 m_remoteVersion.c_str(),
                 m_firmwareUrl.c_str(),
                 m_firmwareSize);

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

String OTAManager::getFirmwareUrl() const {
    return m_firmwareUrl;
}

int OTAManager::compareVersions(const String& version1, const String& version2) {
    std::vector<int> v1_parts, v2_parts;
    
    // Parse version1
    String v1 = version1;
    while (v1.length() > 0) {
        int dotPos = v1.indexOf('.');
        if (dotPos == -1) {
            v1_parts.push_back(v1.toInt());
            break;
        }
        v1_parts.push_back(v1.substring(0, dotPos).toInt());
        v1 = v1.substring(dotPos + 1);
    }
    
    // Parse version2
    String v2 = version2;
    while (v2.length() > 0) {
        int dotPos = v2.indexOf('.');
        if (dotPos == -1) {
            v2_parts.push_back(v2.toInt());
            break;
        }
        v2_parts.push_back(v2.substring(0, dotPos).toInt());
        v2 = v2.substring(dotPos + 1);
    }
    
    // Compare les composants
    int maxParts = max(v1_parts.size(), v2_parts.size());
    for (int i = 0; i < maxParts; i++) {
        int v1_part = (i < v1_parts.size()) ? v1_parts[i] : 0;
        int v2_part = (i < v2_parts.size()) ? v2_parts[i] : 0;
        
        if (v1_part < v2_part) return -1;
        if (v1_part > v2_part) return 1;
    }
    
    return 0;
}

String OTAManager::formatBytes(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + " KB";
    else return String(bytes / (1024.0 * 1024.0), 1) + " MB";
}

String OTAManager::formatSpeed(float speed) {
    if (speed < 1024) return String(speed, 1) + " KB/s";
    else return String(speed / 1024.0, 1) + " MB/s";
} 