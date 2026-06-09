// web_routes_wifi.cpp — Endpoints backend de gestion WiFi.
// Extrait de web_server.cpp (audit optimisation v13.93) pour alléger le god-file.
// Routes : /wifi/scan, /wifi/saved, /wifi/connect, /wifi/remove. Comportement
// identique ; helpers d'auth/réponse partagés via web_routes_status.h.
#include "web_routes_wifi.h"
#include "web_routes_status.h"  // webAuth*, sendJsonResponse, sendErrorResponse, ensureHeapForRoute, getWebParam

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <nvs.h>                 // API C NVS (nvs_open/get_blob/set_blob/commit/close)
#include <nvs_flash.h>
#include "wifi_manager.h"        // WiFiHelpers
#include "esp_wifi.h"            // esp_wifi_scan_get_ap_records
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdlib>
#include <time.h>

#include "automatism.h"
#include "realtime_websocket.h"  // g_realtimeWebSocket
#include "config.h"
#include "config_manager.h"      // ConfigManager (global config)
#include "power.h"               // PowerManager (global power)
#include "app_context.h"

extern Automatism g_autoCtrl;
extern ConfigManager config;
extern PowerManager power;
extern WifiManager wifi;

// Helpers déplacés depuis web_server.cpp (usage exclusif WiFi).
static bool canCreateAsyncTask() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= HeapConfig::MIN_HEAP_BLOCK_FOR_ASYNC_TASK;
}
// Rate limit WiFi connect (évite rafales).
static unsigned long s_lastWifiConnectAt = 0;

namespace WebRoutes {

void registerWifiRoutes(AsyncWebServer& server, AppContext& ctx) {
  (void)ctx;  // routes WiFi via globaux (g_autoCtrl, g_realtimeWebSocket), pas de ctx
  // Scanner les réseaux WiFi disponibles
  // NOTE: WiFi.scanNetworks() est intrinsèquement bloquant (2-5s) sur ESP32
  server.on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Action WiFi critique
    g_autoCtrl.notifyLocalWebActivity();
    
    // v11.169: Vérification mémoire (audit robustesse)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_WIFI_ROUTE, F("/wifi/scan"))) {
      return;
    }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Vérifier que le WiFi est dans un mode permettant le scan
    wifi_mode_t wifiMode = WiFi.getMode();
    if (wifiMode == WIFI_OFF) {
      doc["success"] = false;
      doc["count"] = 0;
      doc["error"] = "WiFi is off - cannot scan";
      if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/wifi/scan"))) { return; }
      AsyncResponseStream* response = req->beginResponseStream("application/json");
      if (response) {
        // v13.60 (audit sécurité): retiré CORS * - même origine que l'UI.
        serializeJson(doc, *response);
        req->send(response);
      } else {
        req->send(NetworkConfig::HTTP_SERVICE_UNAVAILABLE, "text/plain", "Memory error");
      }
      return;
    }
    
    // Scanner les réseaux WiFi (opération bloquante ~2-5s)
    // v11.176: Watchdog reset avant scan bloquant - audit robustesse
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    // v11.176: Watchdog reset après scan bloquant
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    doc["success"] = (n >= 0);
    doc["count"] = n;
    
    // Lecture des résultats via ESP-IDF (évite String Arduino → stabilité long uptime)
    static wifi_ap_record_t s_apRecords[NetworkConfig::WIFI_SCAN_MAX_RECORDS];
    uint16_t num = (n > 0 && n <= NetworkConfig::WIFI_SCAN_MAX_RECORDS) ? (uint16_t)n : (n > 0 ? NetworkConfig::WIFI_SCAN_MAX_RECORDS : 0);
    
