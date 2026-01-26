#include "web_server.h"
#include "diagnostics.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "mailer.h"
#include "automatism.h"
#include "power.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#endif
#include "web_routes_status.h"
#include "web_routes_ui.h"
#include "web_server_context.h"
#include "realtime_websocket.h"
#include "memory_diagnostics.h"  // Diagnostic fragmentation mémoire
#include "sensor_cache.h"
#include "asset_bundler.h"

#include "automatism/automatism_persistence.h"  // Pour pending sync (v11.32)
 
extern Automatism g_autoCtrl;
extern Mailer mailer;
extern ConfigManager config;
extern PowerManager power;
extern WifiManager wifi;

static WebServerContext* g_webServerContext = nullptr;
static portMUX_TYPE g_asyncTaskMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_asyncTaskCount = 0;

static bool tryAcquireAsyncSlot(uint8_t maxSlots) {
  bool ok = false;
  portENTER_CRITICAL(&g_asyncTaskMux);
  if (g_asyncTaskCount < maxSlots) {
    g_asyncTaskCount++;
    ok = true;
  }
  portEXIT_CRITICAL(&g_asyncTaskMux);
  return ok;
}

static void releaseAsyncSlot() {
  portENTER_CRITICAL(&g_asyncTaskMux);
  if (g_asyncTaskCount > 0) {
    g_asyncTaskCount--;
  }
  portEXIT_CRITICAL(&g_asyncTaskMux);
}

WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts)
    : _sensors(sensors), _acts(acts), _diag(nullptr), _ctx(nullptr) {
  initializeServer();
}

WebServerManager::WebServerManager(SystemSensors& sensors, 
                                   SystemActuators& acts, Diagnostics& diag)
    : _sensors(sensors), _acts(acts), _diag(&diag), _ctx(nullptr) {
  initializeServer();
}

void WebServerManager::initializeServer() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  
  // Snapshot après création AsyncWebServer
  MEM_DIAG_SNAPSHOT("after_asyncwebserver_created");
  #endif
}

WebServerManager::~WebServerManager() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  if (_server) {
    delete _server;
    _server = nullptr;
  }
  #endif
  if (_ctx) {
    delete _ctx;
    _ctx = nullptr;
  }
  g_webServerContext = nullptr;
}

// Structure pour passer les paramètres à la tâche de synchronisation des relais
struct RelaySyncTaskParams {
    SystemSensors* sensors;
    const char* relayName;
};

struct WifiConnectTaskParams {
  char ssid[33];
  char password[65];
};

const char* WebServerManager::handleRelayAction(
    const char* relayName,
    std::function<bool()> isRunning,
    std::function<void()> start,
    std::function<void()> stop,
    const char* onResponse,
    const char* offResponse
) {
    bool newState;
    const char* response;
    if (isRunning()) {
        Serial.printf("[Web] 💧 Stopping %s...\\n", relayName);
        stop();
        newState = false;
        response = offResponse;
        Serial.printf("[Web] ✅ %s stopped\\n", relayName);
    } else {
        Serial.printf("[Web] 💧 Starting %s...\\n", relayName);
        start();
        newState = true;
        response = onResponse;
        Serial.printf("[Web] ✅ %s started\\n", relayName);
    }

    // Sauvegarde NVS + pending sync
    AutomatismPersistence::saveCurrentActuatorState(relayName, newState);
    AutomatismPersistence::markPendingSync(relayName, newState);

    // Feedback WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();

    // Créer les paramètres pour la tâche
    RelaySyncTaskParams* params = new RelaySyncTaskParams{&_sensors, relayName};

    // Sync serveur en tâche asynchrone
    BaseType_t created = xTaskCreate([](void* param) {
        RelaySyncTaskParams* p = (RelaySyncTaskParams*)param;
        vTaskDelay(pdMS_TO_TICKS(100));
        SensorReadings readings = p->sensors->read();
        bool syncSuccess = g_autoCtrl.sendFullUpdate(readings);
        
        if (syncSuccess) {
            AutomatismPersistence::clearPendingSync(p->relayName);
            Serial.printf("[Web] ✅ %s synced (async)\\n", p->relayName);
        } else {
            Serial.printf("[Web] ⏳ %s sync pending\\n", p->relayName);
        }

        delete p; // Libérer la mémoire des paramètres
        vTaskDelete(NULL);
    }, "relay_sync", 4096, params, 1, nullptr);
    if (created != pdPASS) {
        Serial.println("[Web] ❌ Échec création tâche relay_sync");
        delete params;
    }

    return response;
}

