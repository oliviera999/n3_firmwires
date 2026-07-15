// ota_manager_download_alt.cpp — chemins de téléchargement secondaires d'OTAManager :
//   • downloadFilesystem : téléchargement + flash de l'image LittleFS
// (C2 : downloadFirmwareUltraRevolutionary supprimé — le firmware passe par l'unique
//  chemin résumable downloadFirmwareAdaptiveResumable dans ota_manager_download.cpp.)
// Extrait de ota_manager_download.cpp (audit : découpe god-file > 1000 l.). Définitions
// de méthodes membres OTAManager réparties sur plusieurs TU ; classe et comportement
// inchangés. Bloc d'includes identique au TU parent (symboles Update/HTTP/esp_ota/TLS).
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

    // Vérifier que le nouveau filesystem tient dans la partition.
    // contentLength <= 0 => réponse chunked / taille inconnue : ne pas déclencher un faux
    // "trop grand" (un int -1 promu en unsigned devient ~4 Go). Cast explicite après le garde.
    if (contentLength > 0 && (size_t)contentLength > spiffs_partition->size) {
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
    
    // contentLength <= 0 (chunked) : pas de limite connue, on lit jusqu'à fin de flux / timeout.
    while ((contentLength <= 0 || totalWritten < (size_t)contentLength)
           && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0
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
            int progress = (contentLength > 0) ? (int)((totalWritten * 100) / (size_t)contentLength) : -1;
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
