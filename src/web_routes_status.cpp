#include "web_routes_status.h"

#ifndef DISABLE_ASYNC_WEBSERVER

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <string.h>
#include <ctype.h>

#include "asset_bundler.h"
#include "config.h"
#include "automatism.h"
#include "config_manager.h"
#include "power.h"
#include "realtime_websocket.h"
#include "system_actuators.h"
#include "system_sensors.h"
#include "app_context.h"

using WebRoutes::registerStatusRoutes;

// Fonctions libres helpers (remplace WebServerContext) - accessibles depuis web_server.cpp
void sendJsonResponse(AsyncWebServerRequest* req, const JsonDocument& doc, bool enableCors) {
  if (!req) {
    return;
  }
  if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("sendJson"))) {
    return;
  }

  AsyncResponseStream* response = req->beginResponseStream("application/json");
  if (!response) {
    Serial.println(F("[Web] ❌ Échec création réponse JSON (heap insuffisant)"));
    req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
    return;
  }

  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  if (enableCors) {
    response->addHeader("Access-Control-Allow-Origin", "*");
  }
  serializeJson(doc, *response);
  req->send(response);
}

// Helper unifié pour réponses d'erreur JSON avec CORS
void sendErrorResponse(AsyncWebServerRequest* req, int httpCode, const char* errorMessage, bool enableCors) {
  if (!req) {
    return;
  }

  // Buffer statique pour réponse d'erreur compacte
  char jsonBuf[256];
  snprintf(jsonBuf, sizeof(jsonBuf), "{\"error\":\"%s\",\"status\":\"error\"}", 
           errorMessage ? errorMessage : "Unknown error");
  
  AsyncWebServerResponse* response = req->beginResponse(httpCode, "application/json", jsonBuf);
  if (response) {
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    if (enableCors) {
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    }
    req->send(response);
  } else {
    // Fallback si création réponse échoue
    req->send(httpCode, "text/plain", errorMessage ? errorMessage : "Error");
  }
}

bool ensureHeapForRoute(AsyncWebServerRequest* req, uint32_t minHeap, const __FlashStringHelper* routeName) {
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap >= minHeap) {
    return true;
  }

  if (req) {
    char message[128];
    snprintf(message, sizeof(message), "Service temporairement indisponible - mémoire faible (heap=%u bytes)", freeHeap);
    req->send(NetworkConfig::HTTP_SERVICE_UNAVAILABLE, "text/plain", message);
  }

  Serial.printf_P(PSTR("[Web] ⚠️ Mémoire insuffisante pour %s (%u < %u bytes)\n"),
                  routeName ? (const char*)routeName : "route",
                  freeHeap,
                  minHeap);
  return false;
}

namespace {

void registerWakeRoutes(AsyncWebServer& server, AppContext& ctx) {
  server.on("/ping", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 🏓 Ping reçu - Réveil système");
    Serial.println("[Event] Réveil par ping");

    StaticJsonDocument<128> doc;
    doc["status"] = "awake";
    char timeBuf[64];
    ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
    doc["timestamp"] = timeBuf;
    doc["uptime_ms"] = millis();

    sendJsonResponse(req, doc);
  });