    if (num > 0 && esp_wifi_scan_get_ap_records(&num, s_apRecords) == ESP_OK) {
      JsonArray networks = doc.createNestedArray("networks");
      for (int i = 0; i < num; i++) {
        char ssidBuf[33];
        memcpy(ssidBuf, s_apRecords[i].ssid, 32);
        ssidBuf[32] = '\0';
        size_t len = strnlen(ssidBuf, 32);
        ssidBuf[len] = '\0';
        
        JsonObject network = networks.createNestedObject();
        network["rssi"] = s_apRecords[i].rssi;
        network["encryption"] = (s_apRecords[i].authmode == WIFI_AUTH_OPEN) ? "open" : "secured";
        network["channel"] = s_apRecords[i].primary;
        
        if (ssidBuf[0] == '\0') {
          network["ssid"] = "<Hidden Network>";
          network["hidden"] = true;
        } else {
          network["ssid"] = ssidBuf;
          network["hidden"] = false;
        }
      }
    } else if (n > 0) {
      doc["error"] = "Failed to get scan records";
    } else {
      doc["error"] = "No networks found or scan failed";
    }
    
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/wifi/scan"))) { return; }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    // v11.169: Vérification nullptr (audit robustesse)
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    // v13.60 (audit sécurité): retiré CORS * - même origine que l'UI (/wifi/scan).
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Lister les réseaux WiFi sauvegardés
  server.on("/wifi/saved", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // v11.40: Pas de notifyLocalWebActivity() - endpoint lecture seule

    // v11.169: Vérification mémoire (audit robustesse)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_WIFI_ROUTE, F("/wifi/saved"))) {
      return;
    }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    JsonArray networks = doc.createNestedArray("networks");
    size_t totalCount = 0;
    