bool WebServerManager::begin() {
  #ifdef DISABLE_ASYNC_WEBSERVER
  // Mode minimal sans serveur web
  Serial.println("[WebServer] Mode minimal - serveur web désactivé");
  return true;
  #else
  
  // Initialiser le serveur WebSocket temps réel
  g_realtimeWebSocket.begin(_sensors, _acts);
  
  // Snapshot après initialisation WebSocket
  HeapSnapshot snapshot_after_websocket = MEM_DIAG_SNAPSHOT("after_websocket");
  
  // Configurer les routes de bundles d'assets
  AssetBundler::setupBundleRoutes(_server);

  if (_ctx) {
    delete _ctx;
  }
  _ctx = new WebServerContext(g_autoCtrl,
                              mailer,
                              config,
                              power,
                              wifi,
                              _sensors,
                              _acts,
                              g_realtimeWebSocket);
  g_webServerContext = _ctx;
  WebServerContext& ctx = *_ctx;
  
  WebRoutes::registerUiRoutes(*_server, ctx);
  WebRoutes::registerStatusRoutes(*_server, ctx);

  // /action endpoint for remote controls - OPTIMISÉ POUR RÉACTIVITÉ
  _server->on("/action", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req){
      g_autoCtrl.notifyLocalWebActivity();
      const char* resp = "OK";
      
      IPAddress remoteIP = req->client()->remoteIP();
      char remoteIPBuf[16];
      snprintf(remoteIPBuf, sizeof(remoteIPBuf), "%d.%d.%d.%d",
               remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
      Serial.printf("[Web] 🎮 Action request from %s\n", remoteIPBuf);
      
      // Traitement des commandes de nourrissage (PRIORITÉ ABSOLUE)
      if (req->hasParam("cmd")) {
          char cmdBuf[64];
          strncpy(cmdBuf, req->getParam("cmd")->value().c_str(), sizeof(cmdBuf) - 1);
          cmdBuf[sizeof(cmdBuf) - 1] = '\0';
          const char* c = cmdBuf;
          Serial.printf("[Web] 🎯 Command: %s\n", c);
          
          if (strcmp(c, "feedSmall") == 0) {
              Serial.println("[Web] 🐟 Starting manual feed small...");
              // 1. EXÉCUTION IMMÉDIATE de l'action physique
              g_autoCtrl.manualFeedSmall();
              
              // 2. Marquer l'événement de nourrissage pour le graphique serveur
              g_autoCtrl.setBouffePetitsFlag("1");
              
              // 3. Feedback WebSocket IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              
              // 4. Réponse HTTP IMMÉDIATE (avant email/sync)
              resp="FEED_SMALL OK";
              
              // 5. Email + Sync en tâche asynchrone (v11.81: Version améliorée)
              auto* sensorsPtr = &_sensors;
              const uint8_t MAX_ASYNC_TASKS = 5; // Augmenté pour plus de robustesse
              
              if (tryAcquireAsyncSlot(MAX_ASYNC_TASKS)) {
                BaseType_t created = xTaskCreate([](void* param) {
                  SystemSensors* sensors = (SystemSensors*)param;
                  vTaskDelay(pdMS_TO_TICKS(100));
                  
                  // Utilisation de notre helper robuste d'envoi email
                  if (g_webServerContext) {
                    g_webServerContext->sendManualActionEmail(
                      "Bouffe manuelle - Petits poissons", 
                      "Bouffe manuelle", 
                      "NOURRISSAGE_PETITS");
                  }
                  
                  // Synchronisation serveur : envoie bouffePetits=1 (marqué ci-dessus)
                  SensorReadings readings = sensors->read();
                  bool syncSuccess = g_autoCtrl.sendFullUpdate(readings, nullptr);
                  Serial.printf("[Web] 📤 Server sync bouffePetits=1 %s\n",
                                syncSuccess ? "completed" : "pending");
                  
                  // Remettre le flag à 0 pour les prochains envois
                  g_autoCtrl.setBouffePetitsFlag("0");
                  
                  releaseAsyncSlot();
                  vTaskDelete(NULL);
                }, "feed_small_sync", 4096, sensorsPtr, 1, nullptr);
                if (created != pdPASS) {
                  releaseAsyncSlot();
                  Serial.println("[Web] ❌ Échec création tâche feed_small_sync");
                  g_autoCtrl.setBouffePetitsFlag("0"); // Reset en cas d'échec
                }
              } else {
                Serial.println(
                  "[Web] ⚠️ Limite de tâches async atteinte - tentative email immédiate");
                // Fallback: tentative d'envoi email immédiat si mémoire suffisante
                if (g_webServerContext && 
                    ESP.getFreeHeap() > g_webServerContext->emailMinHeapBytes) {
                  g_webServerContext->sendManualActionEmail(
                    "Bouffe manuelle - Petits poissons", 
                    "Bouffe manuelle", 
                    "NOURRISSAGE_FALLBACK");
                }
                g_autoCtrl.setBouffePetitsFlag("0"); // Reset
              }
              
              Serial.println("[Web] ✅ Small feed completed, sync in background");
          }
          else if (c == "feedBig") {
              Serial.println("[Web] 🐠 Starting manual feed big...");
              // 1. EXÉCUTION IMMÉDIATE de l'action physique
              g_autoCtrl.manualFeedBig();
              
              // 2. Marquer l'événement de nourrissage pour le graphique serveur
              g_autoCtrl.setBouffeGrosFlag("1");
              
              // 3. Feedback WebSocket IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              
              // 4. Réponse HTTP IMMÉDIATE (avant email/sync)
              resp="FEED_BIG OK";
              
              // 5. Email + Sync en tâche asynchrone (v11.81: Version améliorée)
              auto* sensorsPtr = &_sensors;
              const uint8_t MAX_ASYNC_TASKS = 5; // Augmenté pour plus de robustesse
              
              if (tryAcquireAsyncSlot(MAX_ASYNC_TASKS)) {
                BaseType_t created = xTaskCreate([](void* param) {
                  SystemSensors* sensors = (SystemSensors*)param;
                  vTaskDelay(pdMS_TO_TICKS(100));
                  
                  // Utilisation de notre helper robuste d'envoi email
                  if (g_webServerContext) {
                    g_webServerContext->sendManualActionEmail(
                      "Bouffe manuelle - Gros poissons", 
                      "Bouffe manuelle", 
                      "NOURRISSAGE_GROS");
                  }
                  
                  // Synchronisation serveur : envoie bouffeGros=1 (marqué ci-dessus)
                  SensorReadings readings = sensors->read();
                  bool syncSuccess = g_autoCtrl.sendFullUpdate(readings, nullptr);
                  Serial.printf("[Web] 📤 Server sync bouffeGros=1 %s\n",
                                syncSuccess ? "completed" : "pending");
                  
                  // Remettre le flag à 0 pour les prochains envois
                  g_autoCtrl.setBouffeGrosFlag("0");
                  
                  releaseAsyncSlot();
                  vTaskDelete(NULL);
                }, "feed_big_sync", 4096, sensorsPtr, 1, nullptr);
                if (created != pdPASS) {
                  releaseAsyncSlot();
                  Serial.println("[Web] ❌ Échec création tâche feed_big_sync");
                  g_autoCtrl.setBouffeGrosFlag("0"); // Reset en cas d'échec
                }
              } else {
                Serial.println(
                  "[Web] ⚠️ Limite de tâches async atteinte - tentative email immédiate");
                // Fallback: tentative d'envoi email immédiat si mémoire suffisante
                if (g_webServerContext && 
                    ESP.getFreeHeap() > g_webServerContext->emailMinHeapBytes) {
                  g_webServerContext->sendManualActionEmail(
                    "Bouffe manuelle - Gros poissons", 
                    "Bouffe manuelle", 
                    "NOURRISSAGE_FALLBACK");
                }
                g_autoCtrl.setBouffeGrosFlag("0"); // Reset
              }
              
              Serial.println("[Web] ✅ Big feed completed, sync in background");
          }
          else if (c == "toggleEmail") {
              Serial.println("[Web] 📧 Toggling Email Notifications...");
              // Toggle Email Notifications
              g_autoCtrl.toggleEmailNotifications();
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              resp = g_autoCtrl.isEmailEnabled() ? "EMAIL_NOTIF_ACTIVÉ" : "EMAIL_NOTIF_DÉSACTIVÉ";
              Serial.printf("[Web] ✅ Email Notifications toggled: %s\n",
                            g_autoCtrl.isEmailEnabled() ? "ON" : "OFF");
          }
          else if (c == "forceWakeup") {
              Serial.println("[Web] 🔄 Toggling Force Wakeup...");
              // Toggle Force Wakeup
              g_autoCtrl.toggleForceWakeup();
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              resp="FORCE_WAKEUP TOGGLE OK";
              Serial.printf("[Web] ✅ Force Wakeup toggled: %s\n",
                            g_autoCtrl.getForceWakeUp() ? "ON" : "OFF");
          }
          else if (c == "resetMode") {
              Serial.println("[Web] 🔄 Triggering Reset Mode...");
              // Trigger Reset Mode
              g_autoCtrl.triggerResetMode();
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              resp="RESET_MODE TRIGGERED OK";
              Serial.println("[Web] ✅ Reset Mode triggered");
          }
          else if (c == "wifiToggle") {
              Serial.println("[Web] 📶 WiFi toggle requested...");
              // Toggle WiFi connection/disconnection
              if (wifi.isConnected()) {
                  Serial.println("[Web] 📶 Disconnecting WiFi...");
                  // Déconnexion
                  wifi.disconnect();
                  resp="WIFI_DISCONNECTED OK";
                  Serial.println("[Web] ✅ WiFi disconnected");
              } else {
                  Serial.println("[Web] 📶 Reconnecting WiFi...");
                  // Reconnexion
                  bool success = wifi.reconnect();
                  if (success) {
                      resp="WIFI_RECONNECTED OK";
                      Serial.println("[Web] ✅ WiFi reconnected successfully");
                  } else {
                      resp="WIFI_RECONNECT_FAILED";
                      Serial.println("[Web] ❌ WiFi reconnection failed");
                  }
              }
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
          }
      }
      
      // Traitement des relais avec feedback immédiat
      if (req->hasParam("relay")) {
          char relayBuf[64];
          strncpy(relayBuf, req->getParam("relay")->value().c_str(), sizeof(relayBuf) - 1);
          relayBuf[sizeof(relayBuf) - 1] = '\0';
          const char* rel = relayBuf;
          Serial.printf("[Web] 🔌 Relay control: %s\\n", rel);
          
          if (strcmp(rel, "pumpTank") == 0) {
              resp = handleRelayAction("tank",
                  [&](){ return _acts.isTankPumpRunning(); },
                  [&](){ g_autoCtrl.startTankPumpManual(); },
                  [&](){ g_autoCtrl.stopTankPumpManual(); },
                  "PUMP_TANK ON", "PUMP_TANK OFF"
              );
          } else if (strcmp(rel, "pumpAqua") == 0) {
              resp = handleRelayAction("aqua",
                  [&](){ return _acts.isAquaPumpRunning(); },
                  [&](){ g_autoCtrl.startAquaPumpManualLocal(); },
                  [&](){ g_autoCtrl.stopAquaPumpManualLocal(); },
                  "PUMP_AQUA ON", "PUMP_AQUA OFF"
              );
          } else if (strcmp(rel, "heater") == 0) {
              resp = handleRelayAction("heater",
                  [&](){ return _acts.isHeaterOn(); },
                  [&](){ g_autoCtrl.startHeaterManualLocal(); },
                  [&](){ g_autoCtrl.stopHeaterManualLocal(); },
                  "HEATER ON", "HEATER OFF"
              );
          } else if (strcmp(rel, "light") == 0) {
              resp = handleRelayAction("light",
                  [&](){ return _acts.isLightOn(); },
                  [&](){ g_autoCtrl.startLightManualLocal(); },
                  [&](){ g_autoCtrl.stopLightManualLocal(); },
                  "LIGHT ON", "LIGHT OFF"
              );
          }
      }
      
      Serial.printf("[Web] 📤 Sending response: %s\\n", resp);
      
      // Réponse immédiate avec headers optimisés
      AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", resp);
      if (response) {
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        req->send(response);
      } else {
        Serial.println("[Web] ❌ Échec création réponse (mémoire?)");
        req->send(500, "text/plain", "Erreur mémoire serveur");
      }
      
      Serial.printf("[Web] ✅ Action completed - Response sent to %s\n", remoteIPBuf);
  });

  // Gestion des requêtes CORS preflight pour /json
  _server->on("/json", HTTP_OPTIONS, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", "");
    if (response) {
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      response->addHeader("Access-Control-Max-Age", "86400");
      req->send(response);
    } else {
      req->send(500);
    }
  });

  // Gestion des requêtes HEAD pour /json (connectivité check)
  _server->on("/json", HTTP_HEAD, [](AsyncWebServerRequest* req){
    g_autoCtrl.notifyLocalWebActivity();
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", "");
    if (response) {
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Access-Control-Allow-Origin", "*");
      req->send(response);
    } else {
      req->send(500);
    }
  });

  // Point de terminaison JSON optimisé - RÉACTIVITÉ MAXIMALE
  _server->on("/json", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req) {
    g_autoCtrl.notifyLocalWebActivity();
    
    // NOUVELLE VÉRIFICATION MÉMOIRE (v11.50)
    if (!ctx.ensureHeap(req, ctx.jsonMinHeapBytes, F("/json"))) {
      return;
    }
    
    // Utiliser allocation statique pour éviter fragmentation mémoire
    static StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Récupérer les données via le cache (optimisé)
    SensorReadings r = sensorCache.getReadings(_sensors);
    
    // Données réelles avec fallback intelligent
    doc["tempWater"] = isnan(r.tempWater) ? 25.5 : r.tempWater;
    doc["tempAir"] = isnan(r.tempAir) ? 22.3 : r.tempAir;
    doc["humidity"] = isnan(r.humidity) ? 65.0 : r.humidity;
    doc["wlAqua"] = r.wlAqua == 0 ? 15.2 : r.wlAqua;
    doc["wlTank"] = r.wlTank == 0 ? 8.7 : r.wlTank;
    doc["wlPota"] = r.wlPota == 0 ? 12.1 : r.wlPota;
    doc["luminosite"] = r.luminosite == 0 ? 450 : r.luminosite;
    doc["pumpAqua"] = _acts.isAquaPumpRunning();
    doc["pumpTank"] = _acts.isTankPumpRunning();
    doc["heater"] = _acts.isHeaterOn();
    doc["light"] = _acts.isLightOn();
    doc["forceWakeup"] = g_autoCtrl.getForceWakeUp();
    
    // Informations WiFi STA
    bool staConnected = WiFi.status() == WL_CONNECTED;
    doc["wifiStaConnected"] = staConnected;
    if (staConnected) {
      char staSSIDBuf[33];
      strncpy(staSSIDBuf, WiFi.SSID().c_str(), sizeof(staSSIDBuf) - 1);
      staSSIDBuf[sizeof(staSSIDBuf) - 1] = '\0';
      doc["wifiStaSSID"] = staSSIDBuf;
      char ipBuf[16];
      IPAddress ip = WiFi.localIP();
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      doc["wifiStaIP"] = ipBuf;
      doc["wifiStaRSSI"] = WiFi.RSSI();
    } else {
      doc["wifiStaSSID"] = "";
      doc["wifiStaIP"] = "";
      doc["wifiStaRSSI"] = 0;
    }
    
    // Informations WiFi AP
    wifi_mode_t mode = WiFi.getMode();
    bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
    doc["wifiApActive"] = apActive;
    if (apActive) {
      char apSSIDBuf[33];
      strncpy(apSSIDBuf, WiFi.softAPSSID().c_str(), sizeof(apSSIDBuf) - 1);
      apSSIDBuf[sizeof(apSSIDBuf) - 1] = '\0';
      doc["wifiApSSID"] = apSSIDBuf;
      char apIpBuf[16];
      IPAddress apIP = WiFi.softAPIP();
      snprintf(apIpBuf, sizeof(apIpBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
      doc["wifiApIP"] = apIpBuf;
      doc["wifiApClients"] = WiFi.softAPgetStationNum();
    } else {
      doc["wifiApSSID"] = "";
      doc["wifiApIP"] = "";
      doc["wifiApClients"] = 0;
    }
    
    doc["timestamp"] = millis();
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (response) {
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Pragma", "no-cache");
      response->addHeader("Expires", "0");
      response->addHeader("X-Content-Type-Options", "nosniff");
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      serializeJson(doc, *response);
      req->send(response);
    } else {
      Serial.println("[Web] ❌ Échec création réponse JSON (mémoire insuffisante)");
      req->send(500, "text/plain", "Memory error");
    }
  });

  // Endpoint version firmware
  // /diag endpoint
  _server->on("/diag", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req) {
    // v11.40: Pas de notifyLocalWebActivity() - endpoint diagnostic
    if (_diag) {
      // Augmente la capacité si l'on inclut taskStats (peut être long)
      StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> big;
      _diag->toJson(big);
      ctx.sendJson(req, big);
      return;
    }
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    ctx.sendJson(req, doc);
  });

  // /pumpstats endpoint optimisé : statistiques de la pompe de réserve
  // Gestion des requêtes CORS preflight pour /dbvars
  _server->on("/dbvars", HTTP_OPTIONS, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", "");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Max-Age", "86400");
    req->send(response);
  });

  // /dbvars endpoint : expose variables fetched from remote server - OPTIMISÉ
  _server->on("/dbvars", HTTP_GET, [](AsyncWebServerRequest* req){
    g_autoCtrl.notifyLocalWebActivity();
    
    // NOUVELLE VÉRIFICATION MÉMOIRE AUGMENTÉE (v11.58)
    const uint32_t MIN_HEAP_FOR_DBVARS = 55000; // 55KB minimum pour dbvars (augmenté de 37%)
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_DBVARS) {
      Serial.printf("[Web] ⚠️ Mémoire insuffisante pour /dbvars (%u < %u bytes)\n", 
                    ESP.getFreeHeap(), MIN_HEAP_FOR_DBVARS);
      req->send(503, "text/plain", "Service temporairement indisponible - mémoire faible");
      return;
    }
    
    // Cache côté serveur : utiliser les données en mémoire d'abord
    static unsigned long lastCacheUpdate = 0;
    static JsonDocument cachedSrc;
    static bool cacheValid = false;
    
    unsigned long now = millis();
    bool useCache = cacheValid && (now - lastCacheUpdate < 30000); // Cache valide 30s
    
    JsonDocument src;
    bool ok = false;
    
    if (useCache) {
      // Utiliser le cache en mémoire
      src = cachedSrc;
      ok = true;
      Serial.println("[WebServer] /dbvars: Using cached data");
    } else {
      // OPTIMISATION: Utiliser UNIQUEMENT le cache flash - JAMAIS d'appel distant bloquant
      // Les appels distants doivent être faits de manière asynchrone
      // dans la tâche d'automatisation
      char cached[2048];
      if (config.loadRemoteVars(cached, sizeof(cached)) && strlen(cached) > 0) {
        auto err = deserializeJson(src, cached);
        if (!err) {
          ok = true;
          Serial.println("[WebServer] /dbvars: Using flash cache (fast path)");
          
          // Mettre à jour le cache mémoire pour accélérer les prochains appels
          cachedSrc = src;
          lastCacheUpdate = now;
          cacheValid = true;
        } else {
          Serial.println("[WebServer] /dbvars: Flash cache parse error");
        }
      } else {
        Serial.println("[WebServer] /dbvars: No flash cache available, using defaults");
      }
      
      // SUPPRESSION: Plus d'appel distant depuis le endpoint HTTP
      // L'automatisation se charge de mettre à jour le cache en arrière-plan
    }
    // Normalise les clés attendues par le dashboard (v11.40: simplifié car NVS normalisé)
    JsonDocument out;
    
    // Helper pour valeurs par défaut
    auto getWithDefault = [&src](const char* key, int defaultVal) -> int {
      return src.containsKey(key) ? src[key].as<int>() : defaultVal;
    };
    auto getFloatWithDefault = [&src](const char* key, float defaultVal) -> float {
      return src.containsKey(key) ? src[key].as<float>() : defaultVal;
    };
    auto getStringWithDefault = [&src](const char* key, const char* defaultVal) -> const char* {
      if (src.containsKey(key)) {
        const char* val = src[key].as<const char*>();
        return (val && strlen(val) > 0) ? val : defaultVal;
      }
      return defaultVal;
    };

    // NOTE v11.40: Depuis la normalisation, NVS contient TOUJOURS les clés au format interface
    // Les fallbacks vers anciens formats (tempsGros, mail, etc.) sont gardés pour compatibilité
    
    // Heures nourrissage (valeurs par défaut: 8h, 12h, 19h)
    out["bouffeMatin"] = getWithDefault("bouffeMatin", 8);
    out["bouffeMidi"]  = getWithDefault("bouffeMidi", 12);
    out["bouffeSoir"]  = getWithDefault("bouffeSoir", 19);

    // Durées nourrissage (valeurs par défaut synchronisées avec BDD distante)
    out["tempsGros"] = getWithDefault("tempsGros",
                                      ActuatorConfig::Default::FEED_BIG_DURATION_SEC);
    out["tempsPetits"] = getWithDefault("tempsPetits",
                                        ActuatorConfig::Default::FEED_SMALL_DURATION_SEC);

    // Seuils (valeurs par défaut raisonnables)
    out["aqThreshold"]     = getWithDefault("aqThreshold", 18);
    out["tankThreshold"]   = getWithDefault("tankThreshold", 80);
    out["heaterThreshold"] = getFloatWithDefault("heaterThreshold", 25.0f);
    out["refillDuration"]  = getWithDefault("refillDuration", 120); // v11.40: Ajouté
    out["limFlood"]        = getWithDefault("limFlood", 5);

    // Email (v11.40: Gestion améliorée avec message si non configuré)
    const char* emailAddr = getStringWithDefault("mail", "Non configuré");
    out["mail"] = emailAddr;
    
    // Email enabled (valeur par défaut: false)
    if (src.containsKey("mailNotif")) {
      out["mailNotif"] = src["mailNotif"].as<const char*>();
    } else {
      out["mailNotif"] = "";
    }

    // Flags/commandes (séparés des heures pour éviter l'écrasement)
    out["bouffeMatinOk"] = config.getBouffeMatinOk();
    out["bouffeMidiOk"] = config.getBouffeMidiOk();
    out["bouffeSoirOk"] = config.getBouffeSoirOk();
    if (src.containsKey("bouffePetits")) {
      out["bouffePetits"] = src["bouffePetits"].as<const char*>();
    }
    if (src.containsKey("bouffeGros"))   out["bouffeGros"]   = src["bouffeGros"].as<const char*>();

    out["ok"] = ok;
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(out, *response);
    req->send(response);
  });

  // Mise à jour des variables distantes locales et envoi vers la BDD distante
  _server->on("/dbvars/update", HTTP_POST, [this, &ctx](AsyncWebServerRequest* req){
    g_autoCtrl.notifyLocalWebActivity();
    // Récupère les paramètres envoyés en x-www-form-urlencoded
    // (getParamCStr défini plus bas)

    // v11.70: Clés acceptées standardisées (schéma serveur)
    const char* KEYS[] = {
      "105","106","107",  // GPIO numériques pour heures nourrissage
      "tempsGros","tempsPetits",
      "aqThreshold","tankThreshold","chauffageThreshold",
      "tempsRemplissageSec","limFlood",
      "mail","mailNotif"
    };

    char extraPairs[512] = {0}; // Buffer pour les paires clé-valeur
    char* p = extraPairs;
    const char* end = extraPairs + sizeof(extraPairs);
    bool any = false;

    // Charger JSON NVS existant (si présent)
    char cachedJson[2048];
    JsonDocument nvsDoc;
    if (config.loadRemoteVars(cachedJson, sizeof(cachedJson)) && strlen(cachedJson) > 0) {
      deserializeJson(nvsDoc, cachedJson);
    }

    // Buffer pour getParam (thread-safe car lambda locale)
    char paramBuf[128];
    auto getParamCStr = [req, &paramBuf](const char* name) -> const char* {
      if (req->hasParam(name, true)) {
        const char* val = req->getParam(name, true)->value().c_str();
        strncpy(paramBuf, val, sizeof(paramBuf) - 1);
        paramBuf[sizeof(paramBuf) - 1] = '\0';
        return paramBuf;
      }
      return "";
    };

    auto appendPair = [&](const char* key, const char* value){
      if (value == nullptr || strlen(value) == 0) return;
      if (p >= end - 1) return; // Pas assez d'espace

      size_t written = snprintf(p, end - p, "%s%s=%s", any ? "&" : "", key, value);
      if (written > 0) {
        p += written;
        any = true;
        // MàJ du cache NVS pour persistance locale immédiate
        nvsDoc[key] = value;
      }
    };

    // v11.70: Lecture directe des paramètres - clés standardisées
    // Heures de nourrissage (GPIO numériques)
    appendPair("105", getParamCStr("bouffeMatin"));  // bouffeMatin
    appendPair("106", getParamCStr("bouffeMidi"));   // bouffeMidi
    appendPair("107", getParamCStr("bouffeSoir"));   // bouffeSoir
    // Durées nourrissage
    appendPair("tempsGros", getParamCStr("tempsGros"));
    appendPair("tempsPetits", getParamCStr("tempsPetits"));
    // Seuils/paramètres
    appendPair("aqThreshold", getParamCStr("aqThreshold"));
    appendPair("tankThreshold", getParamCStr("tankThreshold"));
    appendPair("chauffageThreshold", getParamCStr("chauffageThreshold"));
    appendPair("tempsRemplissageSec", getParamCStr("tempsRemplissageSec"));
    appendPair("limFlood", getParamCStr("limFlood"));
    // Email
    appendPair("mail", getParamCStr("mail"));
    appendPair("mailNotif", getParamCStr("mailNotif"));

    // Sauvegarde immédiate en NVS du JSON fusionné
    {
      char saveStr[2048];
      serializeJson(nvsDoc, saveStr, sizeof(saveStr));
      config.saveRemoteVars(saveStr);
      Serial.printf("[Web] 📥 Config sauvegardée en NVS (%zu bytes)\n", strlen(saveStr));
    }

    // Applique les valeurs localement (sans dépendre du distant)
    {
      g_autoCtrl.applyConfigFromJson(nvsDoc);
      Serial.println("[Web] ✅ Config appliquée localement");
    }
    
    // PRIORITÉ LOCALE (v11.32): Marquer pending sync
    AutomatismPersistence::markConfigPendingSync();

    // Envoi immédiat vers la BDD distante pour répercuter les changements
    bool sent = (WiFi.status() == WL_CONNECTED)
      ? g_autoCtrl.sendFullUpdate(_sensors.read(), any ? extraPairs : nullptr)
      : false;
    
    if (sent) {
      // Sync réussi : effacer pending sync
      AutomatismPersistence::clearConfigPendingSync();
      Serial.println("[Web] ✅ Config synced to server");
    } else {
      Serial.println("[Web] ⏳ Config sync pending (will retry)");
    }
    
    // Toujours retourner 200 pour indiquer que l'enregistrement local s'est bien passé,
    // et indiquer séparément si l'envoi distant a réussi
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    doc["remoteSent"] = sent;
    ctx.sendJson(req, doc);
  });

  // Fichiers statiques avec compression optimisée et gestion Content-Length
  auto sendWithCompression = [](AsyncWebServerRequest* req,
                                const char* path,
                                const char* contentType) {
    // Vérifier si le client accepte la compression
    bool acceptsGzip = req->hasHeader("Accept-Encoding") && 
                      req->getHeader("Accept-Encoding")->value().indexOf("gzip") >= 0;
    
    if (acceptsGzip) {
      // Essayer d'abord la version gzip
      char gz[128];
      snprintf(gz, sizeof(gz), "%s.gz", path);
      if (LittleFS.exists(gz)) {
        // CORRECTION: Vérifier la taille du fichier gzip
        File file = LittleFS.open(gz, "r");
        if (file) {
          size_t fileSize = file.size();
          file.close();
          Serial.printf("[Web] 📏 Gzip file %s size: %u bytes\n", gz, fileSize);
          
          AsyncWebServerResponse* r = req->beginResponse(LittleFS, gz, contentType);
          if (r) {
            r->addHeader("Content-Encoding", "gzip");
            r->addHeader("Cache-Control", "public, max-age=604800");
            r->addHeader("X-Content-Type-Options", "nosniff");
            req->send(r);
            return;
          }
        }
      }
    }
    
    // Fallback vers le fichier normal avec vérification de taille
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      if (file) {
        size_t fileSize = file.size();
        file.close();
        Serial.printf("[Web] 📏 File %s size: %u bytes\n", path, fileSize);
        
        AsyncWebServerResponse* r = req->beginResponse(LittleFS, path, contentType);
        if (r) {
          r->addHeader("Cache-Control", "public, max-age=604800");
          r->addHeader("X-Content-Type-Options", "nosniff");
          req->send(r);
          return;
        }
      }
    }
    
    req->send(404);
  };

  _server->on("/chart.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - asset statique
    sendWithCompression(req, "/chart.js", "application/javascript");
  });
  _server->on("/bootstrap.min.css", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - asset statique
    // Servir le fichier CSS consolidé depuis LittleFS
    if (LittleFS.exists("/bootstrap.min.css")) {
      sendWithCompression(req, "/bootstrap.min.css", "text/css");
    } else {
      // Fallback minimal pour compatibilité
      static const char BOOTSTRAP_MIN_PLACEHOLDER[] PROGMEM =
        "/* bootstrap placeholder */\n"
        ".container-fluid{padding:0 12px;}\n"
        ".row{display:flex;flex-wrap:wrap;margin:0 -12px;}\n"
        "[class^=col-]{padding:0 12px;box-sizing:border-box;}\n"
        ".btn{display:inline-block;padding:10px 20px;"
        "border-radius:6px;border:1px solid #ccc;"
        "background:#f8f9fa;cursor:pointer;}\n"
        ".btn-primary{background:#0d6efd;border-color:#0d6efd;color:#fff;}\n"
        ".btn-warning{background:#ffc107;border-color:#ffc107;}\n"
        ".btn-info{background:#0dcaf0;border-color:#0dcaf0;}\n"
        ".btn-danger{background:#dc3545;border-color:#dc3545;color:#fff;}\n"
        ".btn-success{background:#198754;border-color:#198754;color:#fff;}\n"
        ".badge{display:inline-block;padding:.35em .65em;border-radius:.25rem;}\n"
        ".bg-secondary{background:#6c757d;color:#fff;}\n"
        ".bg-primary{background:#0d6efd;color:#fff;}\n"
        ".bg-info{background:#0dcaf0;color:#000;}\n"
        ".bg-warning{background:#ffc107;color:#000;}\n"
        ".bg-success{background:#198754;color:#fff;}\n"
        ".bg-danger{background:#dc3545;color:#fff;}\n";
      AsyncWebServerResponse* r = req->beginResponse_P(200, "text/css", BOOTSTRAP_MIN_PLACEHOLDER);
      if (r) {
        r->addHeader("Cache-Control", "public, max-age=604800");
        r->addHeader("X-Content-Type-Options", "nosniff");
        req->send(r);
      } else {
        req->send(500);
      }
    }
  });
  _server->on("/utils.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - asset statique
    sendWithCompression(req, "/utils.js", "application/javascript");
  });
  // Manifest PWA
  _server->on("/manifest.json", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - manifest statique
    sendWithCompression(req, "/manifest.json", "application/json");
  });

  // Service Worker pour PWA
  _server->on("/sw.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - service worker
    sendWithCompression(req, "/sw.js", "application/javascript");
  });

  // Page de mise à jour OTA via le système personnalisé
  // L'OTA est géré par le système personnalisé dans app.cpp
  // Endpoint /update redirige vers les informations OTA
  _server->on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    // v11.40: Pas de notifyLocalWebActivity() - page info OTA
    req->send(200, "text/html", 
      "<html><head><title>FFP5CS OTA</title></head><body>"
      "<h1>FFP5CS - Mise à jour OTA</h1>"
      "<p>Le système OTA est géré automatiquement par le firmware.</p>"
      "<p>Les mises à jour sont vérifiées toutes les 2 heures.</p>"
      "<p><a href='/'>Retour au dashboard</a></p>"
      "</body></html>");
  });

  // /mailtest endpoint: envoie un e-mail de test
  _server->on("/mailtest", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Action utilisateur critique
    g_autoCtrl.notifyLocalWebActivity();
    char subjBuf[128];
    if (req->hasParam("subject")) {
      strncpy(subjBuf, req->getParam("subject")->value().c_str(), sizeof(subjBuf) - 1);
      subjBuf[sizeof(subjBuf) - 1] = '\0';
    } else {
      strncpy(subjBuf, "Test FFP5CS", sizeof(subjBuf) - 1);
      subjBuf[sizeof(subjBuf) - 1] = '\0';
    }
    char bodyBuf[256];
    if (req->hasParam("body")) {
      strncpy(bodyBuf, req->getParam("body")->value().c_str(), sizeof(bodyBuf) - 1);
      bodyBuf[sizeof(bodyBuf) - 1] = '\0';
    } else {
      strncpy(bodyBuf, "Ceci est un e-mail de test envoyé depuis l'ESP32.", sizeof(bodyBuf) - 1);
      bodyBuf[sizeof(bodyBuf) - 1] = '\0';
    }
    char destBuf[128];
    if (req->hasParam("to")) {
      strncpy(destBuf, req->getParam("to")->value().c_str(), sizeof(destBuf) - 1);
      destBuf[sizeof(destBuf) - 1] = '\0';
    } else {
      strncpy(destBuf, EmailConfig::DEFAULT_RECIPIENT, sizeof(destBuf) - 1);
      destBuf[sizeof(destBuf) - 1] = '\0';
    }
    bool ok = mailer.sendAlert(subjBuf, bodyBuf, destBuf);
    const char* resp = ok ? "OK" : "FAIL";
    req->send(200, "text/plain", resp);
  });