  server.on("/wakeup", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 🔔 Réveil explicite demandé");
    Serial.println("[Event] Réveil par requête HTTP /wakeup");

    StaticJsonDocument<256> doc;
    doc["status"] = "awake";
    char timeBuf[64];
    ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
    doc["timestamp"] = timeBuf;
    doc["wakeup_source"] = "http_request";
    doc["uptime_ms"] = millis();

    sendJsonResponse(req, doc);

    g_realtimeWebSocket.broadcastNow();
  });

  // POST /api/wakeup: AsyncCallbackJsonWebHandler collecte le body JSON (server.on ne le fait pas)
  server.on(AsyncURIMatcher::exact("/api/wakeup"), HTTP_POST, [&ctx](AsyncWebServerRequest* req, JsonVariant& json) {
    Serial.println("[Web] 🔔 Réveil par API POST");

    const char* action = json["action"].as<const char*>();
    if (!action) action = "unknown";
    const char* source = json["source"].as<const char*>();
    if (!source) source = "api";

    Serial.printf("[Web] Action de réveil: %s depuis %s\n", action, source);
    Serial.printf("[Event] Réveil API: %s depuis %s\n", action, source);

    if (strcmp(action, "status") == 0) {
      StaticJsonDocument<1024> statusDoc;
      statusDoc["status"] = "awake";
      char timeBuf[32];
      ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
      statusDoc["timestamp"] = timeBuf;
      statusDoc["uptime_ms"] = millis();
      statusDoc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        statusDoc["wifi_ip"] = ipBuf;
        statusDoc["wifi_rssi"] = WiFi.RSSI();
      }

      sendJsonResponse(req, statusDoc);
    } else if (strcmp(action, "feed") == 0) {
      const char* feedType = json["feedType"].as<const char*>();
      if (!feedType) feedType = "small";
      const bool isBig = (strcmp(feedType, "big") == 0);

      if (ctx.automatism.isFeedingInProgress()) {
        Serial.println("[Web] ⚠️ Nourrissage refusé (API) - cycle en cours");
        StaticJsonDocument<128> errDoc;
        errDoc["status"] = "feed_busy";
        errDoc["error"] = "Nourrissage en cours";
        sendJsonResponse(req, errDoc);
      } else {
        Serial.printf("[Web] 🍽️ Nourrissage à distance: %s\n", isBig ? "gros" : "petits");

        if (isBig) {
          ctx.automatism.setBouffeGrosFlag("1");
          ctx.automatism.manualFeedBig();
        } else {
          ctx.automatism.setBouffePetitsFlag("1");
          ctx.automatism.manualFeedSmall();
        }

        SensorReadings readings{};
        ctx.sensors.getLastCachedReadings(readings);  // Pas de read() bloquant dans webTask
        ctx.automatism.sendFullUpdate(readings, nullptr);

        if (isBig) {
          ctx.automatism.setBouffeGrosFlag("0");
        } else {
          ctx.automatism.setBouffePetitsFlag("0");
        }

        StaticJsonDocument<128> feedDoc;
        feedDoc["status"] = "feeding_triggered";
        feedDoc["feedType"] = isBig ? "big" : "small";
        char timeBuf[32];
        ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
        feedDoc["timestamp"] = timeBuf;

        sendJsonResponse(req, feedDoc);
      }
    } else {
      StaticJsonDocument<128> respDoc;
      respDoc["status"] = "awake";
      respDoc["action"] = action;
      char timeBuf[32];
      ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
      respDoc["timestamp"] = timeBuf;

      sendJsonResponse(req, respDoc);
    }

    g_realtimeWebSocket.broadcastNow();
  });
}

void registerWifiStatus(AsyncWebServer& server, AppContext& ctx) {
  server.on("/wifi/status", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    
    // Utilise le helper centralisé pour construire le JSON WiFi
    WiFiHelpers::addWifiInfoToJson(doc, true /* includeMac */);

    sendJsonResponse(req, doc);
  });
}

void registerServerStatus(AsyncWebServer& server, AppContext& ctx) {
  server.on("/server-status", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    IPAddress remoteIP = req->client()->remoteIP();
    char remoteIPBuf[16];
    snprintf(remoteIPBuf, sizeof(remoteIPBuf), "%d.%d.%d.%d", remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
    Serial.printf("[Web] 📊 Server status request from %s\n", remoteIPBuf);

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapTotal"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramTotal"] = ESP.getPsramSize();
    doc["uptime"] = millis();
    doc["webServerTimeout"] = NetworkConfig::WEB_SERVER_TIMEOUT_MS;
    doc["maxConnections"] = NetworkConfig::WEB_SERVER_MAX_CONNECTIONS;

    doc["wifiStatus"] = WiFi.status();
    char wifiSSIDBuf[33];
    WiFiHelpers::getSSID(wifiSSIDBuf, sizeof(wifiSSIDBuf));
    doc["wifiSSID"] = wifiSSIDBuf;
    IPAddress wifiIP = WiFi.localIP();
    char wifiIPBuf[16];
    snprintf(wifiIPBuf, sizeof(wifiIPBuf), "%d.%d.%d.%d", wifiIP[0], wifiIP[1], wifiIP[2], wifiIP[3]);
    doc["wifiIP"] = wifiIPBuf;
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["webSocketClients"] = g_realtimeWebSocket.getConnectedClients();
    doc["forceWakeUp"] = ctx.automatism.getForceWakeUp();
    doc["sendState"] = ctx.automatism.getSendState();
    doc["lastSendMs"] = ctx.automatism.getLastSendMs();
    doc["lastDataSkipReason"] = ctx.automatism.getLastDataSkipReason();

    Serial.printf("[Web] 📤 Server status sent (JSON)\n");

    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/api/status"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      Serial.println("[Web] ❌ Échec beginResponseStream pour /api/status");
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Internal server error\"}");
      return;
    }
    response->addHeader("Cache-Control", "no-cache");
    serializeJson(doc, *response);
    req->send(response);
  });
}

