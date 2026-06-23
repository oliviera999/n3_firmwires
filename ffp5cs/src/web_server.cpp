#include "web_server.h"
#include "diagnostics.h"
#include "system_boot.h"  // SystemBoot::confirmOtaValidation (v14.17)
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <ArduinoJson.h>
#include "ffp5cs_fs.h"
#include "config.h"
#include "gpio_mapping.h"
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
#include <esp_random.h>  // v13.52: esp_fill_random pour entropie token session web
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#endif
#include "web_routes_status.h"
#include "web_routes_ui.h"
#include "web_routes_wifi.h"
#include "web_routes_nvs.h"
#include "web_routes_system.h"
#include "app_context.h"
#include "app_tasks.h"
#include "realtime_websocket.h"
#include "asset_bundler.h"
#include "login_throttle.h"  // anti-brute-force POST /api/login (logique pure testable)

 
extern Automatism g_autoCtrl;
extern Mailer mailer;
extern ConfigManager config;
extern PowerManager power;
extern WifiManager wifi;

static bool s_dbvarsCacheInvalid = false;
void invalidateDbvarsCache() { s_dbvarsCacheInvalid = true; }

/// Flag pour redémarrage ESP après envoi de la réponse (évite reset avant envoi HTTP)
static bool s_pendingRestart = false;

// Politique reset: tenter une OTA si disponible, puis reset classique sinon.
static bool tryStartOtaBeforeReset(const char* sourceTag) {
  extern AppContext g_appContext;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[Reset] %s: WiFi non connecté, reset direct\n", sourceTag);
    return false;
  }

  if (g_appContext.otaManager.isUpdating()) {
    Serial.printf("[Reset] %s: OTA déjà en cours, reset différé\n", sourceTag);
    return true;
  }

  g_appContext.otaManager.setCurrentVersion(ProjectConfig::VERSION);
  if (!g_appContext.otaManager.checkForUpdate()) {
    Serial.printf("[Reset] %s: aucune OTA disponible, reset direct\n", sourceTag);
    return false;
  }

  if (g_appContext.otaManager.performUpdate()) {
    Serial.printf("[Reset] %s: OTA disponible, mise à jour lancée avant reset\n", sourceTag);
    return true;
  }

  Serial.printf("[Reset] %s: OTA détectée mais échec démarrage update, reset direct\n", sourceTag);
  return false;
}

#ifndef DISABLE_ASYNC_WEBSERVER
// Authentification web locale : token session (perdu au reboot)
//
// v13.52 (audit sécurité 2026-05) :
// - Entropie : esp_fill_random (HW RNG) au lieu de random() + randomSeed (entropie faible).
// - Cookie : flags HttpOnly + SameSite=Strict + Max-Age (TTL 24 h) pour limiter XSS et CSRF.
// - Rotation : token régénéré à chaque /api/login réussi (et plus seulement si vide).
// - TTL côté firmware : token invalidé après WEB_AUTH_TOKEN_TTL_MS sans /api/login.
static char s_webAuthToken[WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN + 1] = {0};
static unsigned long s_webAuthTokenExpiresAt = 0;  // 0 = pas de session active
static constexpr unsigned long WEB_AUTH_TOKEN_TTL_MS = 24UL * 60UL * 60UL * 1000UL;  // 24 h

// Anti-brute-force POST /api/login (audit sécurité 2026-06) : machine d'état pure
// FAIL-SAFE (login_throttle.h). RAM seule (réinitialisée au reboot, comme le token).
// Politique par défaut : 5 échecs dans une fenêtre de 30 s -> verrou de 60 s.
static LoginThrottle::Throttle<> s_loginThrottle;

static bool isAuthenticated(AsyncWebServerRequest* req) {
  if (s_webAuthToken[0] == '\0') return false;
  // v13.52: vérifier expiration session (24 h sans login = forcer reconnexion)
  if (s_webAuthTokenExpiresAt > 0 && (long)(millis() - s_webAuthTokenExpiresAt) >= 0) {
    s_webAuthToken[0] = '\0';
    s_webAuthTokenExpiresAt = 0;
    return false;
  }
  const AsyncWebHeader* h = req->getHeader("Cookie");
  if (!h || !h->value().length()) return false;
  const char* cookie = h->value().c_str();
  const char* prefix = "ffp5cs_auth=";
  const size_t prefixLen = 11;
  const char* p = strstr(cookie, prefix);
  if (!p) return false;
  p += prefixLen;
  size_t i = 0;
  while (i < WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN && p[i] != '\0' && p[i] != ';' && p[i] != ' ') {
    if (p[i] != s_webAuthToken[i]) return false;
    i++;
  }
  return (i == WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN);
}