#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  // Maintenance: format LittleFS (use with care)
  _server->on("/fs/format", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Action maintenance critique
    g_autoCtrl.notifyLocalWebActivity();
    if (!req->hasParam("confirm")) {
      req->send(400, "text/plain", "Missing confirm=1");
      return;
    }
    if (req->getParam("confirm")->value() != "1") {
      req->send(400, "text/plain", "confirm must be 1");
      return;
    }
    bool ok = LittleFS.format();
    req->send(ok ? 200 : 500, "text/plain", ok ? "LittleFS formatted" : "Format failed");
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

  // Page simple de formulaire pour modifier les variables BDD locales et pousser vers le serveur
  _server->on("/dbvars/form", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - formulaire legacy
    const char* html =
      "<html><head><meta charset='utf-8'><title>Variables BDD</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>body{font-family:sans-serif;padding:16px;}"
      "label{display:block;margin-top:8px;}"
      "input,button{font-size:16px;padding:6px 8px;}"
      "fieldset{margin-bottom:12px;}" 
      "button{margin-right:8px;}</style></head><body>"
      "<h2>Modifier les variables BDD</h2>"
      "<form method='POST' action='/dbvars/update'>"
      "<fieldset><legend>Nourrissage</legend>"
      "Heure matin: <input type='number' name='bouffeMatin' min='0' max='23'><br>"
      "Heure midi: <input type='number' name='bouffeMidi' min='0' max='23'><br>"
      "Heure soir: <input type='number' name='bouffeSoir' min='0' max='23'><br>"
      "Durée gros (s): <input type='number' name='tempsGros' min='0' max='120'><br>"
      "Durée petits (s): <input type='number' name='tempsPetits' min='0' max='120'><br>"
      "</fieldset>"
      "<fieldset><legend>Seuils</legend>"
      "Seuil Aquarium (cm): <input type='number' name='aqThreshold' min='0' max='1000'><br>"
      "Seuil Réservoir (cm): <input type='number' name='tankThreshold' min='0' max='1000'><br>"
      "Seuil Chauffage (°C): <input type='number' step='0.1' name='heaterThreshold'><br>"
      "Durée Remplissage (s): <input type='number' name='refillDuration' min='0' max='3600'><br>"
      "Limite Inondation (cm): <input type='number' name='limFlood' min='0' max='1000'><br>"
      "</fieldset>"
      "<fieldset><legend>Email</legend>"
      "Adresse: <input type='email' name='mail'><br>"
      "Notifications: <input type='checkbox' name='mailNotif' value='checked'> activées<br>"
      "</fieldset>"
      "<button type='submit'>Enregistrer</button>"
      "<a href='/'><button type='button'>Retour Dashboard</button></a>"
      "</form>"
      "<p>Astuce: seuls les champs remplis seront envoyés et synchronisés.</p>"
      "</body></html>";
    req->send(200, "text/html", html);
  });

  // /testota endpoint: active manuellement le flag OTA pour les tests
  _server->on("/testota", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint de test
    config.setOtaUpdateFlag(true);
    req->send(200, "text/plain", "Flag OTA activé - redémarrez pour tester l'email");
  });

  // -------------------------------------------------------------------
  // NVS Inspector: lister, modifier, effacer les variables persistantes
  // -------------------------------------------------------------------
#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  _server->on("/nvs.json", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Outil de debug interactif
    g_autoCtrl.notifyLocalWebActivity();
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    res->print('{');
    res->print("\"namespaces\":{");

    bool firstNs = true;
    nvs_iterator_t it = nullptr;
    if (nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it) == ESP_OK) {
      do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        // Ouvre le namespace si c'est la première fois qu'on le rencontre
        if (!firstNs) res->print(',');
        firstNs = false;
        res->print('"'); res->print(info.namespace_name); res->print("\":{");

        bool firstKey = true;
        // Itère toutes les clés de ce namespace en rouvrant l'itérateur filtré
        nvs_iterator_t it2 = nullptr;
        if (nvs_entry_find(NVS_DEFAULT_PART_NAME, info.namespace_name,
                           NVS_TYPE_ANY, &it2) == ESP_OK) {
          do {
            nvs_entry_info_t e2; nvs_entry_info(it2, &e2);
            if (strcmp(e2.namespace_name, info.namespace_name) != 0) continue;
            if (!firstKey) res->print(',');
            firstKey = false;
            res->print('"'); res->print(e2.key); res->print("\":{");

            nvs_handle_t h;
            if (nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
              auto typeToStr = [](nvs_type_t t)->const char*{
                switch (t) {
                  case NVS_TYPE_U8:  return "U8";
                  case NVS_TYPE_I8:  return "I8";
                  case NVS_TYPE_U16: return "U16";
                  case NVS_TYPE_I16: return "I16";
                  case NVS_TYPE_U32: return "U32";
                  case NVS_TYPE_I32: return "I32";
                  case NVS_TYPE_U64: return "U64";
                  case NVS_TYPE_I64: return "I64";
                  case NVS_TYPE_STR: return "STR";
                  case NVS_TYPE_BLOB:return "BLOB";
                  default: return "UNKNOWN";
                }
              };
              res->print("\"type\":\""); res->print(typeToStr(e2.type)); res->print("\"");

              esp_err_t err = ESP_ERR_INVALID_ARG;
              switch (e2.type) {
                case NVS_TYPE_U8: {
                  uint8_t v = 0;
                  err = nvs_get_u8(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_I8: {
                  int8_t v = 0;
                  err = nvs_get_i8(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_U16: {
                  uint16_t v = 0;
                  err = nvs_get_u16(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_I16: {
                  int16_t v = 0;
                  err = nvs_get_i16(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_U32: {
                  uint32_t v = 0;
                  err = nvs_get_u32(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_I32: {
                  int32_t v = 0;
                  err = nvs_get_i32(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print(v);
                  }
                } break;
                case NVS_TYPE_U64: {
                  uint64_t v = 0;
                  err = nvs_get_u64(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print((uint64_t)v);
                  }
                } break;
                case NVS_TYPE_I64: {
                  int64_t v = 0;
                  err = nvs_get_i64(h, e2.key, &v);
                  if (err == ESP_OK) {
                    res->print(",\"value\":");
                    res->print((int64_t)v);
                  }
                } break;
                case NVS_TYPE_STR: {
                  size_t len = 0; err = nvs_get_str(h, e2.key, nullptr, &len);
                  if (err == ESP_OK) {
                    if (len > 0 && len < BufferConfig::JSON_DOCUMENT_SIZE) {
                      char* buf = (char*)malloc(len);
                      if (buf && nvs_get_str(h, e2.key, buf, &len) == ESP_OK) {
                        res->print(",\"value\":\"");
                        for (size_t i=0;i<len && buf[i];++i){
                          char c=buf[i];
                          if (c=='"' || c=='\\') { res->print('\\'); res->print(c); }
                          else if (c=='\n') { res->print("\\n"); }
                          else if (c=='\r') { res->print("\\r"); }
                          else { res->print(c); }
                        }
                        res->print("\"");
                      } else {
                        res->print(",\"value\":\"<OOM>\"");
                      }
                      if (buf) free(buf);
                    } else if (len >= BufferConfig::JSON_DOCUMENT_SIZE) {
                      res->print(",\"value\":\"<too_long>\"");
                    } else {
                      res->print(",\"value\":\"\"");
                    }
                  }
                } break;
                case NVS_TYPE_BLOB: {
                  size_t len = 0; err = nvs_get_blob(h, e2.key, nullptr, &len);
                  if (err == ESP_OK) {
                    res->print(",\"length\":"); res->print((uint32_t)len);
                    res->print(",\"value\":\"<blob>\"");
                  }
                } break;
                default: break;
              }
              nvs_close(h);
            }
            res->print('}');
          } while (nvs_entry_next(&it2) == ESP_OK);
          nvs_release_iterator(it2);
        }

        res->print('}');
      } while (nvs_entry_next(&it) == ESP_OK);
      nvs_release_iterator(it);
    }

    res->print('}'); // namespaces
    res->print('}'); // root
    req->send(res);
  });

  _server->on("/nvs", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Page NVS Inspector interactive
    g_autoCtrl.notifyLocalWebActivity();
    const char* html =
      "<html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>NVS Inspector</title>"
      "<style>body{font-family:sans-serif;margin:12px;}table{border-collapse:collapse;width:100%;}"
      "th,td{border:1px solid #ddd;padding:6px;}"
      "th{background:#f3f3f3;}"
      "input[type=number]{width:120px;}"
      "code{background:#f7f7f7;padding:2px 4px;border-radius:3px;}</style>"
      "</head><body>"
      "<h2>NVS - Variables persistantes</h2>"
      "<p><a href='/'>&larr; Retour</a> | <button onclick=load()>Recharger</button></p>"
      "<div id='content'>Chargement...</div>"
      "<script>"
      "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;');}"
      "async function load(){"
      "const r=await fetch('/nvs.json');"
      "const j=await r.json();"
      "let h='';"
      "for(const ns in j.namespaces){"
      "h+=`<h3>Namespace <code>${esc(ns)}</code></h3>`;"
      "h+=`<table><tr><th>Clé</th><th>Type</th><th>Valeur</th><th>Actions</th></tr>`;"
      "const obj=j.namespaces[ns];"
      "for(const k in obj){"
      "const it=obj[k];"
      "let input='';"
      "const t=it.type;"
      "const id=ns+'::'+k;"
      "if(t==='STR'){"
      "input=`<input id='v_${id}' type='text' "
      "value='${esc(it.value||'')}'/>`;"
      "}"
      "else if(t==='BLOB'){"
      "input=`<em>blob (${it.length||0} bytes)</em>`;"
      "}"
      "else{"
      "input=`<input id='v_${id}' type='number' "
      "value='${esc(it.value)}'/>`;"
      "}"
      "h+=`<tr><td><code>${esc(k)}</code></td><td>${t}</td><td>${input}</td>`+"
      "`<td>`+`<button onclick=save('${ns}','${k}','${t}')>Enregistrer</button> `+"
      "`<button onclick=delKey('${ns}','${k}')>Effacer</button>`+`</td></tr>`;}"
      "h+='</table>';"
      "h+=`<p><button onclick=clearNs('${ns}')>"
      "Effacer le namespace</button></p>`;"
      "}"
      "document.getElementById('content').innerHTML=h;}"
      "async function save(ns,k,t){"
      "const id='v_'+ns+'::'+k;"
      "const el=document.getElementById(id);"
      "let v=el?el.value:'';"
      "if(t!=='STR' && t!=='BLOB'){"
      "if(v==='')v='0';"
      "}"
      "const p=new URLSearchParams();"
      "p.set('ns',ns);p.set('key',k);"
      "p.set('type',t);p.set('value',v);"
      "const r=await fetch('/nvs/set',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "async function delKey(ns,k){"
      "if(!confirm('Effacer '+ns+'::'+k+' ?'))return;"
      "const p=new URLSearchParams();"
      "p.set('ns',ns);p.set('key',k);"
      "const r=await fetch('/nvs/erase',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "async function clearNs(ns){"
      "if(!confirm('Effacer tout le namespace '+ns+' ?'))return;"
      "const p=new URLSearchParams();p.set('ns',ns);"
      "const r=await fetch('/nvs/erase_ns',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "load();"
      "</script>"
      "</body></html>";
    req->send(200, "text/html", html);
  });

  _server->on("/nvs/set", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64], typeBuf[16], valueBuf[256];
    auto getP = [req](const char* n, char* buf, size_t bufSize) -> bool {
      if (req->hasParam(n, true)) {
        const char* val = req->getParam(n, true)->value().c_str();
        strncpy(buf, val, bufSize - 1);
        buf[bufSize - 1] = '\0';
        return true;
      }
      buf[0] = '\0';
      return false;
    };
    if (!getP("ns", nsBuf, sizeof(nsBuf)) || 
        !getP("key", keyBuf, sizeof(keyBuf)) || 
        !getP("type", typeBuf, sizeof(typeBuf))) {
      req->send(400, "text/plain", "Missing ns/key/type");
      return;
    }
    getP("value", valueBuf, sizeof(valueBuf));
    
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }

    auto strToType = [](const char* s)->nvs_type_t{
      if (strcmp(s, "U8") == 0) return NVS_TYPE_U8;
      if (strcmp(s, "I8") == 0) return NVS_TYPE_I8;
      if (strcmp(s, "U16") == 0) return NVS_TYPE_U16;
      if (strcmp(s, "I16") == 0) return NVS_TYPE_I16;
      if (strcmp(s, "U32") == 0) return NVS_TYPE_U32;
      if (strcmp(s, "I32") == 0) return NVS_TYPE_I32;
      if (strcmp(s, "U64") == 0) return NVS_TYPE_U64;
      if (strcmp(s, "I64") == 0) return NVS_TYPE_I64;
      if (strcmp(s, "STR") == 0) return NVS_TYPE_STR;
      if (strcmp(s, "BLOB") == 0) return NVS_TYPE_BLOB;
      return NVS_TYPE_ANY;
    };

    nvs_type_t t = strToType(typeBuf);
    if (t == NVS_TYPE_ANY) {
      nvs_close(h);
      req->send(400, "text/plain", "Invalid type");
      return;
    }

    switch (t) {
      case NVS_TYPE_U8: {
        uint8_t v = (uint8_t)atoi(valueBuf);
        err = nvs_set_u8(h, keyBuf, v);
      } break;
      case NVS_TYPE_I8: {
        int8_t v = (int8_t)atoi(valueBuf);
        err = nvs_set_i8(h, keyBuf, v);
      } break;
      case NVS_TYPE_U16: {
        uint16_t v = (uint16_t)atoi(valueBuf);
        err = nvs_set_u16(h, keyBuf, v);
      } break;
      case NVS_TYPE_I16: {
        int16_t v = (int16_t)atoi(valueBuf);
        err = nvs_set_i16(h, keyBuf, v);
      } break;
      case NVS_TYPE_U32: {
        uint32_t v = (uint32_t)atol(valueBuf);
        err = nvs_set_u32(h, keyBuf, v);
      } break;
      case NVS_TYPE_I32: {
        int32_t v = (int32_t)atol(valueBuf);
        err = nvs_set_i32(h, keyBuf, v);
      } break;
      case NVS_TYPE_U64: {
        uint64_t v = (uint64_t)atoll(valueBuf);
        err = nvs_set_u64(h, keyBuf, v);
      } break;
      case NVS_TYPE_I64: {
        int64_t v = (int64_t)atoll(valueBuf);
        err = nvs_set_i64(h, keyBuf, v);
      } break;
      case NVS_TYPE_STR: { err = nvs_set_str(h, keyBuf, valueBuf); } break;
      case NVS_TYPE_BLOB: {
        req->send(400, "text/plain", "BLOB set not supported");
        nvs_close(h);
        return;
      }
      default: break;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Write failed"); return; }

    // Rafraîchir l'état runtime si nécessaire
    if (strcmp(nsBuf, "bouffe") == 0 || strcmp(nsBuf, "ota") == 0) {
      config.loadBouffeFlags();
    } else if (strcmp(nsBuf, "rtc") == 0) {
      power.loadTimeFromFlash();
    } else if (strcmp(nsBuf, "remoteVars") == 0 && strcmp(keyBuf, "json") == 0) {
      char js[256];
      strncpy(js, valueBuf, sizeof(js) - 1);
      js[sizeof(js) - 1] = '\0';
      if (strlen(js) > 0) {
        JsonDocument tmp;
        if (!deserializeJson(tmp, js)) {
          g_autoCtrl.applyConfigFromJson(tmp);
        }
      }
    }

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    ctx.sendJson(req, doc);
  });

  _server->on("/nvs/erase", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64];
    auto getP = [req](const char* n, char* buf, size_t bufSize) -> bool {
      if (req->hasParam(n, true)) {
        const char* val = req->getParam(n, true)->value().c_str();
        strncpy(buf, val, bufSize - 1);
        buf[bufSize - 1] = '\0';
        return true;
      }
      buf[0] = '\0';
      return false;
    };
    if (!getP("ns", nsBuf, sizeof(nsBuf)) || !getP("key", keyBuf, sizeof(keyBuf))) { 
      req->send(400, "text/plain", "Missing ns/key"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_key(h, keyBuf); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Erase failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    ctx.sendJson(req, doc);
  });

  _server->on("/nvs/erase_ns", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32];
    auto getP = [req](const char* n, char* buf, size_t bufSize) -> bool {
      if (req->hasParam(n, true)) {
        const char* val = req->getParam(n, true)->value().c_str();
        strncpy(buf, val, bufSize - 1);
        buf[bufSize - 1] = '\0';
        return true;
      }
      buf[0] = '\0';
      return false;
    };
    if (!getP("ns", nsBuf, sizeof(nsBuf))) { 
      req->send(400, "text/plain", "Missing ns"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Erase namespace failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    ctx.sendJson(req, doc);
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

  // ========================================
  // GESTIONNAIRE WIFI - ENDPOINTS BACKEND
  // ========================================
  
  // Scanner les réseaux WiFi disponibles
  _server->on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Action WiFi critique
    g_autoCtrl.notifyLocalWebActivity();
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Scanner les réseaux WiFi
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    
    doc["success"] = (n >= 0);
    doc["count"] = n;
    
    if (n > 0) {
      JsonArray networks = doc.createNestedArray("networks");
      
      for (int i = 0; i < n; i++) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
        network["channel"] = WiFi.channel(i);
        
        // Masquer les réseaux cachés (SSID vide)
        if (WiFi.SSID(i).length() == 0) {
          network["ssid"] = "<Hidden Network>";
          network["hidden"] = true;
        } else {
          network["hidden"] = false;
        }
      }
    } else {
      doc["error"] = "No networks found or scan failed";
    }
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Lister les réseaux WiFi sauvegardés
  _server->on("/wifi/saved", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint lecture seule
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    JsonArray networks = doc.createNestedArray("networks");
    size_t totalCount = 0;
    
    // 1. Ajouter les réseaux statiques de secrets.h
    for (size_t i = 0; i < Secrets::WIFI_COUNT; i++) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = Secrets::WIFI_LIST[i].ssid;
      network["password"] = Secrets::WIFI_LIST[i].password;
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
            char* buffer = (char*)malloc(required_size);
            if (buffer) {
              err = nvs_get_blob(nvsHandle, key, buffer, &required_size);
              if (err == ESP_OK) {
                JsonObject network = networks.createNestedObject();
                
                // Parser le format: "ssid|password"
                char* separator = strchr(buffer, '|');
                if (separator != nullptr && separator > buffer) {
                  *separator = '\0';  // Terminer ssid
                  char* ssid = buffer;
                  char* password = separator + 1;
                  
                  // Vérifier si ce réseau n'existe pas déjà dans les réseaux statiques
                  bool existsInStatic = false;
                  for (size_t j = 0; j < Secrets::WIFI_COUNT; j++) {
                    if (strcmp(ssid, Secrets::WIFI_LIST[j].ssid) == 0) {
                      existsInStatic = true;
                      break;
                    }
                  }
                  
                  // Ajouter seulement s'il n'existe pas déjà
                  if (!existsInStatic) {
                    network["ssid"] = ssid;
                    network["password"] = password;
                    network["index"] = totalCount;
                    network["source"] = "saved"; // Marquer comme réseau sauvegardé
                    totalCount++;
                  }
                }
              }
              free(buffer);
            }
          }
        }
      }
      
      nvs_close(nvsHandle);
    }
    
    doc["success"] = true;
    doc["count"] = totalCount;
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Connecter à un réseau WiFi
  _server->on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Changement WiFi critique
    g_autoCtrl.notifyLocalWebActivity();
    
    char ssidBuf[64], passwordBuf[65], saveBuf[8];
    auto getParam = [req](const char* name, char* buf, size_t bufSize) -> bool {
      if (req->hasParam(name, true)) {
        const char* val = req->getParam(name, true)->value().c_str();
        if (val) {
          strncpy(buf, val, bufSize - 1);
          buf[bufSize - 1] = '\0';
          return true;
        }
      }
      buf[0] = '\0';
      return false;
    };
    
    bool hasSsid = getParam("ssid", ssidBuf, sizeof(ssidBuf));
    bool hasPassword = getParam("password", passwordBuf, sizeof(passwordBuf));
    bool hasSave = getParam("save", saveBuf, sizeof(saveBuf));
    
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
          
          // Vérifier si le réseau existe déjà
          bool exists = false;
          for (size_t i = 0; i < networkCount; i++) {
            char key[16];
            snprintf(key, sizeof(key), "net_%zu", i);
            
            size_t data_size = 0;
            err = nvs_get_blob(nvsHandle, key, nullptr, &data_size);
            if (err == ESP_OK && data_size > 0) {
              char* buffer = (char*)malloc(data_size);
              if (buffer) {
                err = nvs_get_blob(nvsHandle, key, buffer, &data_size);
                if (err == ESP_OK) {
                  char* separator = strchr(buffer, '|');
                  if (separator != nullptr && separator > buffer) {
                    *separator = '\0';
                    if (strcmp(buffer, ssidBuf) == 0) {
                      exists = true;
                      // Mettre à jour le mot de passe
                      char newData[130];
                      snprintf(newData, sizeof(newData), "%s|%s", ssidBuf, passwordBuf);
                      nvs_set_blob(nvsHandle, key, newData, strlen(newData) + 1);
                      nvs_commit(nvsHandle);
                      Serial.printf("[WiFi] Réseau '%s' mis à jour dans NVS\n", ssidBuf);
                    }
                    *separator = '|';  // Restaurer pour free()
                  }
                }
                free(buffer);
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
      
      // Envoyer une réponse immédiate AVANT de déconnecter
      // Cela permet au client de recevoir la réponse avant la perte de connexion
      doc["success"] = true;
      doc["message"] = "Connection attempt started";
      doc["ssid"] = ssidBuf;
      doc["note"] = 
        "Connection may take up to 15 seconds. "
        "WebSocket will reconnect automatically.";
      
      char json[512];
      serializeJson(doc, json, sizeof(json));
      
      AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      req->send(response);
      
      // Attendre que la réponse HTTP soit complètement envoyée
      Serial.println("[WiFi] Attente envoi réponse HTTP...");
      vTaskDelay(pdMS_TO_TICKS(300));
      
      // Notifier les clients WebSocket du changement imminent
      Serial.println("[WiFi] Notification des clients WebSocket...");
      g_realtimeWebSocket.notifyWifiChange(ssidBuf);
      vTaskDelay(pdMS_TO_TICKS(200));
      
      // Fermer proprement toutes les connexions WebSocket
      Serial.println("[WiFi] Fermeture des connexions WebSocket...");
      g_realtimeWebSocket.closeAllConnections();
      
      // Attendre que toutes les connexions soient bien fermées
      vTaskDelay(pdMS_TO_TICKS(500));
      
      // MAINTENANT déconnecter et reconnecter le WiFi
      Serial.printf("[WiFi] Déconnexion du réseau actuel\n");
      WiFi.disconnect(false, true);
      vTaskDelay(pdMS_TO_TICKS(200));
      
      // Attendre la connexion avec timeout dans une tâche séparée
      // pour ne pas bloquer le serveur web
      WifiConnectTaskParams* params = new WifiConnectTaskParams{};
      if (!params) {
        Serial.println("[WiFi] ❌ Allocation paramètres connexion échouée");
        return;
      }
      strncpy(params->ssid, ssidBuf, sizeof(params->ssid) - 1);
      params->ssid[sizeof(params->ssid) - 1] = '\0';
      strncpy(params->password, passwordBuf, sizeof(params->password) - 1);
      params->password[sizeof(params->password) - 1] = '\0';
      
      BaseType_t created = xTaskCreate([](void* param) {
        WifiConnectTaskParams* p = (WifiConnectTaskParams*)param;
        bool connected = wifi.connectTo(p->ssid, p->password);
        if (connected) {
          IPAddress ip = WiFi.localIP();
          Serial.printf(
            "[WiFi] Connecté avec succès à '%s' (IP: %d.%d.%d.%d, RSSI: %d dBm)\n", 
            p->ssid, ip[0], ip[1], ip[2], ip[3], WiFi.RSSI());
          // Notifier le changement via WebSocket (si encore connecté)
          g_realtimeWebSocket.broadcastNow();
        } else {
          Serial.printf("[WiFi] Échec de connexion à '%s' (timeout)\n",
                        p->ssid);
          // Retourner en mode AP si la connexion échoue
          // Le WifiManager s'en chargera automatiquement
        }
        
        delete p;
        vTaskDelete(NULL); // Supprimer cette tâche
      }, "wifi_connect_task", 4096, params, 1, nullptr);
      if (created != pdPASS) {
        Serial.println("[WiFi] ❌ Échec création tâche wifi_connect_task");
        delete params;
      }
      
      return; // Retourner immédiatement, la connexion se fait en arrière-plan
    }
    
    // Si on arrive ici, c'est qu'il y a eu une erreur
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Supprimer un réseau WiFi sauvegardé
  _server->on("/wifi/remove", HTTP_POST, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification WiFi
    g_autoCtrl.notifyLocalWebActivity();
    
    char ssidBuf[64];
    bool hasSsid = false;
    if (req->hasParam("ssid", true)) {
      const char* ssidVal = req->getParam("ssid", true)->value().c_str();
      if (ssidVal) {
        strncpy(ssidBuf, ssidVal, sizeof(ssidBuf) - 1);
        ssidBuf[sizeof(ssidBuf) - 1] = '\0';
        hasSsid = true;
      } else {
        ssidBuf[0] = '\0';
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
        for (size_t i = 0; i < networkCount; i++) {
          char key[16];
          snprintf(key, sizeof(key), "net_%zu", i);
          
          size_t data_size = 0;
          err = nvs_get_blob(nvsHandle, key, nullptr, &data_size);
          if (err == ESP_OK && data_size > 0) {
            char* buffer = (char*)malloc(data_size);
            if (buffer) {
              err = nvs_get_blob(nvsHandle, key, buffer, &data_size);
              if (err == ESP_OK) {
                char* separator = strchr(buffer, '|');
                if (separator != nullptr && separator > buffer) {
                  *separator = '\0';
                  if (strcmp(buffer, ssidBuf) == 0) {
                    found = true;
                    foundIndex = i;
                    *separator = '|';  // Restaurer pour free()
                    break;
                  }
                  *separator = '|';  // Restaurer pour free()
                }
              }
              free(buffer);
            }
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
            if (err == ESP_OK && data_size > 0) {
              char* buffer = (char*)malloc(data_size);
              if (buffer) {
                err = nvs_get_blob(nvsHandle, oldKey, buffer, &data_size);
                if (err == ESP_OK) {
                  nvs_set_blob(nvsHandle, newKey, buffer, data_size);
                  nvs_erase_key(nvsHandle, oldKey);
                }
                free(buffer);
              }
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
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Obtenir le statut WiFi actuel
  _server->on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint lecture seule
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Statut de connexion STA
    bool staConnected = WiFi.status() == WL_CONNECTED;
    doc["staConnected"] = staConnected;
    
    if (staConnected) {
      char staSSIDBuf[33];
      strncpy(staSSIDBuf, WiFi.SSID().c_str(), sizeof(staSSIDBuf) - 1);
      staSSIDBuf[sizeof(staSSIDBuf) - 1] = '\0';
      doc["staSSID"] = staSSIDBuf;
      IPAddress staIP = WiFi.localIP();
      char staIPBuf[16];
      snprintf(staIPBuf, sizeof(staIPBuf), "%d.%d.%d.%d", staIP[0], staIP[1], staIP[2], staIP[3]);
      doc["staIP"] = staIPBuf;
      doc["staRSSI"] = WiFi.RSSI();
      char staMacBuf[18];
      strncpy(staMacBuf, WiFi.macAddress().c_str(), sizeof(staMacBuf) - 1);
      staMacBuf[sizeof(staMacBuf) - 1] = '\0';
      doc["staMac"] = staMacBuf;
    } else {
      doc["staSSID"] = "";
      doc["staIP"] = "";
      doc["staRSSI"] = 0;
      char staMacBuf[18];
      strncpy(staMacBuf, WiFi.macAddress().c_str(), sizeof(staMacBuf) - 1);
      staMacBuf[sizeof(staMacBuf) - 1] = '\0';
      doc["staMac"] = staMacBuf;
    }
    
    // Statut AP
    wifi_mode_t mode = WiFi.getMode();
    bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
    doc["apActive"] = apActive;
    
    if (apActive) {
      char apSSIDBuf[33];
      strncpy(apSSIDBuf, WiFi.softAPSSID().c_str(), sizeof(apSSIDBuf) - 1);
      apSSIDBuf[sizeof(apSSIDBuf) - 1] = '\0';
      doc["apSSID"] = apSSIDBuf;
      IPAddress apIP = WiFi.softAPIP();
      char apIPBuf[16];
      snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
      doc["apIP"] = apIPBuf;
      doc["apClients"] = WiFi.softAPgetStationNum();
    } else {
      doc["apSSID"] = "";
      doc["apIP"] = "";
      doc["apClients"] = 0;
    }
    
    doc["mode"] = (mode == WIFI_STA) ? "STA" : (mode == WIFI_AP) ? "AP" : "AP_STA";
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });

  // Endpoint de diagnostic pour les performances
  _server->on("/server-status", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint diagnostic
    
    IPAddress remoteIP = req->client()->remoteIP();
    Serial.printf("[Web] 📊 Server status request from %d.%d.%d.%d\n",
                  remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapTotal"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramTotal"] = ESP.getPsramSize();
    doc["uptime"] = millis();
    doc["webServerTimeout"] = NetworkConfig::WEB_SERVER_TIMEOUT_MS;
    doc["maxConnections"] = NetworkConfig::WEB_SERVER_MAX_CONNECTIONS;
    
    // Ajouter des informations de debug supplémentaires
    doc["wifiStatus"] = WiFi.status();
    char wifiSSIDBuf[33];
    strncpy(wifiSSIDBuf, WiFi.SSID().c_str(), sizeof(wifiSSIDBuf) - 1);
    wifiSSIDBuf[sizeof(wifiSSIDBuf) - 1] = '\0';
    doc["wifiSSID"] = wifiSSIDBuf;
    IPAddress wifiIP = WiFi.localIP();
    char wifiIPBuf[16];
    snprintf(wifiIPBuf, sizeof(wifiIPBuf), "%d.%d.%d.%d",
             wifiIP[0], wifiIP[1], wifiIP[2], wifiIP[3]);
    doc["wifiIP"] = wifiIPBuf;
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["webSocketClients"] = g_realtimeWebSocket.getConnectedClients();
    doc["forceWakeup"] = g_autoCtrl.getForceWakeUp();
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-cache");
    serializeJson(doc, *response);
    req->send(response);
  });

  // === API: Remote flags control (send/recv) ===
  _server->on("/api/remote-flags", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["sendEnabled"] = config.isRemoteSendEnabled();
    doc["recvEnabled"] = config.isRemoteRecvEnabled();
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    serializeJson(doc, *response);
    req->send(response);
  });

  _server->on("/api/remote-flags", HTTP_POST, [](AsyncWebServerRequest* req){
    bool changed = false;
    if (req->hasParam("send", true)) {
      const char* v = req->getParam("send", true)->value().c_str();
      if (v) {
        char vLower[16];
        strncpy(vLower, v, sizeof(vLower) - 1);
        vLower[sizeof(vLower) - 1] = '\0';
        // Convertir en minuscules et trim
        for (char* p = vLower; *p; p++) {
          if (*p >= 'A' && *p <= 'Z') *p += 32;  // tolower
        }
        // Trim (supprimer espaces début/fin)
        char* start = vLower;
        while (*start == ' ' || *start == '\t') start++;
        char* end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';
        bool enable = (strcmp(start, "1") == 0 || 
                      strcmp(start, "true") == 0 || 
                      strcmp(start, "on") == 0);
        config.setRemoteSendEnabled(enable); changed = true;
      }
    }
    if (req->hasParam("recv", true)) {
      const char* v = req->getParam("recv", true)->value().c_str();
      if (v) {
        char vLower[16];
        strncpy(vLower, v, sizeof(vLower) - 1);
        vLower[sizeof(vLower) - 1] = '\0';
        // Convertir en minuscules et trim
        for (char* p = vLower; *p; p++) {
          if (*p >= 'A' && *p <= 'Z') *p += 32;  // tolower
        }
        // Trim (supprimer espaces début/fin)
        char* start = vLower;
        while (*start == ' ' || *start == '\t') start++;
        char* end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';
        bool enable = (strcmp(start, "1") == 0 || 
                      strcmp(start, "true") == 0 || 
                      strcmp(start, "on") == 0);
        config.setRemoteRecvEnabled(enable); changed = true;
      }
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Access-Control-Allow-Origin", "*");
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["ok"] = true;
    doc["changed"] = changed;
    serializeJson(doc, *response);
    req->send(response);
  });

  // Endpoint de debug pour les logs en temps réel
    _server->on("/debug-logs", HTTP_GET, [this](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint debug
    
    IPAddress remoteIP2 = req->client()->remoteIP();
    Serial.printf("[Web] 🔍 Debug logs request from %d.%d.%d.%d\n",
                  remoteIP2[0], remoteIP2[1], remoteIP2[2], remoteIP2[3]);
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Informations système
    doc["system"]["uptime"] = millis();
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["heapSize"] = ESP.getHeapSize();
    doc["system"]["freePsram"] = ESP.getFreePsram();
    doc["system"]["psramSize"] = ESP.getPsramSize();
    
    // Informations WiFi
    doc["wifi"]["status"] = WiFi.status();
    char wifiSSIDBuf2[33];
    strncpy(wifiSSIDBuf2, WiFi.SSID().c_str(), sizeof(wifiSSIDBuf2) - 1);
    wifiSSIDBuf2[sizeof(wifiSSIDBuf2) - 1] = '\0';
    doc["wifi"]["ssid"] = wifiSSIDBuf2;
    IPAddress wifiIP2 = WiFi.localIP();
    char wifiIPBuf2[16];
    snprintf(wifiIPBuf2, sizeof(wifiIPBuf2), "%d.%d.%d.%d",
             wifiIP2[0], wifiIP2[1], wifiIP2[2], wifiIP2[3]);
    doc["wifi"]["ip"] = wifiIPBuf2;
    doc["wifi"]["rssi"] = WiFi.RSSI();
    char wifiMacBuf[18];
    strncpy(wifiMacBuf, WiFi.macAddress().c_str(), sizeof(wifiMacBuf) - 1);
    wifiMacBuf[sizeof(wifiMacBuf) - 1] = '\0';
    doc["wifi"]["mac"] = wifiMacBuf;
    
    // Informations WebSocket
    doc["websocket"]["connectedClients"] = g_realtimeWebSocket.getConnectedClients();
    doc["websocket"]["isActive"] = g_realtimeWebSocket.isRunning();
    
    // Informations automatisme
    doc["automatism"]["forceWakeup"] = g_autoCtrl.getForceWakeUp();
    doc["automatism"]["mailNotif"] = g_autoCtrl.isEmailEnabled();
    doc["automatism"]["mail"] = g_autoCtrl.getEmailAddress();
    
    // Informations capteurs (via cache)
    SensorReadings readings = sensorCache.getReadings(_sensors);
    doc["sensors"]["tempWater"] = readings.tempWater;
    doc["sensors"]["tempAir"] = readings.tempAir;
    doc["sensors"]["humidity"] = readings.humidity;
    doc["sensors"]["wlAqua"] = readings.wlAqua;
    doc["sensors"]["wlTank"] = readings.wlTank;
    doc["sensors"]["wlPota"] = readings.wlPota;
    doc["sensors"]["luminosite"] = readings.luminosite;
    
    // Informations actionneurs
    doc["actuators"]["pumpAqua"] = _acts.isAquaPumpRunning();
    doc["actuators"]["pumpTank"] = _acts.isTankPumpRunning();
    doc["actuators"]["heater"] = _acts.isHeaterOn();
    doc["actuators"]["light"] = _acts.isLightOn();
    
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Access-Control-Allow-Origin", "*");
    serializeJson(doc, *response);
    req->send(response);
  });

  _server->begin();
  Serial.println(F("[Web] AsyncWebServer démarré sur le port 80"));
  Serial.printf("[Web] Timeout serveur: %u ms\n", NetworkConfig::WEB_SERVER_TIMEOUT_MS);
  Serial.printf("[Web] Connexions max: %u\n", NetworkConfig::WEB_SERVER_MAX_CONNECTIONS);
  Serial.println(F("[Web] Serveur HTTP prêt - Interface web accessible"));
  Serial.println(F("[Web] WebSocket temps réel sur le port 81"));
  
  // Snapshot après démarrage serveur web
  HeapSnapshot snapshot_after_web_server = MEM_DIAG_SNAPSHOT("after_web_server");
  MEM_DIAG_COMPARE(snapshot_after_websocket, snapshot_after_web_server);
  return true;
  #endif
}

void WebServerManager::loop() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  // Traiter les boucles du serveur WebSocket temps réel
  g_realtimeWebSocket.loop();
  
  // Diffuser les données capteurs aux clients WebSocket connectés
  g_realtimeWebSocket.broadcastSensorData();
  #endif
} 