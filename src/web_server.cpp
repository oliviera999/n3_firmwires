#include "web_server.h"
#include "diagnostics.h"
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "mailer.h"
#include "automatism.h"
#include "nvs_manager.h"
#include "nvs_keys.h"
#include "power.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include "esp_wifi.h"  // Pour esp_wifi_scan_get_ap_records (éviter String Arduino)
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#endif
#include "web_routes_status.h"
#include "web_routes_ui.h"
#include "app_context.h"
#include "realtime_websocket.h"
#include "asset_bundler.h"

 
extern Automatism g_autoCtrl;
extern Mailer mailer;
extern ConfigManager config;
extern PowerManager power;
extern WifiManager wifi;

static bool s_dbvarsCacheInvalid = false;
void invalidateDbvarsCache() { s_dbvarsCacheInvalid = true; }

/// Flag pour redémarrage ESP après envoi de la réponse (évite reset avant envoi HTTP)
static bool s_pendingRestart = false;

#ifndef DISABLE_ASYNC_WEBSERVER
static bool getWebParam(AsyncWebServerRequest* req, const char* name, char* buf, size_t bufSize, bool post = true) {
  if (req->hasParam(name, post)) {
    const AsyncWebParameter* p = req->getParam(name, post);
    if (p) {
      Utils::safeStrncpy(buf, p->value().c_str(), bufSize);
      return true;
    }
  }
  buf[0] = '\0';
  return false;
}

static const char* nvsTypeToStr(nvs_type_t t) {
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
    case NVS_TYPE_BLOB: return "BLOB";
    default: return "UNKNOWN";
  }
}

static nvs_type_t nvsStrToType(const char* s) {
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
}

