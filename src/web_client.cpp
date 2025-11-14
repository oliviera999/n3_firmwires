#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "log.h"
#include "project_config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

// Simple CRC32 (polynôme 0xEDB88320) pour l'intégrité des payloads
extern ConfigManager config;
static uint32_t crc32(const char* data){
  uint32_t crc = 0xFFFFFFFF;
  while (*data) {
    crc ^= static_cast<uint8_t>(*data++);
    for (int k = 0; k < 8; k++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
    }
  }
  return ~crc;
}

WebClient::WebClient(const char* apiKey) : _apiKey(apiKey) {
  _client.setInsecure();               // accepte tous certificats (à affiner)
  _client.setHandshakeTimeout(12000);  // + de temps pour TLS
  // Désactivation du keep-alive : certaines déconnexions moitié-fermées
  // généraient un blocage interne du client HTTP, empêchant l'appel
  // suivant de se terminer et donc le rafraîchissement du watchdog.
  _http.setReuse(false);
  // NOUVEAU TIMEOUT NON-BLOQUANT (v11.50)
  _http.setTimeout(GlobalTimeouts::HTTP_MAX_MS); // Timeout strict pour éviter blocage
}

bool WebClient::httpRequest(const String& url, const String& payload, String& response) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // NOUVEAU TIMEOUT GLOBAL NON-BLOQUANT (v11.50)
  const uint32_t GLOBAL_TIMEOUT_MS = GlobalTimeouts::HTTP_MAX_MS;
  uint32_t requestStartMs = millis();
  const bool debugLogging = (LOG_LEVEL >= LOG_DEBUG) && LogConfig::SERIAL_ENABLED;
  const bool verboseLogging = (LOG_LEVEL >= LOG_VERBOSE) && LogConfig::SERIAL_ENABLED;

  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] POST %s (payload=%u bytes, timeout=%u ms)",
        url.c_str(), payload.length(), GLOBAL_TIMEOUT_MS);
    LOG(LOG_DEBUG, "[HTTP] WiFi status=%d connected=%s RSSI=%d dBm",
        WiFi.status(), WiFi.isConnected() ? "YES" : "NO", WiFi.RSSI());
    LOG(LOG_DEBUG, "[HTTP] IP=%s gateway=%s dns=%s",
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(),
        WiFi.dnsIP().toString().c_str());
    LOG(LOG_DEBUG, "[HTTP] Heap libre=%u bytes (min=%u)", ESP.getFreeHeap(), ESP.getMinFreeHeap());

    if (verboseLogging && payload.length() > 0) {
      const size_t previewLen = payload.length() > 200 ? 200 : payload.length();
      LOG(LOG_VERBOSE, "[HTTP] Payload (%u/%u): %s%s",
          previewLen,
          payload.length(),
          payload.substring(0, previewLen).c_str(),
          payload.length() > previewLen ? " ..." : "");
    }
  }
  
  // Fix v11.29: Délai minimum entre requêtes HTTP pour éviter saturation TCP
  if (_lastRequestMs > 0) {
    unsigned long timeSinceLastRequest = millis() - _lastRequestMs;
    if (timeSinceLastRequest < ServerConfig::MIN_DELAY_BETWEEN_REQUESTS_MS) {
      uint32_t delayNeeded = ServerConfig::MIN_DELAY_BETWEEN_REQUESTS_MS - timeSinceLastRequest;
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Délai inter-requêtes %u ms", delayNeeded);
      }
      vTaskDelay(pdMS_TO_TICKS(delayNeeded));
    }
  }
  
  // Désactiver le modem sleep juste avant un transfert pour fiabilité
  WiFi.setSleep(false);
  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] Modem sleep désactivé pendant le transfert");
  }

  // Choix du client selon le schéma (HTTP = non-TLS / HTTPS = TLS)
  bool secure = url.startsWith("https://");
  WiFiClient plain; // client non-TLS (portée limitée à la fonction)

  // Politique de retry exponentiel simple
  const int maxAttempts = 3;
  int attempt = 0;
  int code = -1;
  response = "";
  
  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] Boucle de retry max=%d", maxAttempts);
  }
  
  while (attempt < maxAttempts) {
    unsigned long attemptStartMs = millis();
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] Tentative %d/%d", attempt + 1, maxAttempts);
    }
    
    // Ré-initialise la requête
    if (secure) {
      _http.begin(_client, url);
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Client HTTPS utilisé");
      }
    } else {
      _http.begin(plain, url);
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Client HTTP simple utilisé");
      }
    }
    
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] Headers positionnés, timeout requête=%u", ServerConfig::REQUEST_TIMEOUT_MS);
    }
    
    // NOUVEAU TIMEOUT GLOBAL NON-BLOQUANT (v11.50)
    uint32_t elapsedMs = millis() - requestStartMs;
    if (elapsedMs >= GLOBAL_TIMEOUT_MS) {
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u/%u ms", elapsedMs, GLOBAL_TIMEOUT_MS);
      _http.end();
      return false;
    }
    
    // Log avant envoi POST
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] POST en cours (elapsed=%lu ms)", attemptStartMs - requestStartMs);
    }
    code = _http.POST(payload);
    unsigned long postDurationMs = millis() - attemptStartMs;
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] POST terminé en %lu ms", postDurationMs);
    }
    if (code > 0) {
      unsigned long responseStartMs = millis();
      response = _http.getString();
      unsigned long responseDurationMs = millis() - responseStartMs;
      
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Réponse reçue code=%d bytes=%u en %lu ms", code, response.length(), responseDurationMs);
      }

      if (verboseLogging) {
        LOG(LOG_VERBOSE, "[HTTP] Content-Type=%s", _http.header("Content-Type").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Server=%s", _http.header("Server").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Connection=%s", _http.header("Connection").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Content-Length=%s", _http.header("Content-Length").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Transfer-Encoding=%s", _http.header("Transfer-Encoding").c_str());

        if (response.length() > 0) {
          const size_t previewLen = response.length() > 200 ? 200 : response.length();
          LOG(LOG_VERBOSE, "[HTTP] Response (%u/%u): %s%s",
              previewLen,
              response.length(),
              response.substring(0, previewLen).c_str(),
              response.length() > previewLen ? " ..." : "");
        }
      }

      if (response.length() == 0) {
        LOG(LOG_WARN, "[HTTP] Réponse vide reçue (code=%d)", code);
      } else if (code >= 400) {
        LOG(LOG_WARN, "[HTTP] Réponse erreur %d", code);
      }
    } else {
      unsigned long errorTimeMs = millis();
      LOG(LOG_WARN, "[HTTP] Erreur %d (tentative %d/%d, %lu ms) : %s",
          code,
          attempt + 1,
          maxAttempts,
          errorTimeMs - requestStartMs,
          _http.errorToString(code).c_str());
      LOG(LOG_WARN, "[HTTP] URL=%s", url.c_str());
      LOG(LOG_WARN, "[HTTP] WiFi status=%d connected=%s RSSI=%d dBm", WiFi.status(), WiFi.isConnected() ? "YES" : "NO", WiFi.RSSI());
      LOG(LOG_WARN, "[HTTP] Heap libre=%u", ESP.getFreeHeap());

      if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: connection refused");
      } else if (code == HTTPC_ERROR_CONNECTION_LOST) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: connection lost");
      } else if (code == HTTPC_ERROR_NO_STREAM) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: no stream");
      } else if (code == HTTPC_ERROR_NO_HTTP_SERVER) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: no HTTP server");
      } else if (code == HTTPC_ERROR_TOO_LESS_RAM) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: insufficient RAM");
      } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
        LOG(LOG_WARN, "[HTTP] Diagnostic: read timeout");
      }
    }
    _http.end();
    
    // === STATISTIQUES DE LA TENTATIVE ===
    unsigned long attemptDurationMs = millis() - attemptStartMs;
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] Tentative %d/%d terminée en %lu ms", attempt + 1, maxAttempts, attemptDurationMs);
    }
    
    // Record into diagnostics if available
    extern Diagnostics diag;
    if (code > 0) {
      diag.recordHttpResult(code >= 200 && code < 400, code);
    } else {
      diag.recordHttpResult(false, code);
    }

    // Succès 2xx-3xx : sortir immédiatement
    if (code >= 200 && code < 400) {
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Succès %d, arrêt des tentatives", code);
      }
      break;
    }
    
    // Erreur client 4xx : pas de retry (v11.31)
    if (code >= 400 && code < 500) {
      LOG(LOG_WARN, "[HTTP] Erreur client %d, arrêt des retry", code);
      break;
    }
    
    // Court-circuit si plus de WiFi
    if (WiFi.status() != WL_CONNECTED) {
      LOG(LOG_WARN, "[HTTP] WiFi déconnecté, arrêt des tentatives");
      break;
    }
    
    // Incrémenter attempt avant backoff
    attempt++;
    
    // Backoff exponentiel si retry possible (v11.31 amélioré)
    if (attempt < maxAttempts) {
      int backoff = NetworkConfig::BACKOFF_BASE_MS * attempt * attempt;
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Retry %d/%d dans %d ms", attempt + 1, maxAttempts, backoff);
        LOG(LOG_DEBUG, "[HTTP] Avant retry: WiFi=%d RSSI=%d Heap=%u",
            WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap());
      }
      
      vTaskDelay(pdMS_TO_TICKS(backoff));
      
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Après retry: WiFi=%d RSSI=%d Heap=%u",
            WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap());
      }
      
      // Note: Watchdog géré par tâche appelante - vTaskDelay() yield le CPU
      // Ne pas appeler esp_task_wdt_reset() ici (erreur "task not found")
    }
  }

  // === RÉSUMÉ FINAL DE LA REQUÊTE ===
  unsigned long totalDurationMs = millis() - requestStartMs;
  if (debugLogging || code >= 400) {
    LOG(LOG_INFO, "[HTTP] Fin requête: durée=%lu ms, tentatives=%d/%d, code=%d, succès=%s, réponse=%u bytes, heap=%u",
        totalDurationMs,
        attempt + 1,
        maxAttempts,
        code,
        (code >= 200 && code < 400) ? "oui" : "non",
        response.length(),
        ESP.getFreeHeap());
  }
  
  // Fix v11.29: Sauvegarder timestamp pour délai inter-requêtes
  _lastRequestMs = millis();
  
  // Ne pas réactiver le modem-sleep
  WiFi.setSleep(false);
  return (code >= 200 && code < 400);
}