void registerRemoteFlags(AsyncWebServer& server, AppContext& ctx) {
  server.on("/api/remote-flags", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["sendEnabled"] = ctx.config.isRemoteSendEnabled();
    doc["recvEnabled"] = ctx.config.isRemoteRecvEnabled();
    sendJsonResponse(req, doc);
  });

  server.on("/api/remote-flags", HTTP_POST, [&ctx](AsyncWebServerRequest* req) {
    bool changed = false;
    if (req->hasParam("send", true)) {
      const AsyncWebParameter* pSend = req->getParam("send", true);
      if (pSend) {
      char vLower[16];
      strncpy(vLower, pSend->value().c_str(), sizeof(vLower) - 1);
      vLower[sizeof(vLower) - 1] = '\0';
      for (char* p = vLower; *p; ++p) *p = tolower(*p);
      // Trim (simple)
      char* start = vLower;
      while (*start == ' ') start++;
      char* end = start + strlen(start) - 1;
      while (end > start && *end == ' ') *end-- = '\0';
      bool enable = (strcmp(start, "1") == 0 || strcmp(start, "true") == 0 || strcmp(start, "on") == 0);
      ctx.config.setRemoteSendEnabled(enable);
      changed = true;
      }
    }
    if (req->hasParam("recv", true)) {
      const AsyncWebParameter* pRecv = req->getParam("recv", true);
      if (pRecv) {
      char vLower[16];
      strncpy(vLower, pRecv->value().c_str(), sizeof(vLower) - 1);
      vLower[sizeof(vLower) - 1] = '\0';
      for (char* p = vLower; *p; ++p) *p = tolower(*p);
      // Trim (simple)
      char* start = vLower;
      while (*start == ' ') start++;
      char* end = start + strlen(start) - 1;
      while (end > start && *end == ' ') *end-- = '\0';
      bool enable = (strcmp(start, "1") == 0 || strcmp(start, "true") == 0 || strcmp(start, "on") == 0);
      ctx.config.setRemoteRecvEnabled(enable);
      changed = true;
      }
    }
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/api/remote-flags"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      Serial.println("[Web] ❌ Échec beginResponseStream pour /api/remote-flags POST");
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Internal server error\"}");
      return;
    }
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["ok"] = true;
    doc["changed"] = changed;
    serializeJson(doc, *response);
    req->send(response);
  });
}

void registerDebugLogs(AsyncWebServer& server, AppContext& ctx) {
  server.on("/debug-logs", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    IPAddress remoteIP = req->client()->remoteIP();
    char remoteIPBuf[16];
    snprintf(remoteIPBuf, sizeof(remoteIPBuf), "%d.%d.%d.%d", remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
    Serial.printf("[Web] 🔍 Debug logs request from %s\n", remoteIPBuf);

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["system"]["uptime"] = millis();
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["heapSize"] = ESP.getHeapSize();
    doc["system"]["freePsram"] = ESP.getFreePsram();
    doc["system"]["psramSize"] = ESP.getPsramSize();

    doc["wifi"]["status"] = WiFi.status();
    char wifiSSIDBuf2[33];
    WiFiHelpers::getSSID(wifiSSIDBuf2, sizeof(wifiSSIDBuf2));
    doc["wifi"]["ssid"] = wifiSSIDBuf2;
    IPAddress wifiIP = WiFi.localIP();
    char wifiIPBuf[16];
    snprintf(wifiIPBuf, sizeof(wifiIPBuf), "%d.%d.%d.%d", wifiIP[0], wifiIP[1], wifiIP[2], wifiIP[3]);
    doc["wifi"]["ip"] = wifiIPBuf;
    doc["wifi"]["rssi"] = WiFi.RSSI();
    char wifiMacBuf[18];
    WiFiHelpers::getMACString(wifiMacBuf, sizeof(wifiMacBuf));
    doc["wifi"]["mac"] = wifiMacBuf;

    doc["websocket"]["connectedClients"] = g_realtimeWebSocket.getConnectedClients();
    doc["websocket"]["isActive"] = g_realtimeWebSocket.isRunning();

    doc["automatism"]["forceWakeUp"] = ctx.automatism.getForceWakeUp();
    doc["automatism"]["mailNotif"] = ctx.automatism.isEmailEnabled();
    doc["automatism"]["mail"] = ctx.automatism.getEmailAddress();

    SensorReadings readings{};
    if (!ctx.sensors.getLastCachedReadings(readings)) {
      readings.tempWater = NAN;
      readings.tempAir = NAN;
      readings.humidity = NAN;
      readings.wlAqua = readings.wlTank = readings.wlPota = readings.luminosite = 0;
    }
    doc["sensors"]["tempWater"] = readings.tempWater;
    doc["sensors"]["tempAir"] = readings.tempAir;
    doc["sensors"]["humidity"] = readings.humidity;
    doc["sensors"]["wlAqua"] = readings.wlAqua;
    doc["sensors"]["wlTank"] = readings.wlTank;
    doc["sensors"]["wlPota"] = readings.wlPota;
    doc["sensors"]["luminosite"] = readings.luminosite;

    doc["actuators"]["pumpAqua"] = ctx.actuators.isAquaPumpRunning();
    doc["actuators"]["pumpTank"] = ctx.actuators.isTankPumpRunning();
    doc["actuators"]["heater"] = ctx.actuators.isHeaterOn();
    doc["actuators"]["light"] = ctx.actuators.isLightOn();

    Serial.println("[Web] 📤 Debug logs sent");
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/debug-logs"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      Serial.println("[Web] ❌ Échec beginResponseStream pour /debug-logs");
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Internal server error\"}");
      return;
    }
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Access-Control-Allow-Origin", "*");
    serializeJson(doc, *response);
    req->send(response);
  });
}

void registerJsonEndpoint(AsyncWebServer& server, AppContext& ctx) {
  server.on("/json", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", "");
    if (response) {
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      response->addHeader("Access-Control-Max-Age", "86400");
      req->send(response);
    } else {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR);
    }
  });

  server.on("/json", HTTP_HEAD, [&ctx](AsyncWebServerRequest* req) {
    ctx.automatism.notifyLocalWebActivity();
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", "");
    if (response) {
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Access-Control-Allow-Origin", "*");
      req->send(response);
    } else {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR);
    }
  });

  server.on("/json", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ctx.automatism.notifyLocalWebActivity();
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_JSON_ROUTE, F("/json"))) {
      return;
    }

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    SensorReadings r{};
    if (!ctx.sensors.getLastCachedReadings(r)) {
      r.tempWater = r.tempAir = r.humidity = NAN;
      r.wlAqua = r.wlTank = r.wlPota = r.luminosite = 0;
    }
    // Utiliser les constantes centralisées pour les valeurs fallback
    doc["tempWater"] = isnan(r.tempWater) ? SensorConfig::Fallback::TEMP_WATER : r.tempWater;
    doc["tempAir"] = isnan(r.tempAir) ? SensorConfig::Fallback::TEMP_AIR : r.tempAir;
    doc["humidity"] = isnan(r.humidity) ? SensorConfig::Fallback::HUMIDITY : r.humidity;
    doc["wlAqua"] = r.wlAqua == 0 ? SensorConfig::Fallback::WATER_LEVEL_AQUA : r.wlAqua;
    doc["wlTank"] = r.wlTank == 0 ? SensorConfig::Fallback::WATER_LEVEL_TANK : r.wlTank;
    doc["wlPota"] = r.wlPota == 0 ? SensorConfig::Fallback::WATER_LEVEL_POTA : r.wlPota;
    doc["luminosite"] = r.luminosite == 0 ? SensorConfig::Fallback::LUMINOSITY : r.luminosite;
    doc["pumpAqua"] = ctx.actuators.isAquaPumpRunning();
    doc["pumpTank"] = ctx.actuators.isTankPumpRunning();
    doc["heater"] = ctx.actuators.isHeaterOn();
    doc["light"] = ctx.actuators.isLightOn();
    doc["forceWakeUp"] = ctx.automatism.getForceWakeUp();
    doc["mailNotif"] = ctx.automatism.isEmailEnabled();

    // Observabilité sync POST (compteurs et dernière durée)
    doc["sync"]["post_ok"] = ctx.automatism.getSyncPostOkCount();
    doc["sync"]["post_fail"] = ctx.automatism.getSyncPostFailCount();
    doc["sync"]["last_post_duration_ms"] = ctx.automatism.getSyncLastPostDurationMs();

    // Utilise le helper centralisé pour construire le JSON WiFi (sans MAC)
    WiFiHelpers::addWifiInfoToJson(doc, false /* includeMac */);

    doc["timestamp"] = millis();

    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/json"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      Serial.println("[Web] ❌ Échec beginResponseStream pour /json");
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Internal server error\"}");
      return;
    }
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    serializeJson(doc, *response);
    req->send(response);
  });
}

void registerVersionEndpoint(AsyncWebServer& server, AppContext& ctx) {
  server.on("/version", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    StaticJsonDocument<64> doc;
    doc["version"] = ProjectConfig::VERSION;
    sendJsonResponse(req, doc);
  });
}

void registerPumpStats(AsyncWebServer& server, AppContext& ctx) {
  server.on("/pumpstats", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    auto& acts = ctx.actuators;
    const unsigned long now = millis();
    const bool isRunning = acts.isTankPumpRunning();
    const unsigned long currentRuntime = acts.getTankPumpCurrentRuntime();
    const unsigned long totalRuntime = acts.getTankPumpTotalRuntime();
    const unsigned long totalStops = acts.getTankPumpTotalStops();
    const unsigned long lastStopTime = acts.getTankPumpLastStopTime();
    const unsigned long timeSinceLastStop =
        (lastStopTime > 0U && now >= lastStopTime) ? (now - lastStopTime) : 0U;
    const unsigned long timeSinceLastStopSec = timeSinceLastStop / 1000U;
    const unsigned long currentRuntimeSec = currentRuntime / 1000U;
    const unsigned long totalRuntimeSec = totalRuntime / 1000U;

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["isRunning"] = isRunning;
    doc["currentRuntime"] = currentRuntime;
    doc["currentRuntimeSec"] = currentRuntimeSec;
    doc["totalRuntime"] = totalRuntime;
    doc["totalRuntimeSec"] = totalRuntimeSec;
    doc["totalStops"] = totalStops;
    doc["lastStopTime"] = lastStopTime;
    doc["timeSinceLastStop"] = timeSinceLastStop;
    doc["timeSinceLastStopSec"] = timeSinceLastStopSec;
    doc["timestamp"] = now;

    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/pumpstats"))) {
      return;
    }
    AsyncResponseStream* response = req->beginResponseStream("application/json");
    if (!response) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Internal server error\"}");
      return;
    }
    response->addHeader("Cache-Control", "public, max-age=30");
    serializeJson(doc, *response);
    req->send(response);
  });
}

void registerOptimizationStats(AsyncWebServer& server, AppContext& ctx) {
  server.on("/optimization-stats", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;

    // JSON Pool supprimé
    doc["jsonPool"]["totalDocuments"] = 0;
    doc["jsonPool"]["usedDocuments"] = 0;
    doc["jsonPool"]["availableDocuments"] = 0;
    doc["jsonPool"]["totalCapacity"] = 0;
    doc["jsonPool"]["usedCapacity"] = 0;

    auto wsStats = g_realtimeWebSocket.getStats();
    doc["webSocket"]["connectedClients"] = wsStats.connectedClients;
    doc["webSocket"]["isActive"] = wsStats.isActive;
    doc["webSocket"]["lastBroadcast"] = wsStats.lastBroadcast;
    doc["webSocket"]["broadcastInterval"] = wsStats.broadcastInterval;

    auto bundleStats = AssetBundler::getBundleStats();
    doc["bundles"]["jsBundleSize"] = bundleStats.jsBundleSize;
    doc["bundles"]["cssBundleSize"] = bundleStats.cssBundleSize;
    doc["bundles"]["minBundleSize"] = bundleStats.minBundleSize;
    doc["bundles"]["jsAvailable"] = bundleStats.jsAvailable;
    doc["bundles"]["cssAvailable"] = bundleStats.cssAvailable;
    doc["bundles"]["minAvailable"] = bundleStats.minAvailable;
    doc["bundles"]["totalFiles"] = bundleStats.totalFiles;
    doc["bundles"]["totalSize"] = bundleStats.totalSize;

    doc["psram"]["available"] = false;
    doc["psram"]["total"] = 0;
    doc["psram"]["free"] = 0;
    doc["psram"]["used"] = 0;
    doc["psram"]["usagePercent"] = 0;

    doc["timestamp"] = millis();

    sendJsonResponse(req, doc);
  });
}

}  // namespace

namespace WebRoutes {

void registerStatusRoutes(AsyncWebServer& server, AppContext& ctx) {
  registerWakeRoutes(server, ctx);
  registerWifiStatus(server, ctx);
  registerServerStatus(server, ctx);
  registerRemoteFlags(server, ctx);
  registerDebugLogs(server, ctx);
  registerJsonEndpoint(server, ctx);
  registerVersionEndpoint(server, ctx);
  registerPumpStats(server, ctx);
  registerOptimizationStats(server, ctx);
}

}  // namespace WebRoutes

#else
// Stub si DISABLE_ASYNC_WEBSERVER est défini
namespace WebRoutes {
void registerStatusRoutes(AsyncWebServer& server, AppContext& ctx) { (void)server; (void)ctx; }
}  // namespace WebRoutes
#endif
