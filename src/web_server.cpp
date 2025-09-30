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

// Optimisations
#include "json_pool.h"
#include "sensor_cache.h"
#include "pump_stats_cache.h"
#include "network_optimizer.h"
#include "realtime_websocket.h"
#include "asset_bundler.h"
#include "psram_optimizer.h"
 
extern Automatism autoCtrl;
extern Mailer mailer;
extern ConfigManager config;
extern PowerManager power;

WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts)
    : _sensors(sensors), _acts(acts), _diag(nullptr) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  #endif
}

WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts, Diagnostics& diag)
    : _sensors(sensors), _acts(acts), _diag(&diag) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
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
  // Initialiser l'optimiseur PSRAM
  PSRAMOptimizer::init();
  
  // Initialiser le serveur WebSocket temps réel
  realtimeWebSocket.begin(_sensors, _acts);
  
  // Configurer les routes de bundles d'assets
  AssetBundler::setupBundleRoutes(_server);
  
  // Fichiers statiques depuis LittleFS (index.html par défaut)
  _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      req->send(LittleFS, "/index.html", "text/html");
  });
  
  // Page optimisée avec WebSockets et bundles
  _server->on("/optimized", HTTP_GET, [](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      req->send(LittleFS, "/index_optimized.html", "text/html");
  });

  // /action endpoint for remote controls
  _server->on("/action", HTTP_GET, [this](AsyncWebServerRequest* req){
      autoCtrl.notifyLocalWebActivity();
      String resp="";
      if (req->hasParam("cmd")) {
          String c = req->getParam("cmd")->value();
          if (c == "feedSmall") {
              autoCtrl.manualFeedSmall();
              // Notification & synchro identiques à la BDD
              if (autoCtrl.isEmailEnabled()) {
                  String message = autoCtrl.createFeedingMessage("Bouffe manuelle - Petits poissons", 
                                                               autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
                  mailer.sendAlert("Bouffe manuelle", message, autoCtrl.getEmailAddress().c_str());
                  autoCtrl.triggerMailBlink();
              }
              autoCtrl.sendFullUpdate(_sensors.read());
              resp="FEED_SMALL OK";
          }
          else if (c == "feedBig") {
              autoCtrl.manualFeedBig();
              if (autoCtrl.isEmailEnabled()) {
                  String message = autoCtrl.createFeedingMessage("Bouffe manuelle - Gros poissons", 
                                                               autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
                  mailer.sendAlert("Bouffe manuelle", message, autoCtrl.getEmailAddress().c_str());
                  autoCtrl.triggerMailBlink();
              }
              autoCtrl.sendFullUpdate(_sensors.read());
              resp="FEED_BIG OK";
          }
      }
      if (req->hasParam("relay")) {
          String rel = req->getParam("relay")->value();
          if (rel == "light") {
              if (_acts.isLightOn()) { _acts.light.off(); resp="LIGHT OFF"; }
              else { _acts.light.on(); resp="LIGHT ON"; }
          } else if (rel == "pumpTank") {
              if (_acts.isTankPumpRunning()) {
                  autoCtrl.stopTankPumpManual();
                  resp="PUMP_TANK OFF";
              }
              else {
                  autoCtrl.startTankPumpManual();
                  resp="PUMP_TANK ON";
              }
          } else if (rel == "pumpAqua") {
              if (_acts.isAquaPumpRunning()) {
                  Serial.println(F("[Web] Aqua OFF (toggle local)"));
                  autoCtrl.stopAquaPumpManualLocal(); resp="PUMP_AQUA OFF";
              }
              else { autoCtrl.startAquaPumpManualLocal(); resp="PUMP_AQUA ON"; }
          } else if (rel == "heater") {
              if (_acts.isHeaterOn()) { _acts.stopHeater(); resp="HEATER OFF"; }
              else { _acts.startHeater(); resp="HEATER ON"; }
          }
      }
      if(resp=="") resp="OK";
      req->send(200,"text/plain",resp);
  });

  // Point de terminaison JSON optimisé
  _server->on("/json", HTTP_GET, [this](AsyncWebServerRequest* req) {
    autoCtrl.notifyLocalWebActivity();
    
    // Utiliser le pool JSON pour optimiser la mémoire
    ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
    if (!doc) {
      // Fallback vers allocation directe si pool indisponible
      ArduinoJson::DynamicJsonDocument fallbackDoc(512);
      SensorReadings r = sensorCache.getReadings(_sensors);
      
      fallbackDoc["tempWater"] = r.tempWater;
      fallbackDoc["tempAir"] = r.tempAir;
      fallbackDoc["humidity"] = r.humidity;
      fallbackDoc["wlAqua"] = r.wlAqua;
      fallbackDoc["wlTank"] = r.wlTank;
      fallbackDoc["wlPota"] = r.wlPota;
      fallbackDoc["luminosite"] = r.luminosite;
      fallbackDoc["pumpAqua"] = _acts.isAquaPumpRunning();
      fallbackDoc["pumpTank"] = _acts.isTankPumpRunning();
      fallbackDoc["heater"] = _acts.isHeaterOn();
      fallbackDoc["light"] = _acts.isLightOn();
      fallbackDoc["voltage"] = r.voltageMv;
      fallbackDoc["timestamp"] = millis();
      
      String json;
      json.reserve(512);
      serializeJson(fallbackDoc, json);
      NetworkOptimizer::sendOptimizedJson(req, json);
      return;
    }
    
    // Récupérer les données via le cache (optimisé)
    SensorReadings r = sensorCache.getReadings(_sensors);
    
    (*doc)["tempWater"] = r.tempWater;
    (*doc)["tempAir"] = r.tempAir;
    (*doc)["humidity"] = r.humidity;
    (*doc)["wlAqua"] = r.wlAqua;
    (*doc)["wlTank"] = r.wlTank;
    (*doc)["wlPota"] = r.wlPota;
    (*doc)["luminosite"] = r.luminosite;
    (*doc)["pumpAqua"] = _acts.isAquaPumpRunning();
    (*doc)["pumpTank"] = _acts.isTankPumpRunning();
    (*doc)["heater"] = _acts.isHeaterOn();
    (*doc)["light"] = _acts.isLightOn();
    (*doc)["voltage"] = r.voltageMv;
    (*doc)["timestamp"] = millis();
    
    String json;
    json.reserve(512);
    serializeJson(*doc, json);
    
    // Libérer le document du pool
    jsonPool.release(doc);
    
    // Envoyer avec optimisation réseau
    NetworkOptimizer::sendOptimizedJson(req, json);
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
    
    // Envoyer avec optimisation réseau
    NetworkOptimizer::sendOptimizedJson(req, json);
  });

  // /dbvars endpoint : expose variables fetched from remote server
  _server->on("/dbvars", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    ArduinoJson::DynamicJsonDocument src(BufferConfig::JSON_DOCUMENT_SIZE);
    bool ok = autoCtrl.fetchRemoteState(src);
    if (!ok) {
      String cached;
      if (config.loadRemoteVars(cached) && cached.length() > 0) {
        auto err = deserializeJson(src, cached);
        if (!err) ok = true;
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
    out["emailAddress"] = src.containsKey("emailAddress") ? src["emailAddress"].as<const char*>() : (src.containsKey("mail") ? src["mail"].as<const char*>() : "");
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
    req->send(200, "application/json", js);
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
    if (getParam("emailEnabled").length()) {
      // Normaliser en "checked"/"" pour compat serveur
      String v = getParam("emailEnabled"); v.toLowerCase(); v.trim();
      bool on = (v == "1" || v == "true" || v == "on" || v == "checked");
      appendPair("mailNotif", on ? String("checked") : String(""));
    } else if (getParam("mailNotif").length()) {
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
    req->send(sent ? 200 : 503, "application/json", sent ? "{\"status\":\"OK\"}" : "{\"status\":\"ERROR\"}");
  });

  // Fichiers statiques hors-ligne pour le tableau de bord
  auto sendWithGzipIfAvailable = [](AsyncWebServerRequest* req, const char* path, const char* contentType){
    // Try gzip variant first
    String gz = String(path) + ".gz";
    if (LittleFS.exists(gz)) {
      AsyncWebServerResponse* r = req->beginResponse(LittleFS, gz, contentType);
      if (r) { r->addHeader("Content-Encoding", "gzip"); r->addHeader("Cache-Control", "public, max-age=604800"); req->send(r); return; }
    }
    AsyncWebServerResponse* r = req->beginResponse(LittleFS, path, contentType);
    if (r) { r->addHeader("Cache-Control", "public, max-age=604800"); req->send(r); } else { req->send(404); }
  };

  _server->on("/chart.js", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/chart.js", "application/javascript");
  });
  _server->on("/bootstrap.min.css", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/bootstrap.min.css", "text/css");
  });
  _server->on("/utils.js", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/utils.js", "application/javascript");
  });
  _server->on("/uplot.iife.min.js", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/uplot.iife.min.js", "application/javascript");
  });
  _server->on("/uplot.min.css", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/uplot.min.css", "text/css");
  });
  _server->on("/alpine.min.js", HTTP_GET, [sendWithGzipIfAvailable](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    sendWithGzipIfAvailable(req, "/alpine.min.js", "application/javascript");
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
      
      // Statistiques PSRAM
      auto psramStats = PSRAMOptimizer::getStats();
      (*doc)["psram"]["available"] = psramStats.available;
      (*doc)["psram"]["total"] = psramStats.total;
      (*doc)["psram"]["free"] = psramStats.free;
      (*doc)["psram"]["used"] = psramStats.used;
      (*doc)["psram"]["usagePercent"] = psramStats.usagePercent;
      
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

  _server->begin();
  Serial.println(F("[Web] AsyncWebServer démarré sur le port 80"));
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