    // 1. Ajouter les réseaux statiques de secrets.h
    // v13.52 (audit sécurité): ne plus exposer les mots de passe en clair via l'API web.
    // Indique seulement la présence d'un mot de passe (booléen) ; pour récupérer le mdp,
    // passer par le code source (`include/secrets.h`) ou le re-saisir dans l'UI.
    for (size_t i = 0; i < Secrets::WIFI_COUNT; i++) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = Secrets::WIFI_LIST[i].ssid;
      network["hasPassword"] = (Secrets::WIFI_LIST[i].password != nullptr &&
                                Secrets::WIFI_LIST[i].password[0] != '\0');
      network["index"] = totalCount;
      network["source"] = "static"; // Marquer comme réseau statique
      totalCount++;
    }
    
    // 2. Ajouter les réseaux sauvegardés en NVS
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("wifi_saved", NVS_READONLY, &nvsHandle);
    
    if (err == ESP_OK) {
      // Lire le nombre de réseaux sauvegardés
      size_t networkCount = 0;
      size_t required_size = sizeof(networkCount);
      nvs_get_blob(nvsHandle, "count", &networkCount, &required_size);
      
      if (networkCount > 0) {
        // Lire chaque réseau sauvegardé
        for (size_t i = 0; i < networkCount; i++) {
          char key[16];
          snprintf(key, sizeof(key), "net_%zu", i);
          
          size_t required_size = 0;
          err = nvs_get_blob(nvsHandle, key, nullptr, &required_size);
          if (err == ESP_OK && required_size > 0) {
            if (required_size > NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
              Serial.printf("[WiFi] ⚠️ Entrée NVS '%s' ignorée (%u bytes > max %u)\n",
                            key,
                            static_cast<unsigned>(required_size),
                            static_cast<unsigned>(NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES));
              continue;
            }
            static char wifiListBlobBuf[NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES];
            size_t bufLen = sizeof(wifiListBlobBuf);
            err = nvs_get_blob(nvsHandle, key, wifiListBlobBuf, &bufLen);
            if (err == ESP_OK) {
              JsonObject network = networks.createNestedObject();
              char* separator = strchr(wifiListBlobBuf, '|');
              if (separator != nullptr && separator > wifiListBlobBuf) {
                *separator = '\0';
                char* ssid = wifiListBlobBuf;
                char* password = separator + 1;
                bool existsInStatic = false;
                for (size_t j = 0; j < Secrets::WIFI_COUNT; j++) {
                  if (strcmp(ssid, Secrets::WIFI_LIST[j].ssid) == 0) {
                    existsInStatic = true;
                    break;
                  }
                }
                if (!existsInStatic) {
                  // v13.52 (audit sécurité): ne plus exposer le mot de passe NVS en clair.
                  network["ssid"] = ssid;
                  network["hasPassword"] = (password != nullptr && password[0] != '\0');
                  network["index"] = totalCount;
                  network["source"] = "saved";
                  totalCount++;
                }
              }
            }
          }
        }
      }
      
      nvs_close(nvsHandle);
    }
    
    doc["success"] = true;
    doc["count"] = totalCount;
    
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/wifi/saved"))) { return; }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    // v11.169: Vérification nullptr (audit robustesse)
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    // v13.60 (audit sécurité): retiré CORS * - même origine que l'UI (/wifi/saved).
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Connecter à un réseau WiFi
  server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Changement WiFi critique
    g_autoCtrl.notifyLocalWebActivity();
    
    // v11.169: Vérification mémoire (audit robustesse)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_WIFI_ROUTE, F("/wifi/connect"))) {
      return;
    }
    
    char ssidBuf[64], passwordBuf[65], saveBuf[8];
    bool hasSsid = getWebParam(req, "ssid", ssidBuf, sizeof(ssidBuf));
    bool hasPassword = getWebParam(req, "password", passwordBuf, sizeof(passwordBuf));
    bool hasSave = getWebParam(req, "save", saveBuf, sizeof(saveBuf));
    
    Serial.printf("[WiFi] Demande de connexion à '%s'\n", ssidBuf);
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    if (!hasSsid || strlen(ssidBuf) == 0) {
      doc["success"] = false;
      doc["error"] = "SSID required";
      Serial.println("[WiFi] Erreur: SSID vide");
    } else {
      // Sauvegarder le réseau AVANT de se déconnecter pour éviter les pertes de connexion
      if (hasSave && strcmp(saveBuf, "true") == 0 && hasPassword && strlen(passwordBuf) > 0) {
        Serial.println("[WiFi] Sauvegarde du réseau en NVS");
        nvs_handle_t nvsHandle;
        esp_err_t err = nvs_open("wifi_saved", NVS_READWRITE, &nvsHandle);
        
        if (err == ESP_OK) {
          // Lire le nombre actuel de réseaux
          size_t networkCount = 0;
          size_t required_size = sizeof(networkCount);
          nvs_get_blob(nvsHandle, "count", &networkCount, &required_size);
          if (networkCount > NVSConfig::MAX_WIFI_SAVED_NETWORKS) {
            Serial.printf("[WiFi] ⚠️ Compteur NVS wifi_saved.count trop élevé (%u > %u) - clamp à max\n",
                          static_cast<unsigned>(networkCount),
                          static_cast<unsigned>(NVSConfig::MAX_WIFI_SAVED_NETWORKS));
            networkCount = NVSConfig::MAX_WIFI_SAVED_NETWORKS;
          }
          
          // Vérifier si le réseau existe déjà
          bool exists = false;
          static char wifiConnectCheckBuf[NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES];
          for (size_t i = 0; i < networkCount; i++) {
            char key[16];
            snprintf(key, sizeof(key), "net_%zu", i);
            size_t data_size = 0;
            err = nvs_get_blob(nvsHandle, key, nullptr, &data_size);
            if (err == ESP_OK && data_size > 0 && data_size <= NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
              size_t bufLen = sizeof(wifiConnectCheckBuf);
              err = nvs_get_blob(nvsHandle, key, wifiConnectCheckBuf, &bufLen);
              if (err == ESP_OK) {
                char* separator = strchr(wifiConnectCheckBuf, '|');
                if (separator != nullptr && separator > wifiConnectCheckBuf) {
                  *separator = '\0';
                  if (strcmp(wifiConnectCheckBuf, ssidBuf) == 0) {
                    exists = true;
                    char newData[130];
                    snprintf(newData, sizeof(newData), "%s|%s", ssidBuf, passwordBuf);
                    nvs_set_blob(nvsHandle, key, newData, strlen(newData) + 1);
                    nvs_commit(nvsHandle);
                    Serial.printf("[WiFi] Réseau '%s' mis à jour dans NVS\n", ssidBuf);
                  }
                }
              }
            }
          }
          
          // Ajouter le nouveau réseau s'il n'existe pas
          if (!exists) {
            char key[16];
            snprintf(key, sizeof(key), "net_%zu", networkCount);
            char data[130];
            snprintf(data, sizeof(data), "%s|%s", ssidBuf, passwordBuf);
            
            err = nvs_set_blob(nvsHandle, key, data, strlen(data) + 1);
            if (err == ESP_OK) {
              networkCount++;
              nvs_set_blob(nvsHandle, "count", &networkCount, sizeof(networkCount));
              nvs_commit(nvsHandle);
              Serial.printf("[WiFi] Réseau '%s' ajouté dans NVS (total: %zu)\n",
                            ssidBuf, networkCount);
            }
          }
          
          nvs_close(nvsHandle);
        }
      }
      
      // Rate limit: éviter tentatives WiFi trop rapprochées (fragmentation / boucles)
      unsigned long nowWifi = millis();
      if (nowWifi - s_lastWifiConnectAt < AsyncTaskConfig::WIFI_CONNECT_MIN_MS) {
        doc["success"] = false;
        doc["message"] = "Retry in a few seconds";
        char jsonRate[256];
        serializeJson(doc, jsonRate, sizeof(jsonRate));
        req->send(200, "application/json", jsonRate);
        return;
      }
      // Garde fragmentation: ne pas lancer la tâche WiFi si heap trop fragmenté (évite aggravation)
      if (!canCreateAsyncTask()) {
        doc["success"] = false;
        doc["message"] = "Memory low, retry later";
        Serial.printf("[Web] WiFi connect skipped (heap blk=%u)\n",
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        char jsonLow[256];
        serializeJson(doc, jsonLow, sizeof(jsonLow));
        req->send(200, "application/json", jsonLow);
        return;
      }
      s_lastWifiConnectAt = nowWifi;

      // Notifier les clients WebSocket du changement imminent
      g_realtimeWebSocket.notifyWifiChange(ssidBuf);
      vTaskDelay(pdMS_TO_TICKS(200));
      g_realtimeWebSocket.closeAllConnections();
      vTaskDelay(pdMS_TO_TICKS(500));
      Serial.printf("[WiFi] Déconnexion du réseau actuel\n");
      WiFi.disconnect(false, true);
      vTaskDelay(pdMS_TO_TICKS(200));

      // DÉROGATION: blocage webTask jusqu'à WIFI_CONNECT_ATTEMPT_TIMEOUT_MS (15s WROOM) pour connexion WiFi
      esp_task_wdt_reset();
      bool connected = wifi.connectTo(ssidBuf, passwordBuf);
      if (connected) {
        power.saveCurrentWifiCredentials();
        IPAddress ip = WiFi.localIP();
        Serial.printf(
          "[WiFi] Connecté avec succès à '%s' (IP: %d.%d.%d.%d, RSSI: %d dBm)\n",
          ssidBuf, ip[0], ip[1], ip[2], ip[3], WiFi.RSSI());
        g_realtimeWebSocket.broadcastNow();
      } else {
        Serial.printf("[WiFi] Échec de connexion à '%s' (timeout)\n", ssidBuf);
      }

      doc["success"] = connected;
      doc["message"] = connected ? "Connected" : "Connection failed";
      doc["ssid"] = ssidBuf;
      char jsonSync[512];
      serializeJson(doc, jsonSync, sizeof(jsonSync));
      req->send(200, "application/json", jsonSync);
      return;
    }
    
    // Si on arrive ici, c'est qu'il y a eu une erreur
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/wifi/connect"))) { return; }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    // v11.169: Vérification nullptr (audit robustesse)
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    // v13.60 (audit sécurité): retiré CORS * - même origine que l'UI (/wifi/connect).
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Supprimer un réseau WiFi sauvegardé
  server.on("/wifi/remove", HTTP_POST, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Modification WiFi
    g_autoCtrl.notifyLocalWebActivity();
    
    // v11.169: Vérification mémoire (audit robustesse)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_WIFI_ROUTE, F("/wifi/remove"))) {
      return;
    }
    
    char ssidBuf[64];
    bool hasSsid = false;
    if (req->hasParam("ssid", true)) {
      const AsyncWebParameter* p = req->getParam("ssid", true);
      if (p) {
        Utils::safeStrncpy(ssidBuf, p->value().c_str(), sizeof(ssidBuf));
        hasSsid = (ssidBuf[0] != '\0');
      }
    } else {
      ssidBuf[0] = '\0';
    }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    if (!hasSsid || strlen(ssidBuf) == 0) {
      doc["success"] = false;
      doc["error"] = "SSID required";
    } else {
      nvs_handle_t nvsHandle;
      esp_err_t err = nvs_open("wifi_saved", NVS_READWRITE, &nvsHandle);
      
      if (err == ESP_OK) {
        // Lire le nombre actuel de réseaux
        size_t networkCount = 0;
        size_t required_size = sizeof(networkCount);
        nvs_get_blob(nvsHandle, "count", &networkCount, &required_size);
        
        bool found = false;
        size_t foundIndex = 0;
        
        // Trouver l'index du réseau à supprimer
        static char wifiDeleteBuf[NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES];
        for (size_t i = 0; i < networkCount; i++) {
          char key[16];
          snprintf(key, sizeof(key), "net_%zu", i);
          size_t data_size = 0;
          err = nvs_get_blob(nvsHandle, key, nullptr, &data_size);
          if (err == ESP_OK && data_size > 0 && data_size <= NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
            size_t bufLen = sizeof(wifiDeleteBuf);
            err = nvs_get_blob(nvsHandle, key, wifiDeleteBuf, &bufLen);
            if (err == ESP_OK) {
              char* separator = strchr(wifiDeleteBuf, '|');
              if (separator != nullptr && separator > wifiDeleteBuf) {
                *separator = '\0';
                if (strcmp(wifiDeleteBuf, ssidBuf) == 0) {
                  found = true;
                  foundIndex = i;
                  break;
                }
              }
            }
          } else if (err == ESP_OK && data_size > NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
            Serial.printf("[WiFi] ⚠️ Entrée NVS '%s' ignorée pour suppression (%u bytes > max %u)\n",
                          key,
                          static_cast<unsigned>(data_size),
                          static_cast<unsigned>(NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES));
          }
        }
        
        if (found) {
          // Supprimer le réseau trouvé
          char key[16];
          snprintf(key, sizeof(key), "net_%zu", foundIndex);
          nvs_erase_key(nvsHandle, key);
          
          // Décaler les réseaux suivants
          for (size_t i = foundIndex + 1; i < networkCount; i++) {
            char oldKey[16], newKey[16];
            snprintf(oldKey, sizeof(oldKey), "net_%zu", i);
            snprintf(newKey, sizeof(newKey), "net_%zu", i - 1);
            size_t data_size = 0;
            err = nvs_get_blob(nvsHandle, oldKey, nullptr, &data_size);
            if (err == ESP_OK && data_size > 0 && data_size <= NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
              size_t bufLen = sizeof(wifiDeleteBuf);
              err = nvs_get_blob(nvsHandle, oldKey, wifiDeleteBuf, &bufLen);
              if (err == ESP_OK) {
                nvs_set_blob(nvsHandle, newKey, wifiDeleteBuf, bufLen);
                nvs_erase_key(nvsHandle, oldKey);
              }
            } else if (err == ESP_OK && data_size > NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES) {
              Serial.printf("[WiFi] ⚠️ Entrée NVS '%s' ignorée lors du compactage (%u bytes > max %u)\n",
                            oldKey,
                            static_cast<unsigned>(data_size),
                            static_cast<unsigned>(NVSConfig::MAX_WIFI_SAVED_ENTRY_BYTES));
              nvs_erase_key(nvsHandle, oldKey);
            }
          }
          
          // Mettre à jour le compteur
          networkCount--;
          nvs_set_blob(nvsHandle, "count", &networkCount, sizeof(networkCount));
          nvs_commit(nvsHandle);
          
          doc["success"] = true;
          doc["message"] = "Network removed successfully";
        } else {
          doc["success"] = false;
          doc["error"] = "Network not found";
        }
        
        nvs_close(nvsHandle);
      } else {
        doc["success"] = false;
        doc["error"] = "Failed to open NVS";
      }
    }
    
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/wifi/remove"))) { return; }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    // v11.169: Vérification nullptr (audit robustesse)
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    // v13.60 (audit sécurité): retiré CORS * - même origine que l'UI (/wifi/remove).
    serializeJson(doc, *response);
    req->send(response);
  });
}

}  // namespace WebRoutes
