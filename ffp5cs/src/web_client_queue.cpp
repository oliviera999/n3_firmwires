// web_client_queue.cpp — file NVS de POST échoués (retry hors-ligne) de WebClient.
// queueFailedPost / processQueuedPosts / getQueuedPostsCount / clearQueuedPosts.
// Extrait de web_client.cpp (audit : découpe). Méthodes membres, comportement identique.
#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "gpio_mapping.h"  // SensorMap, GPIOMap (source unique noms POST)
#include "log.h"
#include "config.h"
#include "nvs_manager.h"
#include "nvs_keys.h"  // v11.178: Utilisation des clés NVS centralisées (audit)
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>
#include <atomic>

// Global défini dans app.cpp (idem web_client.cpp).
extern ConfigManager config;

bool WebClient::queueFailedPost(const char* payload) {
  if (payload == nullptr || strlen(payload) == 0) {
    return false;
  }
  
  size_t payloadLen = strlen(payload);
  if (payloadLen >= MAX_POST_SIZE) {
    LOG(LOG_WARN, "[HTTP] Payload trop long pour queue: %u bytes", payloadLen);
    return false;
  }
  
  // v11.178: Utilisation des clés NVS centralisées (audit nvs-keys)
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  // Queue pleine: supprimer le plus ancien (FIFO)
  if (count >= MAX_QUEUED_POSTS) {
    // Décaler tous les éléments
    for (int i = 0; i < count - 1; i++) {
      char srcKey[16], dstKey[16];
      snprintf(srcKey, sizeof(srcKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i + 1);
      snprintf(dstKey, sizeof(dstKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
      
      char tempPayload[MAX_POST_SIZE];
      g_nvsManager.loadString(NVS_NAMESPACES::STATE, srcKey, tempPayload, sizeof(tempPayload), "");
      if (strlen(tempPayload) > 0) {
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, dstKey, tempPayload);
      }
    }
    count = MAX_QUEUED_POSTS - 1;
  }
  
  // Ajouter le nouveau payload
  char key[16];
  snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, count);
  g_nvsManager.saveString(NVS_NAMESPACES::STATE, key, payload);
  count++;
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count);
  
  LOG(LOG_INFO, "[HTTP] POST mis en queue (total: %d)", count);
  return true;
}

bool WebClient::processQueuedPosts() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  if (count == 0) {
    return true;  // Rien à traiter
  }
  
  LOG(LOG_INFO, "[HTTP] Traitement queue: %d POSTs en attente", count);
  
  int processed = 0;
  int failed = 0;
  
  for (int i = 0; i < count && i < MAX_QUEUED_POSTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
    
    char payload[MAX_POST_SIZE];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, key, payload, sizeof(payload), "");
    
    if (strlen(payload) == 0) {
      continue;
    }
    
    char resp[1024];

    if (postDataHttpRequest(payload, resp, sizeof(resp))) {
      processed++;
      // Marquer comme traité (effacer)
      g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key);
    } else {
      failed++;
      // Arrêter si échec (réseau peut être parti)
      if (WiFi.status() != WL_CONNECTED) {
        LOG(LOG_WARN, "[HTTP] WiFi perdu, arrêt traitement queue");
        break;
      }
    }
    
    // Délai entre requêtes
    vTaskDelay(pdMS_TO_TICKS(500));
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
  }
  
  // Mettre à jour le compteur
  int remaining = count - processed;
  if (remaining < 0) remaining = 0;
  
  // Compacter la queue si nécessaire
  if (processed > 0 && remaining > 0) {
    int newIdx = 0;
    for (int i = 0; i < count; i++) {
      char srcKey[16];
      snprintf(srcKey, sizeof(srcKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
      
      char payload[MAX_POST_SIZE];
      g_nvsManager.loadString(NVS_NAMESPACES::STATE, srcKey, payload, sizeof(payload), "");
      
      if (strlen(payload) > 0) {
        if (newIdx != i) {
          char dstKey[16];
          snprintf(dstKey, sizeof(dstKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, newIdx);
          g_nvsManager.saveString(NVS_NAMESPACES::STATE, dstKey, payload);
          g_nvsManager.removeKey(NVS_NAMESPACES::STATE, srcKey);
        }
        newIdx++;
      }
    }
    remaining = newIdx;
  }
  
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, remaining);
  
  LOG(LOG_INFO, "[HTTP] Queue traitée: %d envoyés, %d échoués, %d restants", 
      processed, failed, remaining);
  
  return (failed == 0);
}

uint8_t WebClient::getQueuedPostsCount() {
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  return (uint8_t)count;
}

void WebClient::clearQueuedPosts() {
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  for (int i = 0; i < count && i < MAX_QUEUED_POSTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key);
  }
  
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, 0);
  LOG(LOG_INFO, "[HTTP] Queue vidée (%d entrées supprimées)", count);
}