static void sendAuthRequired(AsyncWebServerRequest* req) {
  req->send(NetworkConfig::HTTP_UNAUTHORIZED, "application/json", "{\"ok\":false}");
}

bool webAuthIsAuthenticated(AsyncWebServerRequest* req) {
  return isAuthenticated(req);
}
void webAuthSendRequired(AsyncWebServerRequest* req) {
  sendAuthRequired(req);
}

// v13.52 (audit sécurité): variante utilisée par le WebSocket port 81 — pas d'accès aux
// AsyncWebHeader, on compare directement le token reçu en payload `{"type":"auth","token":...}`.
bool webAuthIsTokenValid(const char* token) {
  if (!token || s_webAuthToken[0] == '\0') return false;
  if (s_webAuthTokenExpiresAt > 0 && (long)(millis() - s_webAuthTokenExpiresAt) >= 0) return false;
  if (strlen(token) != WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN) return false;
  return strncmp(token, s_webAuthToken, WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN) == 0;
}

// v13.52: HW RNG (esp_fill_random) au lieu de random() — entropie cryptographique correcte.
// Rotation systématique du token + (re)mise à jour TTL.
static void generateWebAuthToken() {
  uint8_t raw[16];
  esp_fill_random(raw, sizeof(raw));
  for (size_t i = 0; i < sizeof(raw); i++) {
    snprintf(s_webAuthToken + (i * 2), 3, "%02x", (unsigned)raw[i]);
  }
  s_webAuthToken[WebAuthConfig::WEB_AUTH_TOKEN_HEX_LEN] = '\0';
  s_webAuthTokenExpiresAt = millis() + WEB_AUTH_TOKEN_TTL_MS;
}