static void printNvsEntryToJson(nvs_handle_t h, const nvs_entry_info_t& e2, Print* res) {
  res->print("\"type\":\"");
  res->print(nvsTypeToStr(e2.type));
  res->print("\"");
  esp_err_t err = ESP_ERR_INVALID_ARG;
  switch (e2.type) {
    case NVS_TYPE_U8: {
      uint8_t v = 0;
      err = nvs_get_u8(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I8: {
      int8_t v = 0;
      err = nvs_get_i8(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U16: {
      uint16_t v = 0;
      err = nvs_get_u16(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I16: {
      int16_t v = 0;
      err = nvs_get_i16(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U32: {
      uint32_t v = 0;
      err = nvs_get_u32(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I32: {
      int32_t v = 0;
      err = nvs_get_i32(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U64: {
      uint64_t v = 0;
      err = nvs_get_u64(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print((uint64_t)v); }
    } break;
    case NVS_TYPE_I64: {
      int64_t v = 0;
      err = nvs_get_i64(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print((int64_t)v); }
    } break;
    case NVS_TYPE_STR: {
      size_t len = 0;
      err = nvs_get_str(h, e2.key, nullptr, &len);
      if (err == ESP_OK) {
        if (len > 0 && len < BufferConfig::JSON_DOCUMENT_SIZE) {
          static char nvsStrBuf[BufferConfig::JSON_DOCUMENT_SIZE];
          size_t bufLen = sizeof(nvsStrBuf);
          if (nvs_get_str(h, e2.key, nvsStrBuf, &bufLen) == ESP_OK) {
            res->print(",\"value\":\"");
            for (size_t i = 0; i < bufLen && nvsStrBuf[i]; ++i) {
              char c = nvsStrBuf[i];
              if (c == '"' || c == '\\') { res->print('\\'); res->print(c); }
              else if (c == '\n') { res->print("\\n"); }
              else if (c == '\r') { res->print("\\r"); }
              else { res->print(c); }
            }
            res->print("\"");
          } else {
            res->print(",\"value\":\"<err>\"");
          }
        } else if (len >= BufferConfig::JSON_DOCUMENT_SIZE) {
          res->print(",\"value\":\"<too_long>\"");
        } else {
          res->print(",\"value\":\"\"");
        }
      }
    } break;
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      err = nvs_get_blob(h, e2.key, nullptr, &len);
      if (err == ESP_OK) {
        res->print(",\"length\":");
        res->print((uint32_t)len);
        res->print(",\"value\":\"<blob>\"");
      }
    } break;
    default: break;
  }
}

static unsigned long s_dbvarsLastCacheUpdate = 0;
static StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> s_dbvarsCachedSrc;
static bool s_dbvarsCacheValid = false;

static void fillDbVarsJson(JsonObject& out) {
  if (s_dbvarsCacheInvalid) {
    s_dbvarsCacheValid = false;
    s_dbvarsCacheInvalid = false;
  }
  unsigned long now = millis();
  bool useCache = s_dbvarsCacheValid && (now - s_dbvarsLastCacheUpdate < 30000);
  bool ok = false;
  if (useCache) {
    ok = true;
    Serial.println("[WebServer] /dbvars: Using cached data");
  } else {
    char cached[2048];
    if (config.loadRemoteVars(cached, sizeof(cached)) && strlen(cached) > 0) {
      auto err = deserializeJson(s_dbvarsCachedSrc, cached);
      if (!err) {
        ok = true;
        Serial.println("[WebServer] /dbvars: Using flash cache (fast path)");
        s_dbvarsLastCacheUpdate = now;
        s_dbvarsCacheValid = true;
      } else {
        Serial.println("[WebServer] /dbvars: Flash cache parse error");
      }
    } else {
      Serial.println("[WebServer] /dbvars: No flash cache available, using defaults");
    }
  }
  auto getStringWithDefault = [](const char* key, const char* defaultVal) -> const char* {
    if (s_dbvarsCachedSrc.containsKey(key)) {
      const char* val = s_dbvarsCachedSrc[key].as<const char*>();
      return (val && strlen(val) > 0) ? val : defaultVal;
    }
    return defaultVal;
  };
  auto getIntCanonical = [](const char* canonical, const char* legacy, int def) -> int {
    if (s_dbvarsCachedSrc.containsKey(canonical)) return s_dbvarsCachedSrc[canonical].as<int>();
    if (legacy && s_dbvarsCachedSrc.containsKey(legacy)) return s_dbvarsCachedSrc[legacy].as<int>();
    return def;
  };
  auto getFloatCanonical = [](const char* canonical, const char* legacy, float def) -> float {
    if (s_dbvarsCachedSrc.containsKey(canonical)) return s_dbvarsCachedSrc[canonical].as<float>();
    if (legacy && s_dbvarsCachedSrc.containsKey(legacy)) return s_dbvarsCachedSrc[legacy].as<float>();
    return def;
  };

  out["bouffeMatin"] = ok && s_dbvarsCachedSrc.containsKey("bouffeMatin")
                       ? s_dbvarsCachedSrc["bouffeMatin"].as<int>()
                       : (int)g_autoCtrl.getBouffeMatin();
  out["bouffeMidi"]  = ok && s_dbvarsCachedSrc.containsKey("bouffeMidi")
                       ? s_dbvarsCachedSrc["bouffeMidi"].as<int>()
                       : (int)g_autoCtrl.getBouffeMidi();
  out["bouffeSoir"]  = ok && s_dbvarsCachedSrc.containsKey("bouffeSoir")
                       ? s_dbvarsCachedSrc["bouffeSoir"].as<int>()
                       : (int)g_autoCtrl.getBouffeSoir();
  out["tempsGros"] = ok && s_dbvarsCachedSrc.containsKey("tempsGros")
                     ? s_dbvarsCachedSrc["tempsGros"].as<int>()
                     : (int)g_autoCtrl.getTempsGros();
  out["tempsPetits"] = ok && s_dbvarsCachedSrc.containsKey("tempsPetits")
                       ? s_dbvarsCachedSrc["tempsPetits"].as<int>()
                       : (int)g_autoCtrl.getTempsPetits();
  out["aqThreshold"] = ok && s_dbvarsCachedSrc.containsKey("aqThreshold")
                       ? s_dbvarsCachedSrc["aqThreshold"].as<int>()
                       : (int)g_autoCtrl.getAqThresholdCm();
  out["tankThreshold"] = ok && s_dbvarsCachedSrc.containsKey("tankThreshold")
                         ? s_dbvarsCachedSrc["tankThreshold"].as<int>()
                         : (int)g_autoCtrl.getTankThresholdCm();
  out["chauffageThreshold"] = ok && (s_dbvarsCachedSrc.containsKey("chauffageThreshold") || s_dbvarsCachedSrc.containsKey("heaterThreshold"))
                              ? getFloatCanonical("chauffageThreshold", "heaterThreshold", g_autoCtrl.getHeaterThresholdC())
                              : g_autoCtrl.getHeaterThresholdC();
  out["tempsRemplissageSec"] = ok && (s_dbvarsCachedSrc.containsKey("tempsRemplissageSec") || s_dbvarsCachedSrc.containsKey("refillDuration"))
                               ? getIntCanonical("tempsRemplissageSec", "refillDuration", (int)g_autoCtrl.getRefillDurationSec())
                               : (int)g_autoCtrl.getRefillDurationSec();
  out["limFlood"] = ok && s_dbvarsCachedSrc.containsKey("limFlood")
                    ? s_dbvarsCachedSrc["limFlood"].as<int>()
                    : (int)g_autoCtrl.getLimFlood();
  out["FreqWakeUp"] = ok && s_dbvarsCachedSrc.containsKey("FreqWakeUp")
                     ? s_dbvarsCachedSrc["FreqWakeUp"].as<int>()
                     : (int)g_autoCtrl.getFreqWakeSec();

  const char* localEmail = g_autoCtrl.getEmailAddress();
  const char* emailAddr = (ok && s_dbvarsCachedSrc.containsKey("mail"))
                          ? getStringWithDefault("mail", localEmail && strlen(localEmail) > 0 ? localEmail : "Non configuré")
                          : (localEmail && strlen(localEmail) > 0 ? localEmail : "Non configuré");
  out["mail"] = emailAddr;

  if (ok && s_dbvarsCachedSrc.containsKey("mailNotif")) {
    const char* val = s_dbvarsCachedSrc["mailNotif"].as<const char*>();
    bool enabled = val && (strcmp(val, "checked") == 0 || strcmp(val, "1") == 0 ||
                          strcmp(val, "true") == 0 || strcmp(val, "on") == 0);
    out["mailNotif"] = enabled;
  } else {
    out["mailNotif"] = g_autoCtrl.isEmailEnabled();
  }
  out["bouffeMatinOk"] = config.getBouffeMatinOk();
  out["bouffeMidiOk"] = config.getBouffeMidiOk();
  out["bouffeSoirOk"] = config.getBouffeSoirOk();
  const char* petitsFlag = g_autoCtrl.getBouffePetitsFlag();
  out["bouffePetits"] = (petitsFlag && strlen(petitsFlag) > 0) ? atoi(petitsFlag) : 0;
  const char* grosFlag = g_autoCtrl.getBouffeGrosFlag();
  out["bouffeGros"] = (grosFlag && strlen(grosFlag) > 0) ? atoi(grosFlag) : 0;
  out["ok"] = true;
}
#endif

// Helper pour envoyer un email lors d'une action manuelle
static void sendManualActionEmail(const char* subject, const char* actionType, const char* eventCode) {
  if (!g_autoCtrl.isEmailEnabled()) {
    return;
  }
  const char* emailAddr = g_autoCtrl.getEmailAddress();
  if (emailAddr && strlen(emailAddr) > 0) {
    char timeStr[24] = "(heure N/A)";
    time_t now = time(nullptr);
    if (now > 100000) {
      struct tm tmInfo;
      if (localtime_r(&now, &tmInfo)) {
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", &tmInfo);
      }
    }
    char message[256];
    snprintf(message, sizeof(message),
             "Action manuelle: %s\nÉvénement: %s\nHeure: %s",
             actionType, eventCode, timeStr);
    (void)mailer.sendAlert(subject, message, emailAddr, false);
    Serial.printf("[Web] 📧 Email action manuelle ajouté à la queue: %s\n", subject);
  } else {
    Serial.println("[Web] ⚠️ Email non configuré - action manuelle non notifiée");
  }
}

// Garde fragmentation: ne pas bloquer webTask si heap trop fragmenté (ex. WiFi connect)
static bool canCreateAsyncTask() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= HeapConfig::MIN_HEAP_BLOCK_FOR_ASYNC_TASK;
}

// Rate limit WiFi connect (évite rafales)
static unsigned long s_lastWifiConnectAt = 0;

// v11.178: Constructeur 2 params supprimé (non utilisé - audit dead-code)

WebServerManager::WebServerManager(SystemSensors& sensors, 
                                   SystemActuators& acts, Diagnostics& diag)
    : _sensors(sensors), _acts(acts), _diag(&diag) {
  initializeServer();
}

void WebServerManager::initializeServer() {
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
        #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        Serial.printf("[Web] 💧 Stopping %s...\\n", relayName);
        #endif
        stop();
        newState = false;
        response = offResponse;
        #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        Serial.printf("[Web] ✅ %s stopped\\n", relayName);
        #endif
    } else {
        #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        Serial.printf("[Web] 💧 Starting %s...\\n", relayName);
        #endif
        start();
        newState = true;
        response = onResponse;
        #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        Serial.printf("[Web] ✅ %s started\\n", relayName);
        #endif
    }

    // Sauvegarde NVS + pending sync
    g_autoCtrl.saveCurrentActuatorState(relayName, newState);
    g_autoCtrl.markPendingSync(relayName, newState);

    // Feedback WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();

    // Sync serveur avec dernière lecture connue (évite _sensors.read() 3–5 s dans webTask)
    SensorReadings readings{};
    _sensors.getLastCachedReadings(readings);
    (void)g_autoCtrl.sendFullUpdate(readings);
    g_autoCtrl.clearPendingSync(relayName);

    return response;
}

bool WebServerManager::begin() {
  #ifdef DISABLE_ASYNC_WEBSERVER
  // Mode minimal sans serveur web
  Serial.println("[WebServer] Mode minimal - serveur web désactivé");
  return true;
  #else

  // Initialiser le serveur WebSocket temps réel (callback dbVars pour mise à jour temps réel page Contrôles)
  g_realtimeWebSocket.begin(_sensors, _acts, &fillDbVarsJson);
  // Aligner le cache WebSocket avec l'automatism (NVS) pour forceWakeUp et mailNotif
  g_realtimeWebSocket.updateForceWakeUpState(g_autoCtrl.getForceWakeUp());
  g_realtimeWebSocket.updateMailNotifState(g_autoCtrl.isEmailEnabled());

  // Configurer les routes de bundles d'assets
  AssetBundler::setupBundleRoutes(_server);

  // Utiliser AppContext global au lieu de WebServerContext
  extern AppContext g_appContext;
  AppContext& ctx = g_appContext;
  
  WebRoutes::registerUiRoutes(*_server, ctx);
  WebRoutes::registerStatusRoutes(*_server, ctx);

  // ============================================================================
  // ALIAS ENDPOINTS CONTRACTUELS (conformité règles interface web locale)
  // ============================================================================
  
  // /api/status -> alias contractuel pour /json (GET état capteurs/actionneurs)
  // Redirige vers /json pour éviter la duplication de code
  _server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    g_autoCtrl.notifyLocalWebActivity();
    req->redirect("/json");
  });

  // /api/feed -> endpoint POST pour nourrissage (type=small|big)
  _server->on("/api/feed", HTTP_POST, [this](AsyncWebServerRequest* req) {
    g_autoCtrl.notifyLocalWebActivity();
    
    // Extraire le paramètre type avec validation
    char typeBuf[16] = "small"; // défaut
    if (req->hasParam("type", true)) {
      const AsyncWebParameter* p = req->getParam("type", true);
      if (p) {
        Utils::safeStrncpy(typeBuf, p->value().c_str(), sizeof(typeBuf));
      }
    }
    
    // Validation du type: doit être "small" ou "big"
    if (strcmp(typeBuf, "small") != 0 && strcmp(typeBuf, "big") != 0) {
      sendErrorResponse(req, 400, "Invalid type parameter. Must be 'small' or 'big'", true);
      return;
    }
    
    if (g_autoCtrl.isFeedingInProgress()) {
      req->send(409, "application/json", "{\"success\":false,\"error\":\"FEED_BUSY\",\"message\":\"Nourrissage en cours\"}");
      return;
    }
    if (strcmp(typeBuf, "big") == 0) {
      g_autoCtrl.manualFeedBig();
      g_autoCtrl.setBouffeGrosFlag("1");
      req->send(NetworkConfig::HTTP_OK, "application/json", "{\"success\":true,\"action\":\"feedBig\"}");
    } else {
      g_autoCtrl.manualFeedSmall();
      g_autoCtrl.setBouffePetitsFlag("1");
      req->send(NetworkConfig::HTTP_OK, "application/json", "{\"success\":true,\"action\":\"feedSmall\"}");
    }
    g_realtimeWebSocket.broadcastNow();
    // Remise à 0 pour cohérence avec GET /action et POST /api/wakeup (observabilité /json)
    g_autoCtrl.setBouffePetitsFlag("0");
    g_autoCtrl.setBouffeGrosFlag("0");
  });

  // /action endpoint for remote controls - OPTIMISÉ POUR RÉACTIVITÉ
  _server->on("/action", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req){
      g_autoCtrl.notifyLocalWebActivity();
      const char* resp = "OK";

      // v11.169: Logs verbeux conditionnés (audit performance)
      #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
      IPAddress remoteIP = req->client()->remoteIP();
      char remoteIPBuf[16];
      snprintf(remoteIPBuf, sizeof(remoteIPBuf), "%d.%d.%d.%d",
               remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
      Serial.printf("[Web] 🎮 Action request from %s\n", remoteIPBuf);
      #endif
      
      // Traitement des commandes de nourrissage (PRIORITÉ ABSOLUE)
      if (req->hasParam("cmd")) {
          const AsyncWebParameter* pCmd = req->getParam("cmd");
          if (pCmd) {
          char cmdBuf[64];
          Utils::safeStrncpy(cmdBuf, pCmd->value().c_str(), sizeof(cmdBuf));
          const char* c = cmdBuf;
          #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
          Serial.printf("[Web] 🎯 Command: %s\n", c);
          #endif
          
          if (strcmp(c, "feedSmall") == 0) {
              if (g_autoCtrl.isFeedingInProgress()) {
                  resp = "FEED_BUSY";
                  Serial.println("[Web] ⚠️ Nourrissage petits refusé - cycle en cours");
              } else {
                  Serial.println("[Web] 🐟 Starting manual feed small...");
                  g_autoCtrl.manualFeedSmall();
                  g_autoCtrl.setBouffePetitsFlag("1");
                  g_realtimeWebSocket.broadcastNow();
                  resp = "FEED_SMALL OK";
                  esp_task_wdt_reset();
                  vTaskDelay(pdMS_TO_TICKS(100));
                  sendManualActionEmail(
                    "Bouffe manuelle - Petits poissons", "Bouffe manuelle", "NOURRISSAGE_PETITS");
                  SensorReadings readings{};
                  _sensors.getLastCachedReadings(readings);  // Pas de read() bloquant dans webTask
                  (void)g_autoCtrl.sendFullUpdate(readings, nullptr);
                  g_autoCtrl.setBouffePetitsFlag("0");
                  Serial.println("[Web] ✅ Small feed completed");
              }
          }
          else if (strcmp(c, "feedBig") == 0) {
              if (g_autoCtrl.isFeedingInProgress()) {
                  resp = "FEED_BUSY";
                  Serial.println("[Web] ⚠️ Nourrissage gros refusé - cycle en cours");
              } else {
                  Serial.println("[Web] 🐠 Starting manual feed big...");
                  g_autoCtrl.manualFeedBig();
                  g_autoCtrl.setBouffeGrosFlag("1");
                  g_realtimeWebSocket.broadcastNow();
                  resp = "FEED_BIG OK";
                  esp_task_wdt_reset();
                  vTaskDelay(pdMS_TO_TICKS(100));
                  sendManualActionEmail(
                    "Bouffe manuelle - Gros poissons", "Bouffe manuelle", "NOURRISSAGE_GROS");
                  SensorReadings readings{};
                  _sensors.getLastCachedReadings(readings);  // Pas de read() bloquant dans webTask
                  (void)g_autoCtrl.sendFullUpdate(readings, nullptr);
                  g_autoCtrl.setBouffeGrosFlag("0");
                  Serial.println("[Web] ✅ Big feed completed");
              }
          }
          else if (strcmp(c, "toggleEmail") == 0) {
              Serial.println("[Web] 📧 Toggling Email Notifications...");
              // Toggle Email Notifications
              g_autoCtrl.toggleEmailNotifications();
              // Synchroniser mailNotif pour le WebSocket (même flux que les relais)
              g_realtimeWebSocket.updateMailNotifState(g_autoCtrl.isEmailEnabled());
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              resp = g_autoCtrl.isEmailEnabled() ? "EMAIL_NOTIF_ACTIVÉ" : "EMAIL_NOTIF_DÉSACTIVÉ";
              Serial.printf("[Web] ✅ Email Notifications toggled: %s\n",
                            g_autoCtrl.isEmailEnabled() ? "ON" : "OFF");
          }
          else if (strcmp(c, "forceWakeUp") == 0) {
              Serial.println("[Web] 🔄 Toggling Force Wakeup...");
              // Toggle Force Wakeup
              g_autoCtrl.toggleForceWakeup();
              // Push UI refresh IMMÉDIAT
              g_realtimeWebSocket.broadcastNow();
              resp="FORCE_WAKEUP TOGGLE OK";
              Serial.printf("[Web] ✅ Force Wakeup toggled: %s\n",
                            g_autoCtrl.getForceWakeUp() ? "ON" : "OFF");
          }
          else if (strcmp(c, "resetMode") == 0) {
              Serial.println("[Web] 🔄 Reset ESP demandé (réponse envoyée puis redémarrage)");
              g_realtimeWebSocket.broadcastNow();
              resp = "RESET OK";
              s_pendingRestart = true;  // Redémarrer après envoi de la réponse
          }
          else if (strcmp(c, "wifiToggle") == 0) {
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
      }
      
      // Traitement des relais avec feedback immédiat
      if (req->hasParam("relay")) {
          const AsyncWebParameter* pRelay = req->getParam("relay");
          if (pRelay) {
          char relayBuf[64];
          Utils::safeStrncpy(relayBuf, pRelay->value().c_str(), sizeof(relayBuf));
          const char* rel = relayBuf;
          #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
          Serial.printf("[Web] 🔌 Relay control: %s\\n", rel);
          #endif
          
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
      }
      
      #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
      Serial.printf("[Web] 📤 Sending response: %s\\n", resp);
      #endif
      
      // Réponse immédiate avec headers optimisés
      AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", resp);
      if (response) {
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        req->send(response);
      } else {
        Serial.println("[Web] ❌ Échec création réponse (mémoire?)");
        req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Erreur mémoire serveur");
      }

      if (s_pendingRestart) {
        s_pendingRestart = false;
        Serial.println("[Web] 🔄 Redémarrage ESP dans 1s...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
      }
      
      #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
      Serial.printf("[Web] ✅ Action completed - Response sent to %s\n", remoteIPBuf);
      #endif
  });

  // NOTE: /json endpoints (GET, HEAD, OPTIONS) sont enregistrés dans web_routes_status.cpp
  // via registerJsonEndpoint() - ne pas dupliquer ici

  // /diag endpoint
  _server->on("/diag", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req) {
    // v11.40: Pas de notifyLocalWebActivity() - endpoint diagnostic
    if (_diag) {
      // Augmente la capacité si l'on inclut taskStats (peut être long)
      StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> big;
      _diag->toJson(big);
      sendJsonResponse(req, big);
      return;
    }
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    sendJsonResponse(req, doc);
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

    if (ESP.getFreeHeap() < HeapConfig::MIN_HEAP_DBVARS_ROUTE) {
      Serial.printf("[Web] ⚠️ Mémoire insuffisante pour /dbvars (%u < %u bytes)\n",
                    ESP.getFreeHeap(), HeapConfig::MIN_HEAP_DBVARS_ROUTE);
      req->send(NetworkConfig::HTTP_SERVICE_UNAVAILABLE, "text/plain", "Service temporairement indisponible - mémoire faible");
      return;
    }

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE_DBVARS> doc;
    JsonObject root = doc.to<JsonObject>();
    fillDbVarsJson(root);

    // Garde heap juste avant beginResponseStream (évite abort() cbuf/WebResponses si heap a baissé)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/dbvars"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });

  // Mise à jour des variables distantes locales et envoi vers la BDD distante
  // Flux serveur → NVS → logique locale (plan simplification):
  // 1. Réception paramètres 2. Validation (clés connues) 3. Écriture NVS via config.saveRemoteVars
  // 4. Mise à jour RAM via applyConfigFromJson 5. Sync distant optionnelle (non bloquante)
  // Handler partagé pour POST /dbvars/update et POST /api/config (alias contractuel, pas de redirect pour conserver le body)
  auto handleDbVarsUpdate = [this, &ctx](AsyncWebServerRequest* req) {
    g_autoCtrl.notifyLocalWebActivity();

    // Plan 3.3: debounce pour éviter écritures NVS trop fréquentes (sliders UI)
    static unsigned long lastDbvarsUpdateMs = 0;
    unsigned long nowMs = millis();
    if (lastDbvarsUpdateMs > 0 && (nowMs - lastDbvarsUpdateMs) < 2000) {
      StaticJsonDocument<128> doc;
      doc["status"] = "OK";
      doc["throttled"] = true;
      char buf[128];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }
    lastDbvarsUpdateMs = nowMs;

    char extraPairs[512] = {0}; // Buffer pour les paires clé-valeur
    char* p = extraPairs;
    const char* end = extraPairs + sizeof(extraPairs);
    bool any = false;

    // Charger JSON NVS existant (si présent) - buffer fixe, pas de heap
    char cachedJson[2048];
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> nvsDoc;
    if (config.loadRemoteVars(cachedJson, sizeof(cachedJson)) && strlen(cachedJson) > 0) {
      deserializeJson(nvsDoc, cachedJson);
    }

    char paramBuf[128];
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

    // Harmonisation config: écriture en clés canoniques (serveur distant)
    if (getWebParam(req, "bouffeMatin", paramBuf, sizeof(paramBuf))) appendPair("bouffeMatin", paramBuf);
    if (getWebParam(req, "bouffeMidi", paramBuf, sizeof(paramBuf))) appendPair("bouffeMidi", paramBuf);
    if (getWebParam(req, "bouffeSoir", paramBuf, sizeof(paramBuf))) appendPair("bouffeSoir", paramBuf);
    if (getWebParam(req, "tempsGros", paramBuf, sizeof(paramBuf))) appendPair("tempsGros", paramBuf);
    if (getWebParam(req, "tempsPetits", paramBuf, sizeof(paramBuf))) appendPair("tempsPetits", paramBuf);
    if (getWebParam(req, "aqThreshold", paramBuf, sizeof(paramBuf))) appendPair("aqThreshold", paramBuf);
    if (getWebParam(req, "tankThreshold", paramBuf, sizeof(paramBuf))) appendPair("tankThreshold", paramBuf);
    if (getWebParam(req, "chauffageThreshold", paramBuf, sizeof(paramBuf))) appendPair("chauffageThreshold", paramBuf);
    if (getWebParam(req, "tempsRemplissageSec", paramBuf, sizeof(paramBuf))) appendPair("tempsRemplissageSec", paramBuf);
    if (getWebParam(req, "limFlood", paramBuf, sizeof(paramBuf))) appendPair("limFlood", paramBuf);
    if (getWebParam(req, "FreqWakeUp", paramBuf, sizeof(paramBuf))) appendPair("FreqWakeUp", paramBuf);
    if (getWebParam(req, "mail", paramBuf, sizeof(paramBuf))) appendPair("mail", paramBuf);
    if (getWebParam(req, "mailNotif", paramBuf, sizeof(paramBuf))) appendPair("mailNotif", paramBuf);

    // Sauvegarde immédiate en NVS du JSON fusionné
    {
      char saveStr[2048];
      serializeJson(nvsDoc, saveStr, sizeof(saveStr));
      config.saveRemoteVars(saveStr);
      Serial.printf("[Web] 📥 Config sauvegardée en NVS (%zu bytes)\n", strlen(saveStr));
    }

    // Persister l'email en clé NVS dédiée pour chargement au boot (automatism.cpp lit NVSKeys::Config::EMAIL)
    if (nvsDoc.containsKey("mail")) {
      const char* mailVal = nvsDoc["mail"].as<const char*>();
      if (mailVal && strlen(mailVal) > 0) {
        g_nvsManager.saveString(NVS_NAMESPACES::CONFIG, NVSKeys::Config::EMAIL, mailVal);
      }
    }

    // Applique les valeurs localement (sans dépendre du distant)
    {
      g_autoCtrl.applyConfigFromJson(nvsDoc);
      Serial.println("[Web] ✅ Config appliquée localement");
    }
    
    // PRIORITÉ LOCALE (v11.32): Marquer pending sync
    g_autoCtrl.markConfigPendingSync();

    // Envoi immédiat avec dernière lecture connue (évite _sensors.read() bloquant dans webTask)
    SensorReadings syncReadings{};
    _sensors.getLastCachedReadings(syncReadings);
    bool sent = (WiFi.status() == WL_CONNECTED)
      ? g_autoCtrl.sendFullUpdate(syncReadings, any ? extraPairs : nullptr)
      : false;
    
    if (sent) {
      // Sync réussi : effacer pending sync
      g_autoCtrl.clearConfigPendingSync();
      Serial.println("[Web] ✅ Config synced to server");
    } else {
      Serial.println("[Web] ⏳ Config sync pending (will retry)");
    }
    
    // Toujours retourner 200 pour indiquer que l'enregistrement local s'est bien passé,
    // et indiquer séparément si l'envoi distant a réussi
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    doc["remoteSent"] = sent;
    sendJsonResponse(req, doc);
  };
  _server->on("/dbvars/update", HTTP_POST, handleDbVarsUpdate);
  _server->on("/api/config", HTTP_POST, handleDbVarsUpdate);

  // Page de mise à jour OTA avec bouton POST /api/ota
  _server->on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(NetworkConfig::HTTP_OK, "text/html",
      "<html><head><meta charset='utf-8'><title>FFP5CS OTA</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>body{font-family:sans-serif;padding:16px;max-width:480px;}"
      "button{padding:10px 20px;margin:8px 0;cursor:pointer;border-radius:6px;border:1px solid #0d6efd;background:#0d6efd;color:#fff;} "
      "button:disabled{opacity:0.6;cursor:not-allowed;} #msg{margin-top:12px;padding:8px;border-radius:4px;white-space:pre-wrap;}</style></head><body>"
      "<h1>FFP5CS - Mise à jour OTA</h1>"
      "<p>Vérifier et lancer une mise à jour firmware (WiFi requis).</p>"
      "<button id='btn' onclick='runOta()'>Vérifier et mettre à jour</button>"
      "<div id='msg'></div>"
      "<p><a href='/'>Retour au dashboard</a></p>"
      "<script>"
      "async function runOta(){"
      "var btn=document.getElementById('btn');var msg=document.getElementById('msg');"
      "btn.disabled=true;msg.textContent='Vérification...';"
      "try{"
      "var r=await fetch('/api/ota',{method:'POST'});"
      "var j=await r.json();"
      "msg.textContent=j.message||JSON.stringify(j);"
      "msg.style.background=j.ok?'#d4edda':'#f8d7da';"
      "}catch(e){msg.textContent='Erreur: '+e.message;msg.style.background='#f8d7da';}"
      "btn.disabled=false;}"
      "</script></body></html>");
  });

  // POST /api/ota - Déclenchement manuel OTA (vérification + mise à jour si disponible)
  _server->on("/api/ota", HTTP_POST, [](AsyncWebServerRequest* req) {
    g_autoCtrl.notifyLocalWebActivity();
    extern AppContext g_appContext;
    OTAManager& ota = g_appContext.otaManager;

    if (WiFi.status() != WL_CONNECTED) {
      StaticJsonDocument<256> doc;
      doc["triggered"] = false;
      doc["message"] = "WiFi non connecté";
      doc["ok"] = false;
      char buf[256];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }
    if (ESP.getFreeHeap() < HeapConfig::MIN_HEAP_OTA) {
      StaticJsonDocument<256> doc;
      doc["triggered"] = false;
      doc["message"] = "Heap insuffisant pour OTA";
      doc["ok"] = false;
      char buf[256];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }

    ota.setCurrentVersion(ProjectConfig::VERSION);
    bool hasUpdate = ota.checkForUpdate();
    if (!hasUpdate) {
      StaticJsonDocument<256> doc;
      doc["triggered"] = false;
      doc["message"] = "Aucune mise à jour disponible";
      doc["currentVersion"] = ota.getCurrentVersion();
      doc["ok"] = true;
      char buf[256];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }
    bool started = ota.performUpdate();
    StaticJsonDocument<256> doc;
    doc["triggered"] = started;
    doc["message"] = started ? "Mise à jour lancée" : "Échec lancement OTA";
    doc["remoteVersion"] = ota.getRemoteVersion();
    doc["ok"] = started;
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    req->send(NetworkConfig::HTTP_OK, "application/json", buf);
  });

  // /mailtest endpoint: envoie un e-mail de test
  _server->on("/mailtest", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Action utilisateur critique
    g_autoCtrl.notifyLocalWebActivity();
    char subjBuf[128];
    if (req->hasParam("subject")) {
      const AsyncWebParameter* p = req->getParam("subject");
      if (p) {
        Utils::safeStrncpy(subjBuf, p->value().c_str(), sizeof(subjBuf));
      } else {
        Utils::safeStrncpy(subjBuf, "Test FFP5CS", sizeof(subjBuf));
      }
    } else {
      Utils::safeStrncpy(subjBuf, "Test FFP5CS", sizeof(subjBuf));
    }
    char bodyBuf[256];
    if (req->hasParam("body")) {
      const AsyncWebParameter* p = req->getParam("body");
      if (p) {
        Utils::safeStrncpy(bodyBuf, p->value().c_str(), sizeof(bodyBuf));
      } else {
        Utils::safeStrncpy(bodyBuf, "Ceci est un e-mail de test envoyé depuis l'ESP32.", sizeof(bodyBuf));
      }
    } else {
      Utils::safeStrncpy(bodyBuf, "Ceci est un e-mail de test envoyé depuis l'ESP32.", sizeof(bodyBuf));
    }
    char destBuf[128];
    if (req->hasParam("to")) {
      const AsyncWebParameter* p = req->getParam("to");
      if (p) {
        Utils::safeStrncpy(destBuf, p->value().c_str(), sizeof(destBuf));
      } else {
        const char* configured = g_autoCtrl.getEmailAddress();
        if (configured && strlen(configured) > 0) {
          Utils::safeStrncpy(destBuf, configured, sizeof(destBuf));
        } else {
          Utils::safeStrncpy(destBuf, EmailConfig::DEFAULT_RECIPIENT, sizeof(destBuf));
        }
      }
    } else {
      const char* configured = g_autoCtrl.getEmailAddress();
      if (configured && strlen(configured) > 0) {
        Utils::safeStrncpy(destBuf, configured, sizeof(destBuf));
      } else {
        Utils::safeStrncpy(destBuf, EmailConfig::DEFAULT_RECIPIENT, sizeof(destBuf));
      }
    }
    bool ok = mailer.sendAlert(subjBuf, bodyBuf, destBuf);
    const char* resp = ok ? "OK" : "FAIL";
    req->send(NetworkConfig::HTTP_OK, "text/plain", resp);
  });

#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  // Maintenance: format LittleFS (use with care)
  _server->on("/fs/format", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Action maintenance critique
    g_autoCtrl.notifyLocalWebActivity();
    if (!req->hasParam("confirm")) {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing confirm=1");
      return;
    }
    const AsyncWebParameter* pConfirm = req->getParam("confirm");
    if (!pConfirm || pConfirm->value() != "1") {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "confirm must be 1");
      return;
    }
    bool ok = LittleFS.format();
    req->send(ok ? 200 : 500, "text/plain", ok ? "LittleFS formatted" : "Format failed");
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

  // /testota endpoint: active manuellement le flag OTA pour les tests
  _server->on("/testota", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint de test
    config.setOtaUpdateFlag(true);
    req->send(NetworkConfig::HTTP_OK, "text/plain", "Flag OTA activé - redémarrez pour tester l'email");
  });

  // -------------------------------------------------------------------
  // NVS Inspector: lister, modifier, effacer les variables persistantes
  // -------------------------------------------------------------------
#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  _server->on("/nvs.json", HTTP_GET, [](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Outil de debug interactif
    g_autoCtrl.notifyLocalWebActivity();
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/nvs.json"))) {
      return;
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    if (!res) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    res->print('{');
    res->print("\"namespaces\":{");

    bool firstNs = true;
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    nvs_iterator_t it = nullptr;
    if (nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it) == ESP_OK) {
      do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        if (!firstNs) res->print(',');
        firstNs = false;
        res->print('"'); res->print(info.namespace_name); res->print("\":{");

        bool firstKey = true;
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
              printNvsEntryToJson(h, e2, res);
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
#else
    // IDF 4.x: nvs_entry_find(3 args) retourne l'itérateur, nvs_entry_next(it) retourne le suivant
    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY);
    while (it != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      if (!firstNs) res->print(',');
      firstNs = false;
      res->print('"'); res->print(info.namespace_name); res->print("\":{");

      bool firstKey = true;
      nvs_iterator_t it2 = nvs_entry_find(NVS_DEFAULT_PART_NAME, info.namespace_name, NVS_TYPE_ANY);
      while (it2 != nullptr) {
        nvs_entry_info_t e2; nvs_entry_info(it2, &e2);
        if (strcmp(e2.namespace_name, info.namespace_name) != 0) { it2 = nvs_entry_next(it2); continue; }
        if (!firstKey) res->print(',');
        firstKey = false;
        res->print('"'); res->print(e2.key); res->print("\":{");

        nvs_handle_t h;
        if (nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
          printNvsEntryToJson(h, e2, res);
          nvs_close(h);
        }
        res->print('}');
        it2 = nvs_entry_next(it2);
      }

      res->print('}');
      it = nvs_entry_next(it);
    }
#endif

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
    req->send(NetworkConfig::HTTP_OK, "text/html", html);
  });

  // v11.178: Note audit - NVS Inspector utilise intentionnellement l'API NVS directe
  // (et non NVSManager) car il nécessite un accès bas niveau pour debug/inspection
  // avec support de tous les types NVS (U8, I8, U16, I16, U32, I32, U64, I64, STR, BLOB)
  _server->on("/nvs/set", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64], typeBuf[16], valueBuf[256];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf)) ||
        !getWebParam(req, "key", keyBuf, sizeof(keyBuf)) ||
        !getWebParam(req, "type", typeBuf, sizeof(typeBuf))) {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns/key/type");
      return;
    }
    getWebParam(req, "value", valueBuf, sizeof(valueBuf));
    
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }

    nvs_type_t t = nvsStrToType(typeBuf);
    if (t == NVS_TYPE_ANY) {
      nvs_close(h);
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Invalid type");
      return;
    }

    auto sendBadValue = [req, &h](const char* msg) {
      nvs_close(h);
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", msg);
    };

    bool valueChanged = false;
    errno = 0;
    char* endptr = nullptr;

    switch (t) {
      case NVS_TYPE_U8: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < 0 || parsed > 255) {
          sendBadValue("Invalid value for U8 (0-255)");
          return;
        }
        uint8_t v = (uint8_t)parsed;
        uint8_t current = 0;
        esp_err_t getErr = nvs_get_u8(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u8(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I8: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < -128 || parsed > 127) {
          sendBadValue("Invalid value for I8 (-128 to 127)");
          return;
        }
        int8_t v = (int8_t)parsed;
        int8_t current = 0;
        esp_err_t getErr = nvs_get_i8(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i8(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U16: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < 0 || parsed > 65535) {
          sendBadValue("Invalid value for U16 (0-65535)");
          return;
        }
        uint16_t v = (uint16_t)parsed;
        uint16_t current = 0;
        esp_err_t getErr = nvs_get_u16(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u16(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I16: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < -32768 || parsed > 32767) {
          sendBadValue("Invalid value for I16 (-32768 to 32767)");
          return;
        }
        int16_t v = (int16_t)parsed;
        int16_t current = 0;
        esp_err_t getErr = nvs_get_i16(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i16(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U32: {
        unsigned long parsed = strtoul(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for U32");
          return;
        }
        uint32_t v = (uint32_t)parsed;
        uint32_t current = 0;
        esp_err_t getErr = nvs_get_u32(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u32(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I32: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for I32");
          return;
        }
        int32_t v = (int32_t)parsed;
        int32_t current = 0;
        esp_err_t getErr = nvs_get_i32(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i32(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U64: {
        unsigned long long parsed = strtoull(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for U64");
          return;
        }
        uint64_t v = (uint64_t)parsed;
        uint64_t current = 0;
        esp_err_t getErr = nvs_get_u64(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u64(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I64: {
        long long parsed = strtoll(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for I64");
          return;
        }
        int64_t v = (int64_t)parsed;
        int64_t current = 0;
        esp_err_t getErr = nvs_get_i64(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i64(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_STR: {
        size_t len = 0;
        esp_err_t getErr = nvs_get_str(h, keyBuf, nullptr, &len);
        bool doSet = (getErr == ESP_ERR_NVS_NOT_FOUND);
        if (getErr == ESP_OK && len > 0) {
          if (len <= 512) {
            char currentStr[512];
            size_t readLen = len;
            getErr = nvs_get_str(h, keyBuf, currentStr, &readLen);
            if (getErr == ESP_OK && strcmp(currentStr, valueBuf) != 0) doSet = true;
          } else {
            doSet = true;
          }
        }
        if (doSet) {
          err = nvs_set_str(h, keyBuf, valueBuf);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_BLOB: {
        req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "BLOB set not supported");
        nvs_close(h);
        return;
      }
      default: break;
    }
    if (err != ESP_OK) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Write failed"); return; }
    if (valueChanged && nvs_commit(h) != ESP_OK) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Commit failed"); return; }
    nvs_close(h);

    // Rafraîchir l'état runtime si nécessaire (namespaces actuels cfg/sys + legacy bouffe/ota/rtc/remoteVars)
    const bool nsCfg = (strcmp(nsBuf, NVS_NAMESPACES::CONFIG) == 0);
    const bool nsSys = (strcmp(nsBuf, NVS_NAMESPACES::SYSTEM) == 0);
    const bool legacyBouffe = (strcmp(nsBuf, "bouffe") == 0);
    const bool legacyOta = (strcmp(nsBuf, "ota") == 0);
    const bool legacyRtc = (strcmp(nsBuf, "rtc") == 0);
    const bool legacyRemoteVars = (strcmp(nsBuf, "remoteVars") == 0);
    const bool keyBouffe = (strcmp(keyBuf, NVSKeys::Config::BOUFFE_MATIN) == 0 || strcmp(keyBuf, NVSKeys::Config::BOUFFE_MIDI) == 0 ||
                            strcmp(keyBuf, NVSKeys::Config::BOUFFE_SOIR) == 0 || strcmp(keyBuf, NVSKeys::Config::BOUFFE_JOUR) == 0 ||
                            strcmp(keyBuf, NVSKeys::Config::BF_PMP_LOCK) == 0);
    if ((nsCfg && keyBouffe) || legacyBouffe || (nsSys && strcmp(keyBuf, NVSKeys::System::OTA_UPDATE_FLAG) == 0) || legacyOta) {
      config.loadBouffeFlags();
    }
    if (legacyRtc || (nsSys && strcmp(keyBuf, NVSKeys::System::RTC_EPOCH) == 0)) {
      power.loadTimeFromFlash();
    }
    if ((nsCfg && strcmp(keyBuf, NVSKeys::Config::REMOTE_JSON) == 0) || (legacyRemoteVars && strcmp(keyBuf, "json") == 0)) {
      invalidateDbvarsCache();
      if (strlen(valueBuf) > 0) {
        StaticJsonDocument<256> tmp;
        if (!deserializeJson(tmp, valueBuf)) {
          g_autoCtrl.applyConfigFromJson(tmp);
        }
      }
    }

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });

  _server->on("/nvs/erase", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf)) || !getWebParam(req, "key", keyBuf, sizeof(keyBuf))) { 
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns/key"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_key(h, keyBuf); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Erase failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });

  _server->on("/nvs/erase_ns", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf))) { 
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Erase namespace failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

  // ========================================
  // GESTIONNAIRE WIFI - ENDPOINTS BACKEND
  // ========================================
  
  // Scanner les réseaux WiFi disponibles
  // NOTE: WiFi.scanNetworks() est intrinsèquement bloquant (2-5s) sur ESP32
  _server->on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
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
        response->addHeader("Access-Control-Allow-Origin", "*");
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
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
  
  // Lister les réseaux WiFi sauvegardés
  _server->on("/wifi/saved", HTTP_GET, [](AsyncWebServerRequest* req){
    // v11.40: Pas de notifyLocalWebActivity() - endpoint lecture seule
    
    // v11.169: Vérification mémoire (audit robustesse)
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_WIFI_ROUTE, F("/wifi/saved"))) {
      return;
    }
    
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
                  network["ssid"] = ssid;
                  network["password"] = password;
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

      // DÉROGATION: blocage webTask jusqu'à 5s (WIFI_CONNECT_TIMEOUT_MS) acceptable pour connexion WiFi
      esp_task_wdt_reset();
      bool connected = wifi.connectTo(ssidBuf, passwordBuf);
      if (connected) {
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
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });

  // v11.169: Routes /wifi/status, /server-status, /api/remote-flags, /debug-logs
  // supprimées car dupliquées dans web_routes_status.cpp (audit simplification)

  _server->begin();
  Serial.println(F("[Web] AsyncWebServer démarré sur le port 80"));
  Serial.printf("[Web] Timeout serveur: %u ms\n", NetworkConfig::WEB_SERVER_TIMEOUT_MS);
  Serial.printf("[Web] Connexions max: %u\n", NetworkConfig::WEB_SERVER_MAX_CONNECTIONS);
  Serial.println(F("[Web] Serveur HTTP prêt - Interface web accessible"));
  Serial.println(F("[Web] WebSocket temps réel sur le port 81"));
  // Afficher l'URL d'accès (STA ou AP) pour que l'utilisateur sache où se connecter
  {
    IPAddress addr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
    Serial.printf("[Web] Interface web: http://%d.%d.%d.%d/\n",
                  addr[0], addr[1], addr[2], addr[3]);
  }
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