bool WebClient::sendMeasurements(const Measurements& m, bool includeReset) {
  // === LOGS DÉTAILLÉS SENDMEASUREMENTS v11.32 ===
  unsigned long smStartMs = millis();
  Serial.println(F("=== DÉBUT SENDMEASUREMENTS ==="));
  Serial.printf("[SM] Timestamp: %lu ms\n", smStartMs);
  Serial.printf("[SM] Include reset: %s\n", includeReset ? "OUI" : "NON");
  
  // VALIDATION COMPLÈTE DES MESURES AVANT ENVOI
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humid;

  // Logs des valeurs brutes avant validation
  Serial.printf("[SM] Valeurs brutes - TempEau: %.1f°C, TempAir: %.1f°C, Humidité: %.1f%%\n", 
               tempWater, tempAir, humidity);
  Serial.printf("[SM] Valeurs brutes - EauPotager: %u, EauAquarium: %u, EauReserve: %u\n", 
               m.wlPota, m.wlAqua, m.wlTank);
  Serial.printf("[SM] Valeurs brutes - DiffMaree: %d, Luminosité: %u\n", 
               m.diffMaree, m.luminosite);
  Serial.printf("[SM] États actionneurs - PompeAqua: %s, PompeTank: %s, Heat: %s, UV: %s\n",
               m.pumpAqua ? "ON" : "OFF", m.pumpTank ? "ON" : "OFF", 
               m.heater ? "ON" : "OFF", m.light ? "ON" : "OFF");

  // Validation des températures (rejette 0°C car physiquement impossible)
  if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
    Serial.printf("[SM] ⚠️ Température eau invalide avant envoi: %.1f°C, force NaN\n", tempWater);
    tempWater = NAN;
  }

  if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
    Serial.printf("[SM] ⚠️ Température air invalide avant envoi: %.1f°C, force NaN\n", tempAir);
    tempAir = NAN;
  }

  // Validation de l'humidité
  if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
    Serial.printf("[SM] ⚠️ Humidité invalide avant envoi: %.1f%%, force NaN\n", humidity);
    humidity = NAN;
  }

  // Logs des valeurs après validation
  Serial.printf("[SM] Valeurs validées - TempEau: %s, TempAir: %s, Humidité: %s\n",
               isnan(tempWater) ? "NaN" : String(tempWater).c_str(),
               isnan(tempAir) ? "NaN" : String(tempAir).c_str(),
               isnan(humidity) ? "NaN" : String(humidity).c_str());

  // Construction d'un payload COMPLET et ORDONNÉ selon la liste attendue par le serveur
  auto fmtFloat = [](float v) -> String {
    if (isnan(v)) return String("");
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%.1f", v);
    return String(buf, n);
  };
  // Pour les ultrasons: 0 signifie invalide → envoyer champ vide ""
  auto fmtUltrasonic = [](uint16_t v) -> String {
    if (v == 0) return String("");
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%u", (unsigned)v);
    return String(buf, n);
  };

  String payload;
  payload.reserve(256);
  auto appendKV = [&](const char* key, const String& value) {
    if (payload.length()) payload += "&";
    payload += key;
    payload += "=";
    payload += value;
  };

  Serial.println(F("[SM] Construction du payload..."));
  
  // Ordre exact aligné sur la liste utilisée côté serveur
  appendKV("version", String(Config::VERSION));
  appendKV("TempAir", fmtFloat(tempAir));
  appendKV("Humidite", fmtFloat(humidity));
  appendKV("TempEau", fmtFloat(tempWater));
  appendKV("EauPotager", fmtUltrasonic(m.wlPota));
  appendKV("EauAquarium", fmtUltrasonic(m.wlAqua));
  appendKV("EauReserve", fmtUltrasonic(m.wlTank));
  appendKV("diffMaree", String(m.diffMaree));
  appendKV("Luminosite", String(m.luminosite));
  appendKV("etatPompeAqua", String(m.pumpAqua ? 1 : 0));
  appendKV("etatPompeTank", String(m.pumpTank ? 1 : 0));
  appendKV("etatHeat", String(m.heater ? 1 : 0));
  appendKV("etatUV", String(m.light ? 1 : 0));
  // Champs non mesurés ici: valeurs vides (conforme exigence de complétude)
  appendKV("bouffeMatin", String(""));
  appendKV("bouffeMidi", String(""));
  appendKV("bouffeSoir", String(""));
  appendKV("bouffePetits", String(""));
  appendKV("bouffeGros", String(""));
  appendKV("tempsGros", String(""));
  appendKV("tempsPetits", String(""));
  // Seuils par défaut issus de la configuration centrale
  appendKV("aqThreshold", String(ActuatorConfig::Default::AQUA_LEVEL_CM));
  appendKV("tankThreshold", String(ActuatorConfig::Default::TANK_LEVEL_CM));
  appendKV("chauffageThreshold", String((int)ActuatorConfig::Default::HEATER_THRESHOLD_C));
  // Préférences/flags divers (laisser vide si non gérés localement)
  appendKV("mail", String(""));
  appendKV("mailNotif", String(""));
  appendKV("resetMode", includeReset ? String(0) : String(""));
  appendKV("tempsRemplissageSec", String(""));
  appendKV("limFlood", String(""));
  appendKV("WakeUp", String(""));
  appendKV("FreqWakeUp", String(""));

  Serial.printf("[SM] Payload construit: %u bytes\n", payload.length());
  Serial.printf("[SM] Version: %s\n", Config::VERSION);
  Serial.printf("[SM] Seuils - Aqua: %u, Tank: %u, Heat: %.1f°C\n", 
               ActuatorConfig::Default::AQUA_LEVEL_CM, 
               ActuatorConfig::Default::TANK_LEVEL_CM,
               ActuatorConfig::Default::HEATER_THRESHOLD_C);

  LOG(LOG_DEBUG, "POST %s", payload.c_str());
  
  // Envoi direct: l'ordre exact est déjà respecté
  bool success = postRaw(payload);
  
  unsigned long smDurationMs = millis() - smStartMs;
  Serial.printf("[SM] === FIN SENDMEASUREMENTS ===\n");
  Serial.printf("[SM] Durée totale: %lu ms\n", smDurationMs);
  Serial.printf("[SM] Succès: %s\n", success ? "OUI" : "NON");
  Serial.printf("[SM] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("==============================="));
  
  return success;
}