// Public (déclaré dans web_routes_status.h) : utilisé aussi par web_routes_wifi.cpp.
bool getWebParam(AsyncWebServerRequest* req, const char* name, char* buf, size_t bufSize, bool post) {
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
  out["angleReposGros"] = getIntCanonical("angleReposGros", nullptr, GPIODefaults::SERVO_REST_ANGLE);
  out["angleDistribGros"] = getIntCanonical("angleDistribGros", nullptr, GPIODefaults::SERVO_FEED_ANGLE);
  out["angleInterGros"] = getIntCanonical("angleInterGros", nullptr, GPIODefaults::SERVO_INTER_ANGLE);
  out["angleReposPetits"] = getIntCanonical("angleReposPetits", nullptr, GPIODefaults::SERVO_REST_ANGLE);
  out["angleDistribPetits"] = getIntCanonical("angleDistribPetits", nullptr, GPIODefaults::SERVO_FEED_ANGLE);
  out["angleInterPetits"] = getIntCanonical("angleInterPetits", nullptr, GPIODefaults::SERVO_INTER_ANGLE);
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

// v14.02: sendManualActionEmail supprimé. L'email de nourrissage manuel est désormais
// géré par Automatism::triggerLocalManualFeed (politique/format unifiés avec le distant).

// canCreateAsyncTask() et s_lastWifiConnectAt déplacés dans web_routes_wifi.cpp (audit v13.93).

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
  // AUTHENTIFICATION WEB LOCALE (onglets protégés : admin / ffp3)
  // ============================================================================
  _server->on("/api/auth/check", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (isAuthenticated(req)) {
      req->send(NetworkConfig::HTTP_OK, "application/json", "{\"ok\":true}");
    } else {
      req->send(NetworkConfig::HTTP_UNAUTHORIZED, "application/json", "{\"ok\":false}");
    }
  });

  _server->addHandler(new AsyncCallbackJsonWebHandler("/api/login", [](AsyncWebServerRequest* req, JsonVariant& json) {
    // Anti-brute-force (audit sécurité 2026-06) : si trop d'échecs récents rapprochés,
    // refuser 429 SANS comparer les identifiants (ralentit le brute-force LAN).
    if (s_loginThrottle.isLockedOut(millis())) {
      AsyncWebServerResponse* resp = req->beginResponse(429, "application/json",
                                                        "{\"ok\":false,\"error\":\"locked\"}");
      // Retry-After (secondes, arrondi au supérieur) : indication standard au client.
      char retryAfter[8];
      uint32_t remMs = s_loginThrottle.remainingLockoutMs(millis());
      snprintf(retryAfter, sizeof(retryAfter), "%lu", (unsigned long)((remMs + 999UL) / 1000UL));
      resp->addHeader("Retry-After", retryAfter);
      req->send(resp);
      return;
    }
    const char* user = json["user"].as<const char*>();
    const char* pass = json["pass"].as<const char*>();
    if (!user) user = "";
    if (!pass) pass = "";
    if (strcmp(user, WebAuthConfig::WEB_AUTH_USER) != 0 || strcmp(pass, WebAuthConfig::WEB_AUTH_PASS) != 0) {
      // Échec d'identifiants : enregistrer pour le throttle (peut armer le verrou).
      s_loginThrottle.registerFailure(millis());
      req->send(NetworkConfig::HTTP_UNAUTHORIZED, "application/json", "{\"ok\":false,\"error\":\"invalid\"}");
      return;
    }
    // Succès : remettre le throttle à zéro (l'utilisateur légitime repart à neuf).
    s_loginThrottle.registerSuccess();
    // v13.52: rotation systématique du token à chaque login (limite vol par session ancien token).
    generateWebAuthToken();
    // v13.52: le token est aussi renvoyé dans le body JSON pour usage côté WebSocket
    // (le cookie HttpOnly ne peut pas être lu par JS, donc le client stocke `wsToken`
    // en sessionStorage et l'envoie via `{"type":"auth","token":...}` au handshake WS).
    char body[96];
    snprintf(body, sizeof(body), "{\"ok\":true,\"wsToken\":\"%s\"}", s_webAuthToken);
    AsyncWebServerResponse* response = req->beginResponse(NetworkConfig::HTTP_OK, "application/json", body);
    char cookieVal[160];
    // v13.52: HttpOnly (anti XSS), SameSite=Strict (anti CSRF), Max-Age 86400 s (24 h).
    // Pas de flag `Secure` car le firmware sert HTTP en LAN ; à activer si HTTPS local un jour.
    snprintf(cookieVal, sizeof(cookieVal),
             "ffp5cs_auth=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400",
             s_webAuthToken);
    response->addHeader("Set-Cookie", cookieVal);
    req->send(response);
  }));

  _server->on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
    // v13.52: invalider le token côté firmware ET demander suppression du cookie.
    s_webAuthToken[0] = '\0';
    s_webAuthTokenExpiresAt = 0;
    AsyncWebServerResponse* response = req->beginResponse(NetworkConfig::HTTP_OK, "application/json", "{\"ok\":true}");
    response->addHeader("Set-Cookie", "ffp5cs_auth=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    req->send(response);
  });

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
    if (!isAuthenticated(req)) { sendAuthRequired(req); return; }
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
      sendErrorResponse(req, 400, "Invalid type parameter. Must be 'small' or 'big'");
      return;
    }
    
    // v14.02: politique unique via Automatism::triggerLocalManualFeed (garde, edge,
    // trace serveur, email) — identique pour /action, /api/feed et /api/status.
    const bool isBig = (strcmp(typeBuf, "big") == 0);
    SensorReadings readings{};
    _sensors.getLastCachedReadings(readings);  // Pas de read() bloquant dans webTask
    if (g_autoCtrl.triggerLocalManualFeed(isBig, readings) == Automatism::ManualFeedResult::Busy) {
      req->send(409, "application/json", "{\"success\":false,\"error\":\"FEED_BUSY\",\"message\":\"Nourrissage en cours\"}");
      return;
    }
    req->send(NetworkConfig::HTTP_OK, "application/json",
              isBig ? "{\"success\":true,\"action\":\"feedBig\"}"
                    : "{\"success\":true,\"action\":\"feedSmall\"}");
    g_realtimeWebSocket.broadcastNow();
  });

  // /action endpoint for remote controls - OPTIMISÉ POUR RÉACTIVITÉ
  _server->on("/action", HTTP_GET, [this, &ctx](AsyncWebServerRequest* req){
      if (!isAuthenticated(req)) { sendAuthRequired(req); return; }
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
          
          if (strcmp(c, "feedSmall") == 0 || strcmp(c, "feedBig") == 0) {
              // v14.02: politique unique via Automatism::triggerLocalManualFeed
              // (garde anti-cycle, edge anti-écho, trace serveur, email).
              const bool isBig = (strcmp(c, "feedBig") == 0);
              SensorReadings readings{};
              _sensors.getLastCachedReadings(readings);  // Pas de read() bloquant dans webTask
              if (g_autoCtrl.triggerLocalManualFeed(isBig, readings) == Automatism::ManualFeedResult::Busy) {
                  resp = "FEED_BUSY";
                  Serial.println(isBig ? "[Web] ⚠️ Nourrissage gros refusé - cycle en cours"
                                       : "[Web] ⚠️ Nourrissage petits refusé - cycle en cours");
              } else {
                  g_realtimeWebSocket.broadcastNow();
                  resp = isBig ? "FEED_BIG OK" : "FEED_SMALL OK";
                  Serial.println(isBig ? "[Web] ✅ Big feed triggered" : "[Web] ✅ Small feed triggered");
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
              Serial.println("[Web] 🔄 Reset ESP demandé");
              g_realtimeWebSocket.broadcastNow();
              if (tryStartOtaBeforeReset("web-local")) {
                  resp = "OTA BEFORE RESET";
              } else {
                  resp = "RESET OK";
                  s_pendingRestart = true;  // Redémarrer après envoi de la réponse
              }
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
        // v14.17 : reboot délibéré → valider une éventuelle image OTA en probation pour
        // ne pas rollbacker une bonne image fraîchement flashée (le reboot manuel ne doit
        // pas être interprété comme un échec de l'image).
        SystemBoot::confirmOtaValidation("reboot manuel (web)");
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
    if (!isAuthenticated(req)) { sendAuthRequired(req); return; }
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

  // v13.60 (audit sécurité): /dbvars est same-origin (UI servie par le firmware lui-même).
  // Le préflight CORS OPTIONS et les en-têtes Access-Control-* sont retirés (étaient à "*",
  // permettant à n'importe quel site externe sur le LAN de lire la configuration).
  // Garder la route OPTIONS pour répondre 204 si un client envoie un preflight, mais sans
  // accorder de permission cross-origin.
  _server->on("/dbvars", HTTP_OPTIONS, [](AsyncWebServerRequest* req){
    req->send(NetworkConfig::HTTP_NO_CONTENT);
  });

  // /dbvars endpoint : expose variables fetched from remote server - OPTIMISÉ
  _server->on("/dbvars", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!isAuthenticated(req)) { sendAuthRequired(req); return; }
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
    // v13.60 (audit sécurité): pas de CORS - même origine que l'UI (route /dbvars).
    serializeJson(doc, *response);
    req->send(response);
  });

  // Mise à jour des variables distantes locales et envoi vers la BDD distante
  // Flux serveur → NVS → logique locale (plan simplification):
  // 1. Réception paramètres 2. Validation (clés connues) 3. Écriture NVS via config.saveRemoteVars
  // 4. Mise à jour RAM via applyConfigFromJson 5. Sync distant optionnelle (non bloquante)
  // Handler partagé pour POST /dbvars/update et POST /api/config (alias contractuel, pas de redirect pour conserver le body)
  auto handleDbVarsUpdate = [this, &ctx](AsyncWebServerRequest* req) {
    if (!isAuthenticated(req)) { sendAuthRequired(req); return; }
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
    if (getWebParam(req, "angleReposGros", paramBuf, sizeof(paramBuf))) appendPair("angleReposGros", paramBuf);
    if (getWebParam(req, "angleDistribGros", paramBuf, sizeof(paramBuf))) appendPair("angleDistribGros", paramBuf);
    if (getWebParam(req, "angleInterGros", paramBuf, sizeof(paramBuf))) appendPair("angleInterGros", paramBuf);
    if (getWebParam(req, "angleReposPetits", paramBuf, sizeof(paramBuf))) appendPair("angleReposPetits", paramBuf);
    if (getWebParam(req, "angleDistribPetits", paramBuf, sizeof(paramBuf))) appendPair("angleDistribPetits", paramBuf);
    if (getWebParam(req, "angleInterPetits", paramBuf, sizeof(paramBuf))) appendPair("angleInterPetits", paramBuf);
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
  WebRoutes::registerSystemRoutes(*_server, ctx);

  // -------------------------------------------------------------------
  // NVS Inspector: lister, modifier, effacer les variables persistantes
  // -------------------------------------------------------------------
  WebRoutes::registerNvsRoutes(*_server, ctx);

  // ========================================
  // GESTIONNAIRE WIFI - ENDPOINTS BACKEND
  // ========================================
  
  WebRoutes::registerWifiRoutes(*_server, ctx);

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