#include "web_routes_status.h"

#ifndef DISABLE_ASYNC_WEBSERVER

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "asset_bundler.h"
#include "event_log.h"
#include "json_pool.h"
#include "network_optimizer.h"
#include "project_config.h"
#include "pump_stats_cache.h"
#include "automatism.h"
#include "config_manager.h"
#include "power.h"
#include "psram_optimizer.h"
#include "realtime_websocket.h"
#include "sensor_cache.h"
#include "system_actuators.h"
#include "system_sensors.h"
#include "web_server_context.h"

using WebRoutes::registerStatusRoutes;

namespace {

void registerWakeRoutes(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/ping", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 🏓 Ping reçu - Réveil système");
    EventLog::add("Réveil par ping");

    StaticJsonDocument<128> doc;
    doc["status"] = "awake";
    doc["timestamp"] = ctx.power.getCurrentTimeString();
    doc["uptime_ms"] = millis();

    String response;
    serializeJson(doc, response);
    ctx.sendJson(req, response);
  });

  server.on("/wakeup", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 🔔 Réveil explicite demandé");
    EventLog::add("Réveil par requête HTTP /wakeup");

    StaticJsonDocument<256> doc;
    doc["status"] = "awake";
    doc["timestamp"] = ctx.power.getCurrentTimeString();
    doc["wakeup_source"] = "http_request";
    doc["uptime_ms"] = millis();

    String response;
    serializeJson(doc, response);
    ctx.sendJson(req, response);

    ctx.realtimeWs.broadcastNow();
  });

  server.on("/api/wakeup", HTTP_POST, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 🔔 Réveil par API POST");

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

    if (action == "status") {
      StaticJsonDocument<1024> statusDoc;
      statusDoc["status"] = "awake";
      statusDoc["timestamp"] = ctx.power.getCurrentTimeString();
      statusDoc["uptime_ms"] = millis();
      statusDoc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
      if (WiFi.status() == WL_CONNECTED) {
        statusDoc["wifi_ip"] = WiFi.localIP().toString();
        statusDoc["wifi_rssi"] = WiFi.RSSI();
      }

      String response;
      serializeJson(statusDoc, response);
      ctx.sendJson(req, response);
    } else if (action == "feed") {
      Serial.println("[Web] 🍽️ Déclenchement nourrissage à distance");
      ctx.automatism.manualFeedSmall();

      SensorReadings readings = ctx.sensors.read();
      ctx.automatism.sendFullUpdate(readings, "bouffePetits=0");

      StaticJsonDocument<128> feedDoc;
      feedDoc["status"] = "feeding_triggered";
      feedDoc["timestamp"] = ctx.power.getCurrentTimeString();

      String response;
      serializeJson(feedDoc, response);
      ctx.sendJson(req, response);
    } else {
      StaticJsonDocument<128> respDoc;
      respDoc["status"] = "awake";
      respDoc["action"] = action;
      respDoc["timestamp"] = ctx.power.getCurrentTimeString();

      String response;
      serializeJson(respDoc, response);
      ctx.sendJson(req, response);
    }

    ctx.realtimeWs.broadcastNow();
  });
}

void registerWifiStatus(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/wifi/status", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }

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

    ctx.sendJson(req, json);
  });
}

void registerServerStatus(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/server-status", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.printf("[Web] 📊 Server status request from %s\n",
                  req->client()->remoteIP().toString().c_str());

    ArduinoJson::DynamicJsonDocument doc(512);
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapTotal"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramTotal"] = ESP.getPsramSize();
    doc["uptime"] = millis();
    doc["webServerTimeout"] = NetworkConfig::WEB_SERVER_TIMEOUT_MS;
    doc["maxConnections"] = NetworkConfig::WEB_SERVER_MAX_CONNECTIONS;

    doc["wifiStatus"] = WiFi.status();
    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiIP"] = WiFi.localIP().toString();
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["webSocketClients"] = ctx.realtimeWs.getConnectedClients();
    doc["forceWakeup"] = ctx.automatism.getForceWakeUp();

    String json;
    serializeJson(doc, json);
    Serial.printf("[Web] 📤 Server status sent (%u bytes)\n", json.length());

    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Cache-Control", "no-cache");
    req->send(response);
  });
}

void registerRemoteFlags(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/api/remote-flags", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ArduinoJson::DynamicJsonDocument doc(256);
    doc["sendEnabled"] = ctx.config.isRemoteSendEnabled();
    doc["recvEnabled"] = ctx.config.isRemoteRecvEnabled();
    String json;
    serializeJson(doc, json);
    ctx.sendJson(req, json);
  });

  server.on("/api/remote-flags", HTTP_POST, [&ctx](AsyncWebServerRequest* req) {
    bool changed = false;
    if (req->hasParam("send", true)) {
      String v = req->getParam("send", true)->value();
      v.toLowerCase();
      v.trim();
      bool enable = (v == "1" || v == "true" || v == "on");
      ctx.config.setRemoteSendEnabled(enable);
      changed = true;
    }
    if (req->hasParam("recv", true)) {
      String v = req->getParam("recv", true)->value();
      v.toLowerCase();
      v.trim();
      bool enable = (v == "1" || v == "true" || v == "on");
      ctx.config.setRemoteRecvEnabled(enable);
      changed = true;
    }
    String json = String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}";
    ctx.sendJson(req, json);
  });
}

void registerDebugLogs(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/debug-logs", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.printf("[Web] 🔍 Debug logs request from %s\n",
                  req->client()->remoteIP().toString().c_str());

    ArduinoJson::DynamicJsonDocument doc(1024);
    doc["system"]["uptime"] = millis();
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["heapSize"] = ESP.getHeapSize();
    doc["system"]["freePsram"] = ESP.getFreePsram();
    doc["system"]["psramSize"] = ESP.getPsramSize();

    doc["wifi"]["status"] = WiFi.status();
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["wifi"]["mac"] = WiFi.macAddress();

    doc["websocket"]["connectedClients"] = ctx.realtimeWs.getConnectedClients();
    doc["websocket"]["isActive"] = ctx.realtimeWs.isRunning();

    doc["automatism"]["forceWakeup"] = ctx.automatism.getForceWakeUp();
    doc["automatism"]["mailNotif"] = ctx.automatism.isEmailEnabled();
    doc["automatism"]["mail"] = ctx.automatism.getEmailAddress();

    SensorReadings readings = sensorCache.getReadings(ctx.sensors);
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

    String json;
    serializeJson(doc, json);
    Serial.printf("[Web] 📤 Debug logs sent (%u bytes)\n", json.length());
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", json);
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Access-Control-Allow-Origin", "*");
    req->send(response);
  });
}

void registerJsonEndpoint(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/json", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
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

  server.on("/json", HTTP_HEAD, [&ctx](AsyncWebServerRequest* req) {
    ctx.automatism.notifyLocalWebActivity();
    AsyncWebServerResponse* response = req->beginResponse(200, "application/json", "");
    if (response) {
      response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response->addHeader("Access-Control-Allow-Origin", "*");
      req->send(response);
    } else {
      req->send(500);
    }
  });

  server.on("/json", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ctx.automatism.notifyLocalWebActivity();
    if (!ctx.ensureHeap(req, ctx.jsonMinHeapBytes, F("/json"))) {
      return;
    }

    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      Serial.println("[Web] ⚠️ JSON pool exhausted, using fallback allocation");
      ArduinoJson::DynamicJsonDocument fallbackDoc(512);
      SensorReadings r = sensorCache.getReadings(ctx.sensors);

      fallbackDoc["tempWater"] = isnan(r.tempWater) ? 25.5 : r.tempWater;
      fallbackDoc["tempAir"] = isnan(r.tempAir) ? 22.3 : r.tempAir;
      fallbackDoc["humidity"] = isnan(r.humidity) ? 65.0 : r.humidity;
      fallbackDoc["wlAqua"] = r.wlAqua == 0 ? 15.2 : r.wlAqua;
      fallbackDoc["wlTank"] = r.wlTank == 0 ? 8.7 : r.wlTank;
      fallbackDoc["wlPota"] = r.wlPota == 0 ? 12.1 : r.wlPota;
      fallbackDoc["luminosite"] = r.luminosite == 0 ? 450 : r.luminosite;
      fallbackDoc["pumpAqua"] = ctx.actuators.isAquaPumpRunning();
      fallbackDoc["pumpTank"] = ctx.actuators.isTankPumpRunning();
      fallbackDoc["heater"] = ctx.actuators.isHeaterOn();
      fallbackDoc["light"] = ctx.actuators.isLightOn();
      fallbackDoc["forceWakeup"] = ctx.automatism.getForceWakeUp();

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

    SensorReadings r = sensorCache.getReadings(ctx.sensors);
    (*doc)["tempWater"] = isnan(r.tempWater) ? 25.5 : r.tempWater;
    (*doc)["tempAir"] = isnan(r.tempAir) ? 22.3 : r.tempAir;
    (*doc)["humidity"] = isnan(r.humidity) ? 65.0 : r.humidity;
    (*doc)["wlAqua"] = r.wlAqua == 0 ? 15.2 : r.wlAqua;
    (*doc)["wlTank"] = r.wlTank == 0 ? 8.7 : r.wlTank;
    (*doc)["wlPota"] = r.wlPota == 0 ? 12.1 : r.wlPota;
    (*doc)["luminosite"] = r.luminosite == 0 ? 450 : r.luminosite;
    (*doc)["pumpAqua"] = ctx.actuators.isAquaPumpRunning();
    (*doc)["pumpTank"] = ctx.actuators.isTankPumpRunning();
    (*doc)["heater"] = ctx.actuators.isHeaterOn();
    (*doc)["light"] = ctx.actuators.isLightOn();
    (*doc)["forceWakeup"] = ctx.automatism.getForceWakeUp();

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
    jsonPool.release(doc);

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
}

void registerVersionEndpoint(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/version", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    String js;
    js.reserve(32);
    js = "{\"version\":\"";
    js += ProjectConfig::VERSION;
    js += "\"}";
    ctx.sendJson(req, js);
  });
}