bool WebClient::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
  if (!config.isRemoteRecvEnabled()) {
    Serial.println(F("[GET] ⛔ Réception distante désactivée (config)"));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // === LOGS DÉTAILLÉS GET v11.32 ===
  unsigned long getStartMs = millis();
  Serial.println(F("=== DÉBUT REQUÊTE GET REMOTE STATE ==="));
  Serial.printf("[GET] Timestamp: %lu ms\n", getStartMs);
  
  // Utiliser l'URL complète depuis la configuration serveur
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));
  Serial.printf("[GET] URL: %s\n", url);
  
  // État réseau détaillé
  Serial.printf("[GET] WiFi Status: %d (connected=%s)\n", WiFi.status(), WiFi.isConnected() ? "YES" : "NO");
  Serial.printf("[GET] RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("[GET] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[GET] Heap before GET: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[GET] Free heap: %u bytes\n", ESP.getFreeHeap());
  
  // Sélectionne le bon type de client selon le schéma
  bool secure = strncmp(url, "https://", 8) == 0;
  WiFiClient plain; // client non-TLS local
  
  if (secure) { 
    _http.begin(_client, url); 
    Serial.println(F("[GET] 🔒 Using HTTPS client"));
  } else { 
    _http.begin(plain, url); 
    Serial.println(F("[GET] 🌐 Using HTTP client"));
  }
  
  Serial.printf("[GET] Sending GET request (timeout: %u ms)...\n", ServerConfig::REQUEST_TIMEOUT_MS);
  int code = _http.GET();
  unsigned long getDurationMs = millis() - getStartMs;
  Serial.printf("[GET] GET completed in %lu ms, HTTP code: %d\n", getDurationMs, code);
  Serial.printf("[GET] Heap after GET: %u bytes\n", ESP.getFreeHeap());
  
  if (code <= 0) {
    Serial.printf("[GET] ❌ ERROR %d: %s\n", code, _http.errorToString(code).c_str());
    Serial.printf("[GET] WiFi status at error: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
    _http.end();
    return false;
  }
  
  // Utiliser getString() qui gère automatiquement Transfer-Encoding: chunked
  unsigned long responseStartMs = millis();
  String payload = _http.getString();
  unsigned long responseDurationMs = millis() - responseStartMs;
  _http.end();
  
  Serial.printf("[GET] Response received in %lu ms, size: %u bytes\n", responseDurationMs, payload.length());
  Serial.printf("[GET] Heap after readString: %u bytes\n", ESP.getFreeHeap());
  
  if (payload.length() == 0) {
    Serial.println(F("[GET] ⚠️ Empty response from server"));
    return false;
  }
  
  // Log détaillé de la réponse
  Serial.println(F("[GET] === RÉPONSE REMOTE STATE ==="));
  if (payload.length() <= 600) {
    Serial.printf("[GET] %s\n", payload.c_str());
  } else {
    Serial.printf("[GET] %s ... (truncated)\n", payload.substring(0,600).c_str());
    Serial.printf("[GET] ... (%u bytes restants)\n", payload.length() - 600);
  }
  Serial.println(F("[GET] === FIN RÉPONSE ==="));
  
  // Parser le JSON depuis le String
  unsigned long parseStartMs = millis();
  DeserializationError err = deserializeJson(doc, payload);
  unsigned long parseDurationMs = millis() - parseStartMs;
  
  if (err) {
    Serial.printf("[GET] ❌ JSON parse error: %s (parsing took %lu ms)\n", err.c_str(), parseDurationMs);
    Serial.printf("[GET] Payload preview (first 200 chars): %.200s\n", payload.c_str());
    return false;
  }
  
  Serial.printf("[GET] ✓ Remote JSON parsed successfully in %lu ms\n", parseDurationMs);
  
  // Analyse du contenu JSON
  if (doc.is<JsonObject>()) {
    JsonObject obj = doc.as<JsonObject>();
    Serial.printf("[GET] JSON object with %d keys\n", obj.size());
    
    // Log des clés principales pour debugging
    if (obj.containsKey("version")) {
      Serial.printf("[GET] Server version: %s\n", obj["version"].as<const char*>());
    }
    if (obj.containsKey("timestamp")) {
      Serial.printf("[GET] Server timestamp: %s\n", obj["timestamp"].as<const char*>());
    }
    if (obj.containsKey("status")) {
      Serial.printf("[GET] Server status: %s\n", obj["status"].as<const char*>());
    }
  }
  
  unsigned long totalDurationMs = millis() - getStartMs;
  Serial.printf("[GET] === FIN REQUÊTE GET ===\n");
  Serial.printf("[GET] Durée totale: %lu ms\n", totalDurationMs);
  Serial.printf("[GET] Succès: OUI\n");
  Serial.printf("[GET] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("==============================="));
  
  return true;
}

bool WebClient::sendHeartbeat(const Diagnostics& diag) {
  if (!config.isRemoteSendEnabled()) {
    Serial.println(F("[HB] ⛔ Envoi heartbeat désactivé (config)"));
    return false;
  }
  // === LOGS DÉTAILLÉS HEARTBEAT v11.32 ===
  unsigned long hbStartMs = millis();
  Serial.println(F("=== DÉBUT HEARTBEAT ==="));
  Serial.printf("[HB] Timestamp: %lu ms\n", hbStartMs);
  
  String payload;
  payload.reserve(128);
  const DiagnosticStats& s = diag.getStats();
  
  // Construction du payload avec logs détaillés
  payload = String("uptime=") + s.uptimeSec + "&free=" + s.freeHeap + "&min=" + s.minFreeHeap + "&reboots=" + s.rebootCount;
  Serial.printf("[HB] Payload avant CRC: %s\n", payload.c_str());
  
  char bufCrc2[15];
  snprintf(bufCrc2,sizeof(bufCrc2),"&crc=%08lX",crc32(payload.c_str()));
  String pay2 = payload + bufCrc2;
  
  Serial.printf("[HB] Payload final: %s\n", pay2.c_str());
  Serial.printf("[HB] Taille payload: %u bytes\n", pay2.length());
  
  // État système au moment du heartbeat
  Serial.printf("[HB] Uptime: %lu sec\n", s.uptimeSec);
  Serial.printf("[HB] Free heap: %u bytes\n", s.freeHeap);
  Serial.printf("[HB] Min free heap: %u bytes\n", s.minFreeHeap);
  Serial.printf("[HB] Reboot count: %u\n", s.rebootCount);
  Serial.printf("[HB] WiFi status: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
  
  String resp;
  char heartbeatUrl[256];
  ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
  bool success = httpRequest(heartbeatUrl, pay2, resp);
  
  unsigned long hbDurationMs = millis() - hbStartMs;
  Serial.printf("[HB] === FIN HEARTBEAT ===\n");
  Serial.printf("[HB] Durée totale: %lu ms\n", hbDurationMs);
  Serial.printf("[HB] Succès: %s\n", success ? "OUI" : "NON");
  Serial.printf("[HB] Réponse: %s\n", resp.c_str());
  Serial.println(F("========================="));
  
  return success;
}

// v11.70: makeSkeleton() supprimé - jamais utilisé (includeSkeleton toujours false)

bool WebClient::postRaw(const String& payload){
  if (!config.isRemoteSendEnabled()) {
    Serial.println(F("[PR] ⛔ Envoi distant désactivé (config) - SKIP"));
    return false;
  }
  // === LOGS DÉTAILLÉS POSTRAW v11.70 ===
  unsigned long prStartMs = millis();
  Serial.println(F("=== DÉBUT POSTRAW ==="));
  Serial.printf("[PR] Timestamp: %lu ms\n", prStartMs);
  Serial.printf("[PR] Payload input: %u bytes\n", payload.length());
  
  String full = payload;

  // Ajoute api_key et sensor si absents
  bool hasApi = payload.startsWith("api_key=");
  Serial.printf("[PR] Has API key: %s\n", hasApi ? "OUI" : "NON");
  
  if (!hasApi) {
    // v11.70: Simplification - pas de skeleton, construction directe
    full = String("api_key=") + _apiKey + "&sensor=" + Config::SENSOR;
    if (payload.length()) {
      full += "&";
      full += payload;
    }
    Serial.printf("[PR] Full payload constructed: %u bytes\n", full.length());
  } else {
    Serial.println(F("[PR] Using payload as-is (already has API key)"));
  }

  Serial.printf("[PR] Final payload size: %u bytes\n", full.length());
  Serial.printf("[PR] API Key: %s\n", _apiKey.c_str());
  Serial.printf("[PR] Sensor: %s\n", Config::SENSOR);

  String respPrimary;
  Serial.println(F("[PR] Sending to primary server..."));
  char postDataUrl[256];
  ServerConfig::getPostDataUrl(postDataUrl, sizeof(postDataUrl));
  bool okPrimary = httpRequest(postDataUrl, full, respPrimary);
  Serial.printf("[PR] Primary server result: %s\n", okPrimary ? "SUCCESS" : "FAILED");
  
  // Serveur secondaire supprimé (code mort v11.116)
  bool okSecondary = false;
  
  // Réussite si au moins un des serveurs a reçu l'update
  bool finalSuccess = okPrimary || okSecondary;
  
  unsigned long prDurationMs = millis() - prStartMs;
  Serial.printf("[PR] === FIN POSTRAW ===\n");
  Serial.printf("[PR] Durée totale: %lu ms\n", prDurationMs);
  Serial.printf("[PR] Primary: %s\n", okPrimary ? "SUCCESS" : "FAILED");
  Serial.printf("[PR] Secondary: %s\n", okSecondary ? "SUCCESS" : "FAILED");
  Serial.printf("[PR] Final result: %s\n", finalSuccess ? "SUCCESS" : "FAILED");
  Serial.printf("[PR] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("========================="));
  
  return finalSuccess;
}

// Fonction supprimée - redéfinition causait erreur de compilation 