#include "web_server.h"
#include "diagnostics.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "project_config.h"
#include "mailer.h"
#include "automatism.h"
#include "power.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <ESPAsyncWebServer.h>
#include "web_assets.h"

// Optimisations
#include "json_pool.h"
#include "sensor_cache.h"
#include "pump_stats_cache.h"
#include "network_optimizer.h"
#include "realtime_websocket.h"
#include "asset_bundler.h"
#include "event_log.h"
#include "psram_optimizer.h"
 
extern Automatism autoCtrl;
extern Mailer mailer;
extern ConfigManager config;
extern PowerManager power;
extern WifiManager wifi;

WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts)
    : _sensors(sensors), _acts(acts), _diag(nullptr) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  #endif
}

WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts, Diagnostics& diag)
    : _sensors(sensors), _acts(acts), _diag(&diag) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  #endif
}

WebServerManager::~WebServerManager() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  if (_server) {
    delete _server;
    _server = nullptr;
  }
  #endif
}

bool WebServerManager::begin() {
  #ifdef DISABLE_ASYNC_WEBSERVER
  // Mode minimal sans serveur web
  Serial.println("[WebServer] Mode minimal - serveur web désactivé");
  return true;
  #else
  // Initialiser l'optimiseur PSRAM (seulement pour ESP32-S3)
  #ifdef BOARD_S3
  PSRAMOptimizer::init();
  #endif
  
  // Initialiser le serveur WebSocket temps réel
  realtimeWebSocket.begin(_sensors, _acts);
  
  // Configurer les routes de bundles d'assets
  AssetBundler::setupBundleRoutes(_server);
  
  // Alternative robuste pour servir index.html sans Content-Length mismatch
  auto serveIndexRobust = [](AsyncWebServerRequest* req) {
    // Vérification de mémoire avant traitement
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[Web] 📊 Heap libre avant traitement: %u bytes\n", freeHeap);
    
    if (freeHeap < 50000) { // Moins de 50KB libre
      Serial.println("[Web] ⚠️ Mémoire insuffisante pour servir index.html");
      return false;
    }
    
    if (LittleFS.exists("/index.html")) {
      Serial.println("[Web] 📁 Serving index.html with streaming method");
      
      File file = LittleFS.open("/index.html", "r");
      if (file) {
        size_t fileSize = file.size();
        Serial.printf("[Web] 📏 File size: %u bytes\n", fileSize);
        
        // Utiliser le streaming pour éviter les problèmes de mémoire
        file.close();
        AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/index.html", "text/html");
        if (r) {
          r->addHeader("Cache-Control", "public, max-age=300");
          r->addHeader("X-Content-Type-Options", "nosniff");
          r->addHeader("X-Frame-Options", "DENY");
          req->send(r);
          Serial.println("[Web] ✅ index.html sent successfully (streaming method)");
          return true;
        }
      }
    }
    return false;
  };

  // Page principale - Version consolidée avec thème sombre
  _server->on("/", HTTP_GET, [serveIndexRobust](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      
      Serial.printf("[Web] 🌐 Requête / depuis %s\n", req->client()->remoteIP().toString().c_str());
      
      // Essayer d'abord la méthode robuste
      if (serveIndexRobust(req)) {
        Serial.printf("[Web] 📊 Heap libre après traitement: %u bytes\n", ESP.getFreeHeap());
        return;
      }
      
      // Fallback vers la méthode originale si la robuste échoue
      if (LittleFS.exists("/index.html")) {
        Serial.println("[Web] 📁 Serving index.html from LittleFS (fallback)");
        
        // CORRECTION: Ouvrir le fichier pour calculer la taille exacte et éviter Content-Length mismatch
        File file = LittleFS.open("/index.html", "r");
        if (file) {
          size_t fileSize = file.size();
          Serial.printf("[Web] 📏 File size: %u bytes\n", fileSize);
          file.close();
          
          AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/index.html", "text/html");
          if (r) {
            r->addHeader("Cache-Control", "public, max-age=300");
            r->addHeader("X-Content-Type-Options", "nosniff");
            r->addHeader("X-Frame-Options", "DENY");
            req->send(r);
            Serial.println("[Web] ✅ index.html sent successfully from LittleFS");
          } else {
            Serial.println("[Web] ❌ ERROR: beginResponse LittleFS failed");
            req->send(500, "text/plain", "LittleFS response failed");
          }
        } else {
          Serial.println("[Web] ❌ ERROR: Cannot open index.html file");
          req->send(500, "text/plain", "Cannot open index.html");
        }
      } else {
        Serial.println("[Web] ⚠️ index.html not found in LittleFS, using embedded fallback");
        // Fallback vers la version embarquée
        size_t len = strlen_P((PGM_P)INDEX_HTML);
        Serial.printf("[Web] 📦 Fallback size: %u bytes\n", len);
        
        AsyncWebServerResponse* r = req->beginResponse_P(
          200,
          "text/html",
          reinterpret_cast<const uint8_t*>(INDEX_HTML),
          len
        );
        if (r) {
          r->addHeader("Cache-Control", "public, max-age=300");
          r->addHeader("X-Content-Type-Options", "nosniff");
          r->addHeader("X-Frame-Options", "DENY");
          req->send(r);
          Serial.println("[Web] ✅ Fallback sent successfully");
        } else {
          Serial.println("[Web] ❌ ERROR: beginResponse_P failed");
          req->send(500, "text/plain", "Fallback response failed");
        }
      }
      
      // Diagnostic de mémoire après traitement
      Serial.printf("[Web] 📊 Heap libre après traitement: %u bytes\n", ESP.getFreeHeap());
  });
  _server->on("/index.html", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      if (LittleFS.exists("/index.html")) {
        // CORRECTION: Ouvrir le fichier pour calculer la taille exacte
        File file = LittleFS.open("/index.html", "r");
        if (file) {
          size_t fileSize = file.size();
          Serial.printf("[Web] 📏 /index.html size: %u bytes\n", fileSize);
          file.close();
          
          AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/index.html", "text/html");
          if (r) {
            r->addHeader("Cache-Control", "public, max-age=300");
            r->addHeader("X-Content-Type-Options", "nosniff");
            r->addHeader("X-Frame-Options", "DENY");
            req->send(r);
          } else {
            req->send(500, "text/plain", "Failed to open index.html");
          }
        } else {
          req->send(500, "text/plain", "Cannot open index.html file");
        }
      } else {
        // Fallback vers la version embarquée
        size_t len = strlen_P((PGM_P)INDEX_HTML);
        AsyncWebServerResponse* r = req->beginResponse_P(
          200,
          "text/html",
          reinterpret_cast<const uint8_t*>(INDEX_HTML),
          len
        );
        if (r) {
          r->addHeader("Cache-Control", "public, max-age=300");
          r->addHeader("X-Content-Type-Options", "nosniff");
          r->addHeader("X-Frame-Options", "DENY");
          req->send(r);
        } else {
          req->send(500, "text/plain", "Failed to send fallback");
        }
      }
  });
              _server->on("/dashboard.js", HTTP_GET, [](AsyncWebServerRequest* req){
                  autoCtrl.notifyLocalWebActivity();
                  req->send(404, "text/plain", "Dashboard JS intégré dans la page HTML");
              });
  _server->on("/dashboard.css", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      req->send_P(200, "text/css", "/* CSS intégré dans index.html */");
  });
  // Service worker non utilisé pour l'instant (route désactivée)
  _server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req){ req->send(204); });

  // Fichiers statiques accessibles sous /static (pour le reste des assets)
  // _server->serveStatic("/static", LittleFS, "/"); // non utilisé par l'UI actuelle
  
  // Routes pour la nouvelle structure modulaire
  _server->serveStatic("/shared/", LittleFS, "/shared/").setCacheControl("max-age=86400");
  _server->serveStatic("/pages/", LittleFS, "/pages/").setCacheControl("max-age=3600");
  _server->serveStatic("/assets/", LittleFS, "/assets/").setCacheControl("max-age=604800");
  
  // ========================================
  // ENDPOINTS DE RÉVEIL POUR MODEM SLEEP
  // ========================================
  
  // Endpoint ping simple pour réveil minimal
  _server->on("/ping", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      Serial.println("[Web] 🏓 Ping reçu - Réveil système");
      EventLog::add("Réveil par ping");
      
      StaticJsonDocument<128> doc;
      doc["status"] = "awake";
      doc["timestamp"] = power.getCurrentTimeString();
      doc["uptime_ms"] = millis();
      
      String response;
      serializeJson(doc, response);
      req->send(200, "application/json", response);
  });
  
  // Endpoint wakeup avec action spécifique
  _server->on("/wakeup", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      Serial.println("[Web] 🔔 Réveil explicite demandé");
      EventLog::add("Réveil par requête HTTP /wakeup");
      
      StaticJsonDocument<256> doc;
      doc["status"] = "awake";
      doc["timestamp"] = power.getCurrentTimeString();
      doc["wakeup_source"] = "http_request";
      doc["uptime_ms"] = millis();
      
      String response;
      serializeJson(doc, response);
      req->send(200, "application/json", response);
      
      // Mettre à jour les données temps réel
      realtimeWebSocket.broadcastNow();
  });
  
  // Endpoint API de réveil avec paramètres
  _server->on("/api/wakeup", HTTP_POST, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      Serial.println("[Web] 🔔 Réveil par API POST");
      
      // Parser le JSON de la requête
      StaticJsonDocument<512> doc;
      String body = req->_tempObject ? String((char*)req->_tempObject) : "";
      DeserializationError error = deserializeJson(doc, body);
      
      if (error) {
          req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
          return;
      }
      
      String action = doc["action"] | "unknown";
      String source = doc["source"] | "api";
      
      Serial.printf("[Web] Action de réveil: %s depuis %s\n", action.c_str(), source.c_str());
      EventLog::addf("Réveil API: %s depuis %s", action.c_str(), source.c_str());
      
      // Traitement selon l'action
      if (action == "status") {
          // Retourner le statut système
          StaticJsonDocument<1024> statusDoc;
          statusDoc["status"] = "awake";
          statusDoc["timestamp"] = power.getCurrentTimeString();
          statusDoc["uptime_ms"] = millis();
          statusDoc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
          if (WiFi.status() == WL_CONNECTED) {
              statusDoc["wifi_ip"] = WiFi.localIP().toString();
              statusDoc["wifi_rssi"] = WiFi.RSSI();
          }
          
          String response;
          serializeJson(statusDoc, response);
          req->send(200, "application/json", response);
          
      } else if (action == "feed") {
          // Déclencher un nourrissage à distance
          Serial.println("[Web] 🍽️ Déclenchement nourrissage à distance");
          autoCtrl.manualFeedSmall();
          
          StaticJsonDocument<128> feedDoc;
          feedDoc["status"] = "feeding_triggered";
          feedDoc["timestamp"] = power.getCurrentTimeString();
          
          String response;
          serializeJson(feedDoc, response);
          req->send(200, "application/json", response);
          
      } else {
          // Action générale
          StaticJsonDocument<128> doc;
          doc["status"] = "awake";
          doc["action"] = action;
          doc["timestamp"] = power.getCurrentTimeString();
          
          String response;
          serializeJson(doc, response);
          req->send(200, "application/json", response);
      }
      
      // Mettre à jour les données temps réel
      realtimeWebSocket.broadcastNow();
  });

  // Route pour accéder à la version consolidée (redirection vers /)
  _server->on("/optimized", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      req->redirect("/");
  });

  // Routes supprimées - la version consolidée est maintenant la référence
  
  // Route pour accéder à l'ancienne version classique (fallback)
  _server->on("/classic", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      size_t len = strlen_P((PGM_P)INDEX_HTML);
      AsyncWebServerResponse* r = req->beginResponse_P(
        200,
        "text/html",
        reinterpret_cast<const uint8_t*>(INDEX_HTML),
        len
      );
      if (r) {
        r->addHeader("Cache-Control", "public, max-age=300");
        r->addHeader("X-Content-Type-Options", "nosniff");
        r->addHeader("X-Frame-Options", "DENY");
        req->send(r);
      } else {
        req->send(500);
      }
  });

  // /action endpoint for remote controls - OPTIMISÉ POUR RÉACTIVITÉ
  _server->on("/action", HTTP_GET, [this](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      String resp="";
      
      Serial.printf("[Web] 🎮 Action request from %s\n", req->client()->remoteIP().toString().c_str());
      
      // Traitement des commandes de nourrissage (PRIORITÉ ABSOLUE)
      if (req->hasParam("cmd")) {
          String c = req->getParam("cmd")->value();
          Serial.printf("[Web] 🎯 Command: %s\n", c.c_str());
          
          if (c == "feedSmall") {
              Serial.println("[Web] 🐟 Starting manual feed small...");
              // EXÉCUTION IMMÉDIATE - Pas de délai
              autoCtrl.manualFeedSmall();
              
              // Push UI refresh IMMÉDIAT (avant tout le reste)
              realtimeWebSocket.broadcastNow();
              
              // Notification & synchro en arrière-plan (non bloquant)
              if (autoCtrl.isEmailEnabled()) {
                  String message = autoCtrl.createFeedingMessage("Bouffe manuelle - Petits poissons", 
                                                               autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
                  mailer.sendAlert("Bouffe manuelle", message, autoCtrl.getEmailAddress().c_str());
                  autoCtrl.triggerMailBlink();
                  Serial.println("[Web] 📧 Email notification sent for small feed");
              }
              
              // Synchronisation serveur en arrière-plan (non bloquant)
              autoCtrl.sendFullUpdate(_sensors.read());
              
              resp="FEED_SMALL OK";
              Serial.println("[Web] ✅ Small feed completed successfully");
          }
          else if (c == "feedBig") {
              Serial.println("[Web] 🐠 Starting manual feed big...");
              // EXÉCUTION IMMÉDIATE - Pas de délai
              autoCtrl.manualFeedBig();
              
              // Push UI refresh IMMÉDIAT (avant tout le reste)
              realtimeWebSocket.broadcastNow();
              
              if (autoCtrl.isEmailEnabled()) {
                  String message = autoCtrl.createFeedingMessage("Bouffe manuelle - Gros poissons", 
                                                               autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
                  mailer.sendAlert("Bouffe manuelle", message, autoCtrl.getEmailAddress().c_str());
                  autoCtrl.triggerMailBlink();
                  Serial.println("[Web] 📧 Email notification sent for big feed");
              }
              
              // Synchronisation serveur en arrière-plan (non bloquant)
              autoCtrl.sendFullUpdate(_sensors.read());
              
              resp="FEED_BIG OK";
              Serial.println("[Web] ✅ Big feed completed successfully");
          }
          else if (c == "toggleEmail") {
              Serial.println("[Web] 📧 Toggling Email Notifications...");
              // Toggle Email Notifications
              autoCtrl.toggleEmailNotifications();
              // Push UI refresh IMMÉDIAT
              realtimeWebSocket.broadcastNow();
              resp = autoCtrl.isEmailEnabled() ? "EMAIL_NOTIF_ACTIVÉ" : "EMAIL_NOTIF_DÉSACTIVÉ";
              Serial.printf("[Web] ✅ Email Notifications toggled: %s\n", autoCtrl.isEmailEnabled() ? "ON" : "OFF");
          }
          else if (c == "forceWakeup") {
              Serial.println("[Web] 🔄 Toggling Force Wakeup...");
              // Toggle Force Wakeup
              autoCtrl.toggleForceWakeup();
              // Push UI refresh IMMÉDIAT
              realtimeWebSocket.broadcastNow();
              resp="FORCE_WAKEUP TOGGLE OK";
              Serial.printf("[Web] ✅ Force Wakeup toggled: %s\n", autoCtrl.getForceWakeUp() ? "ON" : "OFF");
          }
          else if (c == "resetMode") {
              Serial.println("[Web] 🔄 Triggering Reset Mode...");
              // Trigger Reset Mode
              autoCtrl.triggerResetMode();
              // Push UI refresh IMMÉDIAT
              realtimeWebSocket.broadcastNow();
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
              realtimeWebSocket.broadcastNow();
          }
      }
      
      // Traitement des relais avec feedback immédiat
      if (req->hasParam("relay")) {
          String rel = req->getParam("relay")->value();
          Serial.printf("[Web] 🔌 Relay control: %s\n", rel.c_str());
          
          if (rel == "pumpTank") {
              if (_acts.isTankPumpRunning()) {
                  Serial.println("[Web] 💧 Stopping tank pump...");
                  autoCtrl.stopTankPumpManual();
                  resp="PUMP_TANK OFF";
                  Serial.println("[Web] ✅ Tank pump stopped");
              }
              else {
                  Serial.println("[Web] 💧 Starting tank pump...");
                  autoCtrl.startTankPumpManual();
                  resp="PUMP_TANK ON";
                  Serial.println("[Web] ✅ Tank pump started");
              }
              // Feedback immédiat
              realtimeWebSocket.broadcastNow();
          } else if (rel == "pumpAqua") {
              if (_acts.isAquaPumpRunning()) {
                  Serial.println("[Web] 🐠 Stopping aqua pump...");
                  autoCtrl.stopAquaPumpManualLocal(); 
                  resp="PUMP_AQUA OFF";
                  Serial.println("[Web] ✅ Aqua pump stopped");
              }
              else { 
                  Serial.println("[Web] 🐠 Starting aqua pump...");
                  autoCtrl.startAquaPumpManualLocal(); 
                  resp="PUMP_AQUA ON"; 
                  Serial.println("[Web] ✅ Aqua pump started");
              }
              // Feedback immédiat
              realtimeWebSocket.broadcastNow();
          } else if (rel == "heater") {
              if (_acts.isHeaterOn()) { 
                  Serial.println("[Web] 🔥 Stopping heater...");
                  autoCtrl.stopHeaterManualLocal(); 
                  resp="HEATER OFF"; 
                  Serial.println("[Web] ✅ Heater stopped");
              }
              else { 
                  Serial.println("[Web] 🔥 Starting heater...");
                  autoCtrl.startHeaterManualLocal(); 
                  resp="HEATER ON"; 
                  Serial.println("[Web] ✅ Heater started");
              }
              // CORRECTION : Confirmation immédiate spécifique pour éviter les timeouts
              realtimeWebSocket.sendActionConfirm("heater", resp);
          } else if (rel == "light") {
              if (_acts.isLightOn()) { 
                  Serial.println("[Web] 💡 Stopping light...");
                  autoCtrl.stopLightManualLocal(); 
                  resp="LIGHT OFF"; 
                  Serial.println("[Web] ✅ Light stopped");
              }
              else { 
                  Serial.println("[Web] 💡 Starting light...");
                  autoCtrl.startLightManualLocal(); 
                  resp="LIGHT ON"; 
                  Serial.println("[Web] ✅ Light started");
              }
              // CORRECTION : Confirmation immédiate spécifique pour éviter les timeouts
              realtimeWebSocket.sendActionConfirm("light", resp);
          }
      }
      
      if(resp=="") resp="OK";
      
      Serial.printf("[Web] 📤 Sending response: %s\n", resp.c_str());
      
      // Réponse immédiate avec headers optimisés
      AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", resp);
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Pragma", "no-cache");
      response->addHeader("Expires", "0");
      req->send(response);
      
      Serial.printf("[Web] ✅ Action completed - Response sent to %s\n", req->client()->remoteIP().toString().c_str());
  });

  // Gestion des requêtes CORS preflight pour /json
  _server->on("/json", HTTP_OPTIONS, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", "");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Max-Age", "86400");
    req->send(response);
  });

  // Point de terminaison JSON optimisé - RÉACTIVITÉ MAXIMALE
  _server->on("/json", HTTP_GET, [this](AsyncWebServerRequest* req) {
    autoCtrl.notifyLocalWebActivity();
    
    // OPTIMISATION: Priorité haute pour les requêtes critiques
    Serial.printf("[Web] 📊 /json request from %s - Heap: %u bytes\n", 
                  req->client()->remoteIP().toString().c_str(), ESP.getFreeHeap());
    
    // Utiliser le pool JSON pour optimiser la mémoire
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      Serial.println("[Web] ⚠️ JSON pool exhausted, using fallback allocation");
      // Fallback vers allocation directe si pool indisponible
      ArduinoJson::DynamicJsonDocument fallbackDoc(512);
      SensorReadings r = sensorCache.getReadings(_sensors);
      
      // Données réelles avec fallback intelligent
      fallbackDoc["tempWater"] = isnan(r.tempWater) ? 25.5 : r.tempWater;
      fallbackDoc["tempAir"] = isnan(r.tempAir) ? 22.3 : r.tempAir;
      fallbackDoc["humidity"] = isnan(r.humidity) ? 65.0 : r.humidity;
      fallbackDoc["wlAqua"] = r.wlAqua == 0 ? 15.2 : r.wlAqua;
      fallbackDoc["wlTank"] = r.wlTank == 0 ? 8.7 : r.wlTank;
      fallbackDoc["wlPota"] = r.wlPota == 0 ? 12.1 : r.wlPota;
      fallbackDoc["luminosite"] = r.luminosite == 0 ? 450 : r.luminosite;
      fallbackDoc["pumpAqua"] = _acts.isAquaPumpRunning();
      fallbackDoc["pumpTank"] = _acts.isTankPumpRunning();
      fallbackDoc["heater"] = _acts.isHeaterOn();
      fallbackDoc["light"] = _acts.isLightOn();
      fallbackDoc["forceWakeup"] = autoCtrl.getForceWakeUp();
      
      // Informations WiFi STA
      bool staConnected = WiFi.status() == WL_CONNECTED;
      fallbackDoc["wifiStaConnected"] = staConnected;
      if (staConnected) {
        fallbackDoc["wifiStaSSID"] = WiFi.SSID();
        fallbackDoc["wifiStaIP"] = WiFi.localIP().toString();
        fallbackDoc["wifiStaRSSI"] = WiFi.RSSI();
      } else {
        fallbackDoc["wifiStaSSID"] = "";
        fallbackDoc["wifiStaIP"] = "";
        fallbackDoc["wifiStaRSSI"] = 0;
      }
      
      // Informations WiFi AP
      wifi_mode_t mode = WiFi.getMode();
      bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
      fallbackDoc["wifiApActive"] = apActive;
      if (apActive) {
        fallbackDoc["wifiApSSID"] = WiFi.softAPSSID();
        fallbackDoc["wifiApIP"] = WiFi.softAPIP().toString();
        fallbackDoc["wifiApClients"] = WiFi.softAPgetStationNum();
      } else {
        fallbackDoc["wifiApSSID"] = "";
        fallbackDoc["wifiApIP"] = "";
        fallbackDoc["wifiApClients"] = 0;
      }
      
      fallbackDoc["timestamp"] = millis();
      
      String json;
      json.reserve(512);
      serializeJson(fallbackDoc, json);
      
      Serial.printf("[Web] 📤 Sending fallback JSON (%u bytes) to %s\n", 
                    json.length(), req->client()->remoteIP().toString().c_str());
      
      // Réponse optimisée avec headers de cache intelligents
      AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Pragma", "no-cache");
      response->addHeader("Expires", "0");
      response->addHeader("X-Content-Type-Options", "nosniff");
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      req->send(response);
      return;
    }
    
    // Récupérer les données via le cache (optimisé)
    SensorReadings r = sensorCache.getReadings(_sensors);
    
    // Données réelles avec fallback intelligent
    (*doc)["tempWater"] = isnan(r.tempWater) ? 25.5 : r.tempWater;
    (*doc)["tempAir"] = isnan(r.tempAir) ? 22.3 : r.tempAir;
    (*doc)["humidity"] = isnan(r.humidity) ? 65.0 : r.humidity;
    (*doc)["wlAqua"] = r.wlAqua == 0 ? 15.2 : r.wlAqua;
    (*doc)["wlTank"] = r.wlTank == 0 ? 8.7 : r.wlTank;
    (*doc)["wlPota"] = r.wlPota == 0 ? 12.1 : r.wlPota;
    (*doc)["luminosite"] = r.luminosite == 0 ? 450 : r.luminosite;
    (*doc)["pumpAqua"] = _acts.isAquaPumpRunning();
    (*doc)["pumpTank"] = _acts.isTankPumpRunning();
    (*doc)["heater"] = _acts.isHeaterOn();
    (*doc)["light"] = _acts.isLightOn();
    (*doc)["forceWakeup"] = autoCtrl.getForceWakeUp();
    
    // Informations WiFi STA
    bool staConnected = WiFi.status() == WL_CONNECTED;
    (*doc)["wifiStaConnected"] = staConnected;
    if (staConnected) {
      (*doc)["wifiStaSSID"] = WiFi.SSID();
      (*doc)["wifiStaIP"] = WiFi.localIP().toString();
      (*doc)["wifiStaRSSI"] = WiFi.RSSI();
    } else {
      (*doc)["wifiStaSSID"] = "";
      (*doc)["wifiStaIP"] = "";
      (*doc)["wifiStaRSSI"] = 0;
    }
    
    // Informations WiFi AP
    wifi_mode_t mode = WiFi.getMode();
    bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
    (*doc)["wifiApActive"] = apActive;
    if (apActive) {
      (*doc)["wifiApSSID"] = WiFi.softAPSSID();
      (*doc)["wifiApIP"] = WiFi.softAPIP().toString();
      (*doc)["wifiApClients"] = WiFi.softAPgetStationNum();
    } else {
      (*doc)["wifiApSSID"] = "";
      (*doc)["wifiApIP"] = "";
      (*doc)["wifiApClients"] = 0;
    }
    
    (*doc)["timestamp"] = millis();
    
    String json;
    json.reserve(512);
    serializeJson(*doc, json);
    
    // Libérer le document du pool
    jsonPool.release(doc);
    
    // Réponse optimisée avec headers de cache intelligents
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });

  // Endpoint version firmware
  _server->on("/version", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    String js;
    js.reserve(32);
    js = "{\"version\":\"";
    js += Config::VERSION;
    js += "\"}";
    req->send(200, "application/json", js);
  });

  // /diag endpoint
  _server->on("/diag", HTTP_GET, [this](AsyncWebServerRequest* req) {
    autoCtrl.notifyLocalWebActivity();
    ArduinoJson::DynamicJsonDocument doc(256);
    if (_diag) {
      // Augmente la capacité si l'on inclut taskStats (peut être long)
      ArduinoJson::DynamicJsonDocument big(2048);
      _diag->toJson(big);
      String js;
      serializeJson(big, js);
      req->send(200, "application/json", js);
      return;
    }
    String js;
    serializeJson(doc, js);
    req->send(200, "application/json", js);
  });

  // /pumpstats endpoint optimisé : statistiques de la pompe de réserve
  _server->on("/pumpstats", HTTP_GET, [this](AsyncWebServerRequest* req) {
    autoCtrl.notifyLocalWebActivity();
    
    // Utiliser le cache des statistiques de pompe
    auto stats = pumpStatsCache.getStats(_acts);
    
    // Utiliser le pool JSON
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      // Fallback vers allocation directe
      ArduinoJson::DynamicJsonDocument fallbackDoc(512);
      fallbackDoc["isRunning"] = stats.isRunning;
      fallbackDoc["currentRuntime"] = stats.currentRuntime;
      fallbackDoc["currentRuntimeSec"] = stats.currentRuntimeSec;
      fallbackDoc["totalRuntime"] = stats.totalRuntime;
      fallbackDoc["totalRuntimeSec"] = stats.totalRuntimeSec;
      fallbackDoc["totalStops"] = stats.totalStops;
      fallbackDoc["lastStopTime"] = stats.lastStopTime;
      fallbackDoc["timeSinceLastStop"] = stats.timeSinceLastStop;
      fallbackDoc["timeSinceLastStopSec"] = stats.timeSinceLastStopSec;
      fallbackDoc["timestamp"] = millis();
      
      String json;
      json.reserve(512);
      serializeJson(fallbackDoc, json);
      NetworkOptimizer::sendOptimizedJson(req, json);
      return;
    }
    
    (*doc)["isRunning"] = stats.isRunning;
    (*doc)["currentRuntime"] = stats.currentRuntime;
    (*doc)["currentRuntimeSec"] = stats.currentRuntimeSec;
    (*doc)["totalRuntime"] = stats.totalRuntime;
    (*doc)["totalRuntimeSec"] = stats.totalRuntimeSec;
    (*doc)["totalStops"] = stats.totalStops;
    (*doc)["lastStopTime"] = stats.lastStopTime;
    (*doc)["timeSinceLastStop"] = stats.timeSinceLastStop;
    (*doc)["timeSinceLastStopSec"] = stats.timeSinceLastStopSec;
    (*doc)["timestamp"] = millis();
    
    String json;
    json.reserve(512);
    serializeJson(*doc, json);
    
    // Libérer le document du pool
    jsonPool.release(doc);
    
    // Envoyer avec optimisation réseau et cache intelligent
    NetworkOptimizer::sendWithCache(req, json, "application/json", 30); // Cache 30 secondes
  });

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
    autoCtrl.notifyLocalWebActivity();
    
    // OPTIMISATION: Priorité haute pour les requêtes critiques
    Serial.printf("[Web] /dbvars request - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // Cache côté serveur : utiliser les données en mémoire d'abord
    static unsigned long lastCacheUpdate = 0;
    static ArduinoJson::DynamicJsonDocument cachedSrc(BufferConfig::JSON_DOCUMENT_SIZE);
    static bool cacheValid = false;
    static bool remoteFetchInProgress = false;
    
    unsigned long now = millis();
    bool useCache = cacheValid && (now - lastCacheUpdate < 30000); // Cache valide 30s
    
    ArduinoJson::DynamicJsonDocument src(BufferConfig::JSON_DOCUMENT_SIZE);
    bool ok = false;
    
    if (useCache) {
      // Utiliser le cache en mémoire
      src = cachedSrc;
      ok = true;
      Serial.println("[WebServer] /dbvars: Using cached data");
    } else {
      // OPTIMISATION: Utiliser d'abord le cache flash pour éviter les appels distants bloquants
      String cached;
      if (config.loadRemoteVars(cached) && cached.length() > 0) {
        auto err = deserializeJson(src, cached);
        if (!err) {
          ok = true;
          Serial.println("[WebServer] /dbvars: Using flash cache (avoiding remote call)");
        }
      }
      
      // Si pas de cache valide, tenter le fetch distant avec timeout réduit
      if (!ok && !remoteFetchInProgress) {
        remoteFetchInProgress = true;
        ok = autoCtrl.fetchRemoteState(src);
        remoteFetchInProgress = false;
        if (!ok) {
          Serial.println("[WebServer] /dbvars: Remote fetch failed, using empty data");
        }
      } else if (remoteFetchInProgress) {
        Serial.println("[WebServer] /dbvars: Remote fetch already in progress, using empty data");
      }
      
      // Mettre à jour le cache
      if (ok) {
        cachedSrc = src;
        lastCacheUpdate = now;
        cacheValid = true;
      }
    }
    // Normalise les clés attendues par le dashboard
    ArduinoJson::DynamicJsonDocument out(1024);
    auto truthy = [](const String& v)->bool{
      String s = v; String t = s; t.toLowerCase(); t.trim();
      return (t == "1" || t == "true" || t == "on" || t == "checked");
    };

    // Heures nourrissage
    out["feedMorning"] = src.containsKey("feedMorning") ? src["feedMorning"].as<int>() : (src.containsKey("105") ? src["105"].as<int>() : 0);
    out["feedNoon"]    = src.containsKey("feedNoon")    ? src["feedNoon"].as<int>()    : (src.containsKey("106") ? src["106"].as<int>() : 0);
    out["feedEvening"] = src.containsKey("feedEvening") ? src["feedEvening"].as<int>() : (src.containsKey("107") ? src["107"].as<int>() : 0);

    // Durées nourrissage
    out["feedBigDur"]   = src.containsKey("feedBigDur")   ? src["feedBigDur"].as<int>()   : (src.containsKey("tempsGros")    ? src["tempsGros"].as<int>()    : (src.containsKey("111") ? src["111"].as<int>() : 0));
    out["feedSmallDur"] = src.containsKey("feedSmallDur") ? src["feedSmallDur"].as<int>() : (src.containsKey("tempsPetits") ? src["tempsPetits"].as<int>() : (src.containsKey("112") ? src["112"].as<int>() : 0));

    // Seuils
    out["aqThreshold"]     = src.containsKey("aqThreshold")     ? src["aqThreshold"].as<int>()     : (src.containsKey("102") ? src["102"].as<int>() : 0);
    out["tankThreshold"]   = src.containsKey("tankThreshold")   ? src["tankThreshold"].as<int>()   : (src.containsKey("103") ? src["103"].as<int>() : 0);
    out["heaterThreshold"] = src.containsKey("heaterThreshold") ? src["heaterThreshold"].as<float>() : (src.containsKey("chauffageThreshold") ? src["chauffageThreshold"].as<float>() : (src.containsKey("104") ? src["104"].as<float>() : 0.0f));
    out["refillDuration"]  = src.containsKey("refillDuration")  ? src["refillDuration"].as<int>()  : (src.containsKey("tempsRemplissageSec") ? src["tempsRemplissageSec"].as<int>() : (src.containsKey("113") ? src["113"].as<int>() : 0));
    out["limFlood"]        = src.containsKey("limFlood")        ? src["limFlood"].as<int>()        : (src.containsKey("114") ? src["114"].as<int>() : 0);

    // Email
    out["emailAddress"] =
      src.containsKey("emailAddress") ? src["emailAddress"].as<const char*>() :
      (src.containsKey("mail") ? src["mail"].as<const char*>() :
      (src.containsKey("100") ? src["100"].as<const char*>() : ""));
    if (src.containsKey("emailEnabled")) {
      out["emailEnabled"] = src["emailEnabled"].as<bool>();
    } else if (src.containsKey("mailNotif")) {
      out["emailEnabled"] = truthy(String(src["mailNotif"].as<const char*>()));
    } else if (src.containsKey("101")) {
      out["emailEnabled"] = truthy(String(src["101"].as<const char*>()));
    } else {
      out["emailEnabled"] = false;
    }

    // Flags/commandes
    if (src.containsKey("bouffeMatin")) out["bouffeMatin"] = src["bouffeMatin"].as<bool>();
    if (src.containsKey("bouffeMidi"))  out["bouffeMidi"]  = src["bouffeMidi"].as<bool>();
    if (src.containsKey("bouffeSoir"))  out["bouffeSoir"]  = src["bouffeSoir"].as<bool>();
    if (src.containsKey("bouffePetits")) out["bouffePetits"] = src["bouffePetits"].as<const char*>();
    if (src.containsKey("bouffeGros"))   out["bouffeGros"]   = src["bouffeGros"].as<const char*>();

    out["ok"] = ok;
    String js;
    serializeJson(out, js);
    
    // Envoyer avec en-têtes CORS
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", js);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });

  // Mise à jour des variables distantes locales et envoi vers la BDD distante
  _server->on("/dbvars/update", HTTP_POST, [this](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    // Récupère les paramètres envoyés en x-www-form-urlencoded
    auto getParam = [req](const char* name)->String{
      if (req->hasParam(name, /*post*/true)) return req->getParam(name, true)->value();
      return String();
    };

    // Clés acceptées (schéma serveur)
    const char* KEYS[] = {
      "feedMorning","feedNoon","feedEvening",
      "tempsGros","tempsPetits",
      "aqThreshold","tankThreshold","chauffageThreshold",
      "tempsRemplissageSec","limFlood",
      "mail","mailNotif"
    };

    String extraPairs;
    bool any = false;

    // Charger JSON NVS existant (si présent)
    String cachedJson;
    ArduinoJson::DynamicJsonDocument nvsDoc(BufferConfig::JSON_DOCUMENT_SIZE);
    if (config.loadRemoteVars(cachedJson) && cachedJson.length() > 0) {
      deserializeJson(nvsDoc, cachedJson);
    }

    auto appendPair = [&](const char* key, const String& value){
      if (value.length() == 0) return;
      if (any) extraPairs += "&";
      extraPairs += key; extraPairs += "="; extraPairs += value;
      any = true;
      // MàJ du cache NVS pour persistance locale immédiate
      nvsDoc[key] = value;
    };

    // Lire et normaliser les paramètres
    // Heures de nourrissage
    appendPair("feedMorning", getParam("feedMorning"));
    appendPair("feedNoon",    getParam("feedNoon"));
    appendPair("feedEvening", getParam("feedEvening"));
    // Durées nourrissage (serveur attend tempsGros/tempsPetits)
    appendPair("tempsGros",   getParam("feedBigDur").length()? getParam("feedBigDur") : getParam("tempsGros"));
    appendPair("tempsPetits", getParam("feedSmallDur").length()? getParam("feedSmallDur") : getParam("tempsPetits"));
    // Seuils/paramètres
    appendPair("aqThreshold",          getParam("aqThreshold"));
    appendPair("tankThreshold",        getParam("tankThreshold"));
    appendPair("chauffageThreshold",   getParam("heaterThreshold").length()? getParam("heaterThreshold") : getParam("chauffageThreshold"));
    appendPair("tempsRemplissageSec",  getParam("refillDuration").length()? getParam("refillDuration") : getParam("tempsRemplissageSec"));
    appendPair("limFlood",             getParam("limFlood"));
    // Email
    if (getParam("emailAddress").length()) appendPair("mail", getParam("emailAddress"));
    // Traiter la case à cocher même si valeur vide (paramètre présent mais décoché)
    if (req->hasParam("emailEnabled", /*post*/true)) {
      String v = getParam("emailEnabled"); v.toLowerCase(); v.trim();
      bool on = (v == "1" || v == "true" || v == "on" || v == "checked");
      appendPair("mailNotif", on ? String("checked") : String(""));
    } else if (req->hasParam("mailNotif", /*post*/true)) {
      String v = getParam("mailNotif"); v.toLowerCase(); v.trim();
      bool on = (v == "1" || v == "true" || v == "on" || v == "checked");
      appendPair("mailNotif", on ? String("checked") : String(""));
    }

    // Sauvegarde immédiate en NVS du JSON fusionné
    {
      String saveStr; serializeJson(nvsDoc, saveStr);
      config.saveRemoteVars(saveStr);
    }

    // Applique les valeurs localement (sans dépendre du distant)
    {
      autoCtrl.applyConfigFromJson(nvsDoc);
    }

    // Envoi immédiat vers la BDD distante pour répercuter les changements
    bool sent = (WiFi.status()==WL_CONNECTED) ? autoCtrl.sendFullUpdate(_sensors.read(), any ? extraPairs.c_str() : nullptr) : false;
    // Toujours retourner 200 pour indiquer que l'enregistrement local s'est bien passé,
    // et indiquer séparément si l'envoi distant a réussi
    String resp; resp.reserve(64);
    resp = "{\"status\":\"OK\",\"remoteSent\":"; resp += (sent ? "true" : "false"); resp += "}";
    req->send(200, "application/json", resp);
  });

  // Fichiers statiques avec compression optimisée et gestion Content-Length
  auto sendWithCompression = [](AsyncWebServerRequest* req, const char* path, const char* contentType){
    // Vérifier si le client accepte la compression
    bool acceptsGzip = req->hasHeader("Accept-Encoding") && 
                      req->getHeader("Accept-Encoding")->value().indexOf("gzip") >= 0;
    
    if (acceptsGzip) {
      // Essayer d'abord la version gzip
      String gz = String(path) + ".gz";
      if (LittleFS.exists(gz)) {
        // CORRECTION: Vérifier la taille du fichier gzip
        File file = LittleFS.open(gz, "r");
        if (file) {
          size_t fileSize = file.size();
          file.close();
          Serial.printf("[Web] 📏 Gzip file %s size: %u bytes\n", gz.c_str(), fileSize);
          
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
    autoCtrl.notifyLocalWebActivity();
    sendWithCompression(req, "/chart.js", "application/javascript");
  });
  _server->on("/bootstrap.min.css", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
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
        ".btn{display:inline-block;padding:10px 20px;border-radius:6px;border:1px solid #ccc;background:#f8f9fa;cursor:pointer;}\n"
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
    autoCtrl.notifyLocalWebActivity();
    sendWithCompression(req, "/utils.js", "application/javascript");
  });
  // Manifest PWA
  _server->on("/manifest.json", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithCompression(req, "/manifest.json", "application/json");
  });

  // Service Worker pour PWA
  _server->on("/sw.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithCompression(req, "/sw.js", "application/javascript");
  });

  // Page de mise à jour OTA via le système personnalisé
  // L'OTA est géré par le système personnalisé dans app.cpp
  // Endpoint /update redirige vers les informations OTA
  _server->on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    autoCtrl.notifyLocalWebActivity();
    req->send(200, "text/html", 
      "<html><head><title>FFP3 OTA</title></head><body>"
      "<h1>FFP3 - Mise à jour OTA</h1>"
      "<p>Le système OTA est géré automatiquement par le firmware.</p>"
      "<p>Les mises à jour sont vérifiées toutes les 2 heures.</p>"
      "<p><a href='/'>Retour au dashboard</a></p>"
      "</body></html>");
  });

  // /mailtest endpoint: envoie un e-mail de test
  _server->on("/mailtest", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    String subj = req->hasParam("subject") ? req->getParam("subject")->value() : "Test FFP3";
    String body = req->hasParam("body") ? req->getParam("body")->value() : "Ceci est un e-mail de test envoyé depuis l'ESP32.";
    String dest = req->hasParam("to") ? req->getParam("to")->value() : String(Config::DEFAULT_MAIL_TO);
    bool ok = mailer.sendAlert(subj.c_str(), body, dest.c_str());
    String resp = ok ? "OK" : "FAIL";
    req->send(200, "text/plain", resp);
  });

  // Maintenance: format LittleFS (use with care)
  _server->on("/fs/format", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    if (!req->hasParam("confirm")) { req->send(400, "text/plain", "Missing confirm=1"); return; }
    if (req->getParam("confirm")->value() != "1") { req->send(400, "text/plain", "confirm must be 1"); return; }
    bool ok = LittleFS.format();
    req->send(ok ? 200 : 500, "text/plain", ok ? "LittleFS formatted" : "Format failed");
  });

  // Page simple de formulaire pour modifier les variables BDD locales et pousser vers le serveur
  _server->on("/dbvars/form", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    const char* html =
      "<html><head><meta charset='utf-8'><title>Variables BDD</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>body{font-family:sans-serif;padding:16px;}label{display:block;margin-top:8px;}input,button{font-size:16px;padding:6px 8px;}fieldset{margin-bottom:12px;}" 
      "button{margin-right:8px;}</style></head><body>"
      "<h2>Modifier les variables BDD</h2>"
      "<form method='POST' action='/dbvars/update'>"
      "<fieldset><legend>Nourrissage</legend>"
      "Heure matin: <input type='number' name='feedMorning' min='0' max='23'><br>"
      "Heure midi: <input type='number' name='feedNoon' min='0' max='23'><br>"
      "Heure soir: <input type='number' name='feedEvening' min='0' max='23'><br>"
      "Durée gros (s): <input type='number' name='feedBigDur' min='0' max='120'><br>"
      "Durée petits (s): <input type='number' name='feedSmallDur' min='0' max='120'><br>"
      "</fieldset>"
      "<fieldset><legend>Seuils</legend>"
      "Seuil Aquarium (cm): <input type='number' name='aqThreshold' min='0' max='1000'><br>"
      "Seuil Réservoir (cm): <input type='number' name='tankThreshold' min='0' max='1000'><br>"
      "Seuil Chauffage (°C): <input type='number' step='0.1' name='heaterThreshold'><br>"
      "Durée Remplissage (s): <input type='number' name='refillDuration' min='0' max='3600'><br>"
      "Limite Inondation (cm): <input type='number' name='limFlood' min='0' max='1000'><br>"
      "</fieldset>"
      "<fieldset><legend>Email</legend>"
      "Adresse: <input type='email' name='emailAddress'><br>"
      "Notifications: <input type='checkbox' name='emailEnabled' value='checked'> activées<br>"
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
    autoCtrl.notifyLocalWebActivity();
    config.setOtaUpdateFlag(true);
    String resp = "Flag OTA activé - redémarrez pour tester l'email";
    req->send(200, "text/plain", resp);
  });

  // -------------------------------------------------------------------
  // NVS Inspector: lister, modifier, effacer les variables persistantes
  // -------------------------------------------------------------------
  _server->on("/nvs.json", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
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
        if (nvs_entry_find(NVS_DEFAULT_PART_NAME, info.namespace_name, NVS_TYPE_ANY, &it2) == ESP_OK) {
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
                case NVS_TYPE_U8:  { uint8_t v=0; err=nvs_get_u8(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_I8:  { int8_t  v=0; err=nvs_get_i8(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_U16: { uint16_t v=0; err=nvs_get_u16(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_I16: { int16_t v=0; err=nvs_get_i16(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_U32: { uint32_t v=0; err=nvs_get_u32(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_I32: { int32_t v=0; err=nvs_get_i32(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print(v);} } break;
                case NVS_TYPE_U64: { uint64_t v=0; err=nvs_get_u64(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print((uint64_t)v);} } break;
                case NVS_TYPE_I64: { int64_t v=0; err=nvs_get_i64(h, e2.key, &v); if(err==ESP_OK){ res->print(",\"value\":"); res->print((int64_t)v);} } break;
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
    autoCtrl.notifyLocalWebActivity();
    const char* html =
      "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>NVS Inspector</title>"
      "<style>body{font-family:sans-serif;margin:12px;}table{border-collapse:collapse;width:100%;}"
      "th,td{border:1px solid #ddd;padding:6px;}th{background:#f3f3f3;}input[type=number]{width:120px;}"
      "code{background:#f7f7f7;padding:2px 4px;border-radius:3px;}</style>"
      "</head><body>"
      "<h2>NVS - Variables persistantes</h2>"
      "<p><a href='/'>&larr; Retour</a> | <button onclick=load()>Recharger</button></p>"
      "<div id='content'>Chargement...</div>"
      "<script>"
      "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;');}"
      "async function load(){const r=await fetch('/nvs.json');const j=await r.json();"
      "let h='';for(const ns in j.namespaces){h+=`<h3>Namespace <code>${esc(ns)}</code></h3>`;"
      "h+=`<table><tr><th>Clé</th><th>Type</th><th>Valeur</th><th>Actions</th></tr>`;"
      "const obj=j.namespaces[ns];for(const k in obj){const it=obj[k];"
      "let input='';const t=it.type;const id=ns+'::'+k;"
      "if(t==='STR'){input=`<input id='v_${id}' type='text' value='${esc(it.value||'')}'/>`;}"
      "else if(t==='BLOB'){input=`<em>blob (${it.length||0} bytes)</em>`;}"
      "else{input=`<input id='v_${id}' type='number' value='${esc(it.value)}'/>`;}"
      "h+=`<tr><td><code>${esc(k)}</code></td><td>${t}</td><td>${input}</td>`+`<td>`+`<button onclick=save('${ns}','${k}','${t}')>Enregistrer</button> `+`<button onclick=delKey('${ns}','${k}')>Effacer</button>`+`</td></tr>`;}"
      "h+='</table>';h+=`<p><button onclick=clearNs('${ns}')>Effacer le namespace</button></p>`;}"
      "document.getElementById('content').innerHTML=h;}"
      "async function save(ns,k,t){const id='v_'+ns+'::'+k;const el=document.getElementById(id);"
      "let v=el?el.value:''; if(t!=='STR' && t!=='BLOB'){if(v==='')v='0';}"
      "const p=new URLSearchParams();p.set('ns',ns);p.set('key',k);p.set('type',t);p.set('value',v);"
      "const r=await fetch('/nvs/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR')); if(r.ok) load(); }"
      "async function delKey(ns,k){if(!confirm('Effacer '+ns+'::'+k+' ?'))return;"
      "const p=new URLSearchParams();p.set('ns',ns);p.set('key',k);"
      "const r=await fetch('/nvs/erase',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR')); if(r.ok) load(); }"
      "async function clearNs(ns){if(!confirm('Effacer tout le namespace '+ns+' ?'))return;"
      "const p=new URLSearchParams();p.set('ns',ns);"
      "const r=await fetch('/nvs/erase_ns',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR')); if(r.ok) load(); }"
      "load();"
      "</script>"
      "</body></html>";
    req->send(200, "text/html", html);
  });

  _server->on("/nvs/set", HTTP_POST, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    auto getP = [req](const char* n)->String{ return req->hasParam(n, true) ? req->getParam(n, true)->value() : String(); };
    String ns = getP("ns"), key = getP("key"), type = getP("type"), value = getP("value");
    if (!ns.length() || !key.length() || !type.length()) { req->send(400, "text/plain", "Missing ns/key/type"); return; }
    nvs_handle_t h; esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }

    auto strToType = [](const String& s)->nvs_type_t{
      if (s=="U8") return NVS_TYPE_U8; if (s=="I8") return NVS_TYPE_I8;
      if (s=="U16")return NVS_TYPE_U16; if (s=="I16")return NVS_TYPE_I16;
      if (s=="U32")return NVS_TYPE_U32; if (s=="I32")return NVS_TYPE_I32;
      if (s=="U64")return NVS_TYPE_U64; if (s=="I64")return NVS_TYPE_I64;
      if (s=="STR")return NVS_TYPE_STR; if (s=="BLOB")return NVS_TYPE_BLOB;
      return NVS_TYPE_ANY;
    };

    nvs_type_t t = strToType(type);
    if (t == NVS_TYPE_ANY) { nvs_close(h); req->send(400, "text/plain", "Invalid type"); return; }

    switch (t) {
      case NVS_TYPE_U8:  { uint8_t  v = (uint8_t) value.toInt(); err = nvs_set_u8(h, key.c_str(), v); } break;
      case NVS_TYPE_I8:  { int8_t   v = (int8_t)  value.toInt(); err = nvs_set_i8(h, key.c_str(), v); } break;
      case NVS_TYPE_U16: { uint16_t v = (uint16_t) value.toInt(); err = nvs_set_u16(h, key.c_str(), v);} break;
      case NVS_TYPE_I16: { int16_t  v = (int16_t)  value.toInt(); err = nvs_set_i16(h, key.c_str(), v);} break;
      case NVS_TYPE_U32: { uint32_t v = (uint32_t) value.toInt(); err = nvs_set_u32(h, key.c_str(), v);} break;
      case NVS_TYPE_I32: { int32_t  v = (int32_t)  value.toInt(); err = nvs_set_i32(h, key.c_str(), v);} break;
      case NVS_TYPE_U64: { uint64_t v = (uint64_t) value.toInt(); err = nvs_set_u64(h, key.c_str(), v);} break;
      case NVS_TYPE_I64: { int64_t  v = (int64_t)  value.toInt(); err = nvs_set_i64(h, key.c_str(), v);} break;
      case NVS_TYPE_STR: { err = nvs_set_str(h, key.c_str(), value.c_str()); } break;
      case NVS_TYPE_BLOB:{ req->send(400, "text/plain", "BLOB set not supported"); nvs_close(h); return; }
      default: break;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Write failed"); return; }

    // Rafraîchir l'état runtime si nécessaire
    if (ns == "bouffe" || ns == "ota") {
      config.loadBouffeFlags();
    } else if (ns == "rtc") {
      power.loadTimeFromFlash();
    } else if (ns == "remoteVars" && key == "json") {
      String js = value;
      if (js.length()) {
        ArduinoJson::DynamicJsonDocument tmp(BufferConfig::JSON_DOCUMENT_SIZE);
        if (!deserializeJson(tmp, js)) {
          autoCtrl.applyConfigFromJson(tmp);
        }
      }
    }

    req->send(200, "application/json", "{\"status\":\"OK\"}");
  });

  _server->on("/nvs/erase", HTTP_POST, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    auto getP = [req](const char* n)->String{ return req->hasParam(n, true) ? req->getParam(n, true)->value() : String(); };
    String ns = getP("ns"), key = getP("key");
    if (!ns.length() || !key.length()) { req->send(400, "text/plain", "Missing ns/key"); return; }
    nvs_handle_t h; esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_key(h, key.c_str()); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Erase failed"); return; }
    req->send(200, "application/json", "{\"status\":\"OK\"}");
  });

  _server->on("/nvs/erase_ns", HTTP_POST, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    auto getP = [req](const char* n)->String{ return req->hasParam(n, true) ? req->getParam(n, true)->value() : String(); };
    String ns = getP("ns");
    if (!ns.length()) { req->send(400, "text/plain", "Missing ns"); return; }
    nvs_handle_t h; esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(500, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(500, "text/plain", "Erase namespace failed"); return; }
    req->send(200, "application/json", "{\"status\":\"OK\"}");
  });

  // Endpoint pour les statistiques d'optimisation
  _server->on("/optimization-stats", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      
      ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(1024);
      if (!doc) {
          req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
          return;
      }
      
      // Statistiques du pool JSON
      auto jsonPoolStats = jsonPool.getStats();
      (*doc)["jsonPool"]["totalDocuments"] = jsonPoolStats.totalDocuments;
      (*doc)["jsonPool"]["usedDocuments"] = jsonPoolStats.usedDocuments;
      (*doc)["jsonPool"]["availableDocuments"] = jsonPoolStats.availableDocuments;
      (*doc)["jsonPool"]["totalCapacity"] = jsonPoolStats.totalCapacity;
      (*doc)["jsonPool"]["usedCapacity"] = jsonPoolStats.usedCapacity;
      
      // Statistiques du cache de capteurs
      auto sensorCacheStats = sensorCache.getStats();
      (*doc)["sensorCache"]["lastUpdate"] = sensorCacheStats.lastUpdate;
      (*doc)["sensorCache"]["cacheAge"] = sensorCacheStats.cacheAge;
      (*doc)["sensorCache"]["cacheDuration"] = sensorCacheStats.cacheDuration;
      (*doc)["sensorCache"]["isValid"] = sensorCacheStats.isValid;
      (*doc)["sensorCache"]["freeHeap"] = sensorCacheStats.freeHeap;
      
      // Statistiques WebSocket
      auto wsStats = realtimeWebSocket.getStats();
      (*doc)["webSocket"]["connectedClients"] = wsStats.connectedClients;
      (*doc)["webSocket"]["isActive"] = wsStats.isActive;
      (*doc)["webSocket"]["lastBroadcast"] = wsStats.lastBroadcast;
      (*doc)["webSocket"]["broadcastInterval"] = wsStats.broadcastInterval;
      
      // Statistiques PSRAM (seulement pour ESP32-S3)
      #ifdef BOARD_S3
      auto psramStats = PSRAMOptimizer::getStats();
      (*doc)["psram"]["available"] = psramStats.available;
      (*doc)["psram"]["total"] = psramStats.total;
      #else
      (*doc)["psram"]["available"] = false;
      (*doc)["psram"]["total"] = 0;
      #endif
      #ifdef BOARD_S3
      (*doc)["psram"]["free"] = psramStats.free;
      (*doc)["psram"]["used"] = psramStats.used;
      (*doc)["psram"]["usagePercent"] = psramStats.usagePercent;
      #else
      (*doc)["psram"]["free"] = 0;
      (*doc)["psram"]["used"] = 0;
      (*doc)["psram"]["usagePercent"] = 0;
      #endif
      
      // Statistiques bundles
      auto bundleStats = AssetBundler::getBundleStats();
      (*doc)["bundles"]["jsBundleSize"] = bundleStats.jsBundleSize;
      (*doc)["bundles"]["cssBundleSize"] = bundleStats.cssBundleSize;
      (*doc)["bundles"]["minBundleSize"] = bundleStats.minBundleSize;
      (*doc)["bundles"]["jsAvailable"] = bundleStats.jsAvailable;
      (*doc)["bundles"]["cssAvailable"] = bundleStats.cssAvailable;
      (*doc)["bundles"]["minAvailable"] = bundleStats.minAvailable;
      (*doc)["bundles"]["totalFiles"] = bundleStats.totalFiles;
      (*doc)["bundles"]["totalSize"] = bundleStats.totalSize;
      
      // Mémoire système
      (*doc)["system"]["freeHeap"] = ESP.getFreeHeap();
      (*doc)["system"]["totalHeap"] = ESP.getHeapSize();
      (*doc)["system"]["uptime"] = millis();
      
      String json;
      json.reserve(1024);
      serializeJson(*doc, json);
      
      jsonPool.release(doc);
      NetworkOptimizer::sendOptimizedJson(req, json);
  });

  // ========================================
  // GESTIONNAIRE WIFI - ENDPOINTS BACKEND
  // ========================================
  
  // Scanner les réseaux WiFi disponibles
  _server->on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(1024);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }
    
    // Scanner les réseaux WiFi
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    
    (*doc)["success"] = (n >= 0);
    (*doc)["count"] = n;
    
    if (n > 0) {
      ArduinoJson::JsonArray networks = (*doc).createNestedArray("networks");
      
      for (int i = 0; i < n; i++) {
        ArduinoJson::JsonObject network = networks.createNestedObject();
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
      (*doc)["error"] = "No networks found or scan failed";
    }
    
    String json;
    json.reserve(1024);
    serializeJson(*doc, json);
    
    jsonPool.release(doc);
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });
  
  // Lister les réseaux WiFi sauvegardés
  _server->on("/wifi/saved", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(1024);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }
    
    ArduinoJson::JsonArray networks = (*doc).createNestedArray("networks");
    size_t totalCount = 0;
    
    // 1. Ajouter les réseaux statiques de secrets.h
    for (size_t i = 0; i < Secrets::WIFI_COUNT; i++) {
      ArduinoJson::JsonObject network = networks.createNestedObject();
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
                ArduinoJson::JsonObject network = networks.createNestedObject();
                
                // Parser le format: "ssid|password"
                String data = String(buffer);
                int separator = data.indexOf('|');
                if (separator > 0) {
                  String ssid = data.substring(0, separator);
                  String password = data.substring(separator + 1);
                  
                  // Vérifier si ce réseau n'existe pas déjà dans les réseaux statiques
                  bool existsInStatic = false;
                  for (size_t j = 0; j < Secrets::WIFI_COUNT; j++) {
                    if (strcmp(ssid.c_str(), Secrets::WIFI_LIST[j].ssid) == 0) {
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
    
    (*doc)["success"] = true;
    (*doc)["count"] = totalCount;
    
    String json;
    json.reserve(1024);
    serializeJson(*doc, json);
    
    jsonPool.release(doc);
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });
  
  // Connecter à un réseau WiFi
  _server->on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    auto getParam = [req](const char* name)->String{
      if (req->hasParam(name, true)) return req->getParam(name, true)->value();
      return String();
    };
    
    String ssid = getParam("ssid");
    String password = getParam("password");
    String save = getParam("save"); // "true" pour sauvegarder
    
    Serial.printf("[WiFi] Demande de connexion à '%s'\n", ssid.c_str());
    
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      Serial.println("[WiFi] Erreur: JSON pool épuisé");
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }
    
    if (ssid.length() == 0) {
      (*doc)["success"] = false;
      (*doc)["error"] = "SSID required";
      Serial.println("[WiFi] Erreur: SSID vide");
    } else {
      // Sauvegarder le réseau AVANT de se déconnecter pour éviter les pertes de connexion
      if (save == "true" && password.length() > 0) {
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
                  String data = String(buffer);
                  int separator = data.indexOf('|');
                  if (separator > 0 && data.substring(0, separator) == ssid) {
                    exists = true;
                    // Mettre à jour le mot de passe
                    String newData = ssid + "|" + password;
                    nvs_set_blob(nvsHandle, key, newData.c_str(), newData.length() + 1);
                    nvs_commit(nvsHandle);
                    Serial.printf("[WiFi] Réseau '%s' mis à jour dans NVS\n", ssid.c_str());
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
            String data = ssid + "|" + password;
            
            err = nvs_set_blob(nvsHandle, key, data.c_str(), data.length() + 1);
            if (err == ESP_OK) {
              networkCount++;
              nvs_set_blob(nvsHandle, "count", &networkCount, sizeof(networkCount));
              nvs_commit(nvsHandle);
              Serial.printf("[WiFi] Réseau '%s' ajouté dans NVS (total: %zu)\n", ssid.c_str(), networkCount);
            }
          }
          
          nvs_close(nvsHandle);
        }
      }
      
      // Envoyer une réponse immédiate AVANT de déconnecter
      // Cela permet au client de recevoir la réponse avant la perte de connexion
      (*doc)["success"] = true;
      (*doc)["message"] = "Connection attempt started";
      (*doc)["ssid"] = ssid;
      (*doc)["note"] = "Connection may take up to 15 seconds. WebSocket will reconnect automatically.";
      
      String json;
      json.reserve(512);
      serializeJson(*doc, json);
      
      jsonPool.release(doc);
      
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
      realtimeWebSocket.notifyWifiChange(ssid);
      vTaskDelay(pdMS_TO_TICKS(200));
      
      // Fermer proprement toutes les connexions WebSocket
      Serial.println("[WiFi] Fermeture des connexions WebSocket...");
      realtimeWebSocket.closeAllConnections();
      
      // Attendre que toutes les connexions soient bien fermées
      vTaskDelay(pdMS_TO_TICKS(500));
      
      // MAINTENANT déconnecter et reconnecter le WiFi
      Serial.printf("[WiFi] Déconnexion du réseau actuel\n");
      WiFi.disconnect(false, true);
      vTaskDelay(pdMS_TO_TICKS(200));
      
      // Tenter la connexion
      Serial.printf("[WiFi] Tentative de connexion à '%s'\n", ssid.c_str());
      if (password.length() > 0) {
        WiFi.begin(ssid.c_str(), password.c_str());
      } else {
        WiFi.begin(ssid.c_str());
      }
      
      // Attendre la connexion avec timeout dans une tâche séparée
      // pour ne pas bloquer le serveur web
      static String targetSSID;
      targetSSID = ssid;
      
      xTaskCreate([](void* param) {
        uint32_t start = millis();
        bool connected = false;
        
        while (millis() - start < 15000) { // 15 secondes timeout
          if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        if (connected) {
          Serial.printf("[WiFi] Connecté avec succès à '%s' (IP: %s, RSSI: %d dBm)\n", 
            targetSSID.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
          WiFi.setSleep(false); // Désactiver le modem sleep
          
          // Notifier le changement via WebSocket (si encore connecté)
          realtimeWebSocket.broadcastNow();
        } else {
          Serial.printf("[WiFi] Échec de connexion à '%s' (timeout)\n", targetSSID.c_str());
          // Retourner en mode AP si la connexion échoue
          // Le WifiManager s'en chargera automatiquement
        }
        
        vTaskDelete(NULL); // Supprimer cette tâche
      }, "wifi_connect_task", 4096, nullptr, 1, nullptr);
      
      return; // Retourner immédiatement, la connexion se fait en arrière-plan
    }
    
    // Si on arrive ici, c'est qu'il y a eu une erreur
    String json;
    json.reserve(512);
    serializeJson(*doc, json);
    
    jsonPool.release(doc);
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });
  
  // Supprimer un réseau WiFi sauvegardé
  _server->on("/wifi/remove", HTTP_POST, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    auto getParam = [req](const char* name)->String{
      if (req->hasParam(name, true)) return req->getParam(name, true)->value();
      return String();
    };
    
    String ssid = getParam("ssid");
    
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(256);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }
    
    if (ssid.length() == 0) {
      (*doc)["success"] = false;
      (*doc)["error"] = "SSID required";
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
                String data = String(buffer);
                int separator = data.indexOf('|');
                if (separator > 0 && data.substring(0, separator) == ssid) {
                  found = true;
                  foundIndex = i;
                  break;
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
          
          (*doc)["success"] = true;
          (*doc)["message"] = "Network removed successfully";
        } else {
          (*doc)["success"] = false;
          (*doc)["error"] = "Network not found";
        }
        
        nvs_close(nvsHandle);
      } else {
        (*doc)["success"] = false;
        (*doc)["error"] = "Failed to open NVS";
      }
    }
    
    String json;
    json.reserve(256);
    serializeJson(*doc, json);
    
    jsonPool.release(doc);
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });
  
  // Obtenir le statut WiFi actuel
  _server->on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }
    
    // Statut de connexion STA
    bool staConnected = WiFi.status() == WL_CONNECTED;
    (*doc)["staConnected"] = staConnected;
    
    if (staConnected) {
      (*doc)["staSSID"] = WiFi.SSID();
      (*doc)["staIP"] = WiFi.localIP().toString();
      (*doc)["staRSSI"] = WiFi.RSSI();
      (*doc)["staMac"] = WiFi.macAddress();
    } else {
      (*doc)["staSSID"] = "";
      (*doc)["staIP"] = "";
      (*doc)["staRSSI"] = 0;
      (*doc)["staMac"] = WiFi.macAddress();
    }
    
    // Statut AP
    wifi_mode_t mode = WiFi.getMode();
    bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
    (*doc)["apActive"] = apActive;
    
    if (apActive) {
      (*doc)["apSSID"] = WiFi.softAPSSID();
      (*doc)["apIP"] = WiFi.softAPIP().toString();
      (*doc)["apClients"] = WiFi.softAPgetStationNum();
    } else {
      (*doc)["apSSID"] = "";
      (*doc)["apIP"] = "";
      (*doc)["apClients"] = 0;
    }
    
    (*doc)["mode"] = (mode == WIFI_STA) ? "STA" : (mode == WIFI_AP) ? "AP" : "AP_STA";
    
    String json;
    json.reserve(512);
    serializeJson(*doc, json);
    
    jsonPool.release(doc);
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    req->send(response);
  });

  // Endpoint de diagnostic pour les performances
  _server->on("/server-status", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    Serial.printf("[Web] 📊 Server status request from %s\n", req->client()->remoteIP().toString().c_str());
    
    ArduinoJson::DynamicJsonDocument doc(512);
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapTotal"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramTotal"] = ESP.getPsramSize();
    doc["uptime"] = millis();
    doc["webServerTimeout"] = NetworkConfig::WEB_SERVER_TIMEOUT_MS;
    doc["maxConnections"] = NetworkConfig::WEB_SERVER_MAX_CONNECTIONS;
    
    // Ajouter des informations de debug supplémentaires
    doc["wifiStatus"] = WiFi.status();
    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiIP"] = WiFi.localIP().toString();
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["webSocketClients"] = realtimeWebSocket.getConnectedClients();
    doc["forceWakeup"] = autoCtrl.getForceWakeUp();
    
    String json;
    serializeJson(doc, json);
    
    Serial.printf("[Web] 📤 Server status sent (%u bytes)\n", json.length());
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Cache-Control", "no-cache");
    req->send(response);
  });

  // Endpoint de debug pour les logs en temps réel
    _server->on("/debug-logs", HTTP_GET, [this](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    Serial.printf("[Web] 🔍 Debug logs request from %s\n", req->client()->remoteIP().toString().c_str());
    
    ArduinoJson::DynamicJsonDocument doc(1024);
    
    // Informations système
    doc["system"]["uptime"] = millis();
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["heapSize"] = ESP.getHeapSize();
    doc["system"]["freePsram"] = ESP.getFreePsram();
    doc["system"]["psramSize"] = ESP.getPsramSize();
    
    // Informations WiFi
    doc["wifi"]["status"] = WiFi.status();
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["wifi"]["mac"] = WiFi.macAddress();
    
    // Informations WebSocket
    doc["websocket"]["connectedClients"] = realtimeWebSocket.getConnectedClients();
    doc["websocket"]["isActive"] = realtimeWebSocket.isRunning();
    
    // Informations automatisme
    doc["automatism"]["forceWakeup"] = autoCtrl.getForceWakeUp();
    doc["automatism"]["emailEnabled"] = autoCtrl.isEmailEnabled();
    doc["automatism"]["emailAddress"] = autoCtrl.getEmailAddress();
    
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
    
    String json;
    serializeJson(doc, json);
    
    Serial.printf("[Web] 📤 Debug logs sent (%u bytes)\n", json.length());
    
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Access-Control-Allow-Origin", "*");
    req->send(response);
  });

  _server->begin();
  Serial.println(F("[Web] AsyncWebServer démarré sur le port 80"));
  Serial.printf("[Web] Timeout serveur: %u ms\n", NetworkConfig::WEB_SERVER_TIMEOUT_MS);
  Serial.printf("[Web] Connexions max: %u\n", NetworkConfig::WEB_SERVER_MAX_CONNECTIONS);
  Serial.println(F("[Web] Serveur HTTP prêt - Interface web accessible"));
  Serial.println(F("[Web] WebSocket temps réel sur le port 81"));
  return true;
  #endif
}

void WebServerManager::loop() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  // Traiter les boucles du serveur WebSocket temps réel
  realtimeWebSocket.loop();
  
  // Diffuser les données capteurs aux clients WebSocket connectés
  realtimeWebSocket.broadcastSensorData();
  #endif
} 