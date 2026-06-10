// ota_manager_download_alt.cpp — chemins de téléchargement secondaires d'OTAManager :
//   • downloadFirmwareUltraRevolutionary : fallback firmware en micro-chunks (2KB + retry/chunk)
//   • downloadFilesystem                 : téléchargement + flash de l'image LittleFS
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
    if (strlen(m_firmwareMD5) > 0) {
        Update.setMD5(m_firmwareMD5);
        log("🔐 MD5 défini pour vérification");
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
                const esp_partition_t* bootNow3 = esp_ota_get_boot_partition();
                if (bootNow3) {
                    char logPart3[128];
                    snprintf(logPart3, sizeof(logPart3), "[OTA] Partition écrite: %s (0x%x), boot après marquage: %s (0x%x)",
                        target_partition->label, (unsigned)target_partition->address, bootNow3->label, (unsigned)bootNow3->address);
                    log(logPart3);
                }
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
