// ota_manager_download.cpp — Méthodes de téléchargement/flash OTAManager
// (metadata, firmware modern/fallback/ultra, filesystem, updateTask). Extrait de
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
        if (code == -1) {
            log("   (connexion ou timeout: serveur injoignable, DNS, ou délai dépassé)");
        }
        http.end();
        return false;
    }

    // Lecture directe dans le buffer appelant (évite double buffer ~4 Ko sur stack otaTask)
    if (payloadSize < 2) {
        http.end();
        logError("Buffer metadata trop petit");
        return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    size_t payloadLen = 0;
    if (stream) {
      // v11.178: Ajout timeout pour éviter blocage infini (audit bugs-high)
      unsigned long streamStart = millis();
      const unsigned long STREAM_TIMEOUT_MS = 5000;
      while (stream->available() && payloadLen < payloadSize - 1
             && (millis() - streamStart) < STREAM_TIMEOUT_MS) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();
        }
        size_t bytesRead = stream->readBytes(payload + payloadLen, payloadSize - payloadLen - 1);
        payloadLen += bytesRead;
      }
      payload[payloadLen] = '\0';
      // Détection troncation : buffer plein et données restantes
      if (payloadLen >= payloadSize - 1 && stream->available() > 0) {
          log("⚠️ Payload metadata tronqué (buffer sortie insuffisant), JSON peut être invalide");
      }
    } else {
      // v11.180: Suppression getString() - cause crashes LoadProhibited dans destructeur String
      log("⚠️ Pas de stream HTTP disponible");
      payload[0] = '\0';
      payloadLen = 0;
    }
    http.end();

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
    // v13.60 (audit sécurité): exiger la validation du CN du certificat HTTPS pour metadata OTA.
    // L'ancien `skip_cert_common_name_check = true` permettait un MITM si DNS détourné vers
    // un serveur dont le certificat appartient à un autre domaine. iot.olution.info est servi
    // par un certificat valide pour ce domaine, donc le check doit passer normalement.
    config.skip_cert_common_name_check = false;
    // Bundle TLS : S3 et Arduino-ESP32 2.x → arduino_esp_crt_bundle_attach ; WROOM Arduino 3.x (pioarduino) → esp_crt_bundle_attach.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
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
    // MD5: toujours défini quand disponible (même avec OTA_UNSAFE_FORCE) pour détecter binaires tronqués/corrompus
    if (strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
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
        const esp_partition_t* bootNow = esp_ota_get_boot_partition();
        if (bootNow) {
            char logPart[128];
            snprintf(logPart, sizeof(logPart), "[OTA] Partition écrite: %s (0x%x), boot après marquage: %s (0x%x)",
                target_partition->label, (unsigned)target_partition->address, bootNow->label, (unsigned)bootNow->address);
            log(logPart);
        }
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
    if (strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
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
        const esp_partition_t* bootNow2 = esp_ota_get_boot_partition();
        if (bootNow2) {
            char logPart2[128];
            snprintf(logPart2, sizeof(logPart2), "[OTA] Partition écrite: %s (0x%x), boot après marquage: %s (0x%x)",
                target_partition->label, (unsigned)target_partition->address, bootNow2->label, (unsigned)bootNow2->address);
            log(logPart2);
        }
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
             "OTA distant démarré\n\nAncienne version: %s\nNouvelle version: %s\nEnvironnement: %s\nTaille firmware: %s",
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
        ota->m_otaLock = false;
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