void registerPumpStats(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/pumpstats", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    auto stats = pumpStatsCache.getStats(ctx.actuators);

    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
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
    jsonPool.release(doc);

    NetworkOptimizer::sendWithCache(req, json, "application/json", 30);
  });
}

void registerOptimizationStats(AsyncWebServer& server, WebServerContext& ctx) {
  server.on("/optimization-stats", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(1024);
    if (!doc) {
      req->send(500, "application/json", "{\"error\":\"No JSON document available\"}");
      return;
    }

    auto jsonPoolStats = jsonPool.getStats();
    (*doc)["jsonPool"]["totalDocuments"] = jsonPoolStats.totalDocuments;
    (*doc)["jsonPool"]["usedDocuments"] = jsonPoolStats.usedDocuments;
    (*doc)["jsonPool"]["availableDocuments"] = jsonPoolStats.availableDocuments;
    (*doc)["jsonPool"]["totalCapacity"] = jsonPoolStats.totalCapacity;
    (*doc)["jsonPool"]["usedCapacity"] = jsonPoolStats.usedCapacity;

    auto sensorCacheStats = sensorCache.getStats();
    (*doc)["sensorCache"]["lastUpdate"] = sensorCacheStats.lastUpdate;
    (*doc)["sensorCache"]["cacheAge"] = sensorCacheStats.cacheAge;
    (*doc)["sensorCache"]["cacheDuration"] = sensorCacheStats.cacheDuration;
    (*doc)["sensorCache"]["isValid"] = sensorCacheStats.isValid;
    (*doc)["sensorCache"]["freeHeap"] = sensorCacheStats.freeHeap;

    auto wsStats = ctx.realtimeWs.getStats();
    (*doc)["webSocket"]["connectedClients"] = wsStats.connectedClients;
    (*doc)["webSocket"]["isActive"] = wsStats.isActive;
    (*doc)["webSocket"]["lastBroadcast"] = wsStats.lastBroadcast;
    (*doc)["webSocket"]["broadcastInterval"] = wsStats.broadcastInterval;

    auto bundleStats = AssetBundler::getBundleStats();
    (*doc)["bundles"]["jsBundleSize"] = bundleStats.jsBundleSize;
    (*doc)["bundles"]["cssBundleSize"] = bundleStats.cssBundleSize;
    (*doc)["bundles"]["minBundleSize"] = bundleStats.minBundleSize;
    (*doc)["bundles"]["jsAvailable"] = bundleStats.jsAvailable;
    (*doc)["bundles"]["cssAvailable"] = bundleStats.cssAvailable;
    (*doc)["bundles"]["minAvailable"] = bundleStats.minAvailable;
    (*doc)["bundles"]["totalFiles"] = bundleStats.totalFiles;
    (*doc)["bundles"]["totalSize"] = bundleStats.totalSize;

#ifdef BOARD_S3
    auto psramStats = PSRAMOptimizer::getStats();
    (*doc)["psram"]["available"] = psramStats.available;
    (*doc)["psram"]["total"] = psramStats.total;
    (*doc)["psram"]["free"] = psramStats.free;
    (*doc)["psram"]["used"] = psramStats.used;
    (*doc)["psram"]["usagePercent"] = psramStats.usagePercent;
#else
    (*doc)["psram"]["available"] = false;
    (*doc)["psram"]["total"] = 0;
    (*doc)["psram"]["free"] = 0;
    (*doc)["psram"]["used"] = 0;
    (*doc)["psram"]["usagePercent"] = 0;
#endif

    (*doc)["timestamp"] = millis();

    String json;
    json.reserve(1024);
    serializeJson(*doc, json);
    jsonPool.release(doc);

    NetworkOptimizer::sendOptimizedJson(req, json);
  });
}

}  // namespace

namespace WebRoutes {

void registerStatusRoutes(AsyncWebServer& server, WebServerContext& ctx) {
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
void registerStatusRoutes(void* server, void* ctx) {}
}  // namespace WebRoutes
#endif


