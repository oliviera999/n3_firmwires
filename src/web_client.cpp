#include "web_client.h"
#include "diagnostics.h"
#include "log.h"
#include "project_config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

// Simple CRC32 (polynôme 0xEDB88320) pour l'intégrité des payloads
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
  // généraient un blocage interne du client HTTP, empêchant l’appel
  // suivant de se terminer et donc le rafraîchissement du watchdog.
  _http.setReuse(false);
  // Réduit le temps maximum d’attente global (connect + transfert)
  _http.setTimeout(ServerConfig::REQUEST_TIMEOUT_MS); // centralisé dans project_config.h
}

bool WebClient::httpRequest(const String& url, const String& payload, String& response) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // === LOGS DÉTAILLÉS DE DEBUGGING v11.32 ===
  unsigned long requestStartMs = millis();
  Serial.println(F("=== DÉBUT REQUÊTE HTTP ==="));
  Serial.printf("[HTTP] Timestamp: %lu ms\n", requestStartMs);
  Serial.printf("[HTTP] URL: %s\n", url.c_str());
  Serial.printf("[HTTP] Payload size: %u bytes\n", payload.length());
  
  // Logs détaillés du payload (toujours affiché pour debugging)
  Serial.println(F("[HTTP] === PAYLOAD COMPLET ==="));
  if (payload.length() <= 500) {
    Serial.printf("[HTTP] %s\n", payload.c_str());
  } else {
    Serial.printf("[HTTP] %s ... (truncated)\n", payload.substring(0,500).c_str());
    Serial.printf("[HTTP] ... (%u bytes restants)\n", payload.length() - 500);
  }
  Serial.println(F("[HTTP] === FIN PAYLOAD ==="));
  
  // État réseau détaillé
  Serial.printf("[HTTP] WiFi Status: %d (connected=%s)\n", WiFi.status(), WiFi.isConnected() ? "YES" : "NO");
  Serial.printf("[HTTP] RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("[HTTP] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[HTTP] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("[HTTP] DNS: %s\n", WiFi.dnsIP().toString().c_str());
  
  // Mémoire disponible
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  Serial.printf("[HTTP] Free heap: %u bytes (min: %u)\n", freeHeap, minFreeHeap);
  
  // Fix v11.29: Délai minimum entre requêtes HTTP pour éviter saturation TCP
  if (_lastRequestMs > 0) {
    unsigned long timeSinceLastRequest = millis() - _lastRequestMs;
    if (timeSinceLastRequest < ServerConfig::MIN_DELAY_BETWEEN_REQUESTS_MS) {
      uint32_t delayNeeded = ServerConfig::MIN_DELAY_BETWEEN_REQUESTS_MS - timeSinceLastRequest;
      Serial.printf("[HTTP] ⏱️ Délai inter-requêtes: %u ms\n", delayNeeded);
      vTaskDelay(pdMS_TO_TICKS(delayNeeded));
    }
  }
  
  // Désactiver le modem sleep juste avant un transfert pour fiabilité
  WiFi.setSleep(false);
  Serial.println(F("[HTTP] Modem sleep disabled for transfer"));

  // Choix du client selon le schéma (HTTP = non-TLS / HTTPS = TLS)
  bool secure = url.startsWith("https://");
  WiFiClient plain; // client non-TLS (portée limitée à la fonction)

  // Politique de retry exponentiel simple
  const int maxAttempts = 3;
  int attempt = 0;
  int code = -1;
  response = "";
  
  Serial.printf("[HTTP] Starting retry loop (max %d attempts)\n", maxAttempts);
  
  while (attempt < maxAttempts) {
    unsigned long attemptStartMs = millis();
    Serial.printf("[HTTP] === TENTATIVE %d/%d ===\n", attempt+1, maxAttempts);
    
    // Ré-initialise la requête
    if (secure) {
      _http.begin(_client, url);
      Serial.printf("[HTTP] 🔒 Using HTTPS client (attempt %d/%d)\n", attempt+1, maxAttempts);
    } else {
      _http.begin(plain, url);
      Serial.printf("[HTTP] 🌐 Using HTTP client (attempt %d/%d)\n", attempt+1, maxAttempts);
    }
    
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    Serial.printf("[HTTP] Headers set, timeout: %u ms\n", ServerConfig::REQUEST_TIMEOUT_MS);
    
    // Log avant envoi POST
    Serial.printf("[HTTP] Sending POST at %lu ms...\n", attemptStartMs);
    code = _http.POST(payload);
    unsigned long postDurationMs = millis() - attemptStartMs;
    Serial.printf("[HTTP] POST completed in %lu ms\n", postDurationMs);
    if (code > 0) {
      unsigned long responseStartMs = millis();
      response = _http.getString();
      unsigned long responseDurationMs = millis() - responseStartMs;
      
      // === LOGS DÉTAILLÉS v11.32 - DIAGNOSTIC COMPLET ===
      Serial.printf("[HTTP] ← HTTP %d, %u bytes (received in %lu ms)\n", code, response.length(), responseDurationMs);
      
      // Headers détaillés
      String contentType = _http.header("Content-Type");
      String server = _http.header("Server");
      String connection = _http.header("Connection");
      String contentLength = _http.header("Content-Length");
      String transferEncoding = _http.header("Transfer-Encoding");
      
      Serial.printf("[HTTP] Content-Type: %s\n", contentType.c_str());
      Serial.printf("[HTTP] Server: %s\n", server.c_str());
      Serial.printf("[HTTP] Connection: %s\n", connection.c_str());
      Serial.printf("[HTTP] Content-Length: %s\n", contentLength.c_str());
      Serial.printf("[HTTP] Transfer-Encoding: %s\n", transferEncoding.c_str());
      
      // Analyse détaillée de la réponse
      if (response.length() > 0) {
        Serial.println(F("[HTTP] === RÉPONSE COMPLÈTE ==="));
        if (response.length() <= 800) {
          Serial.printf("[HTTP] %s\n", response.c_str());
        } else {
          Serial.printf("[HTTP] %s ... (truncated)\n", response.substring(0,800).c_str());
          Serial.printf("[HTTP] ... (%u bytes restants)\n", response.length() - 800);
        }
        Serial.println(F("[HTTP] === FIN RÉPONSE ==="));
        
        // Analyse du type de contenu
        if (response.startsWith("<") || response.indexOf("<!DOCTYPE") >= 0 || response.indexOf("<html") >= 0) {
          Serial.println(F("[HTTP] ⚠️ ALERTE: Réponse HTML détectée au lieu de JSON/texte !"));
        } else if (response.startsWith("{") || response.startsWith("[")) {
          Serial.println(F("[HTTP] ✓ Réponse JSON détectée"));
        } else if (response.indexOf("success") >= 0 || response.indexOf("ok") >= 0) {
          Serial.println(F("[HTTP] ✓ Réponse texte positive détectée"));
        } else if (response.indexOf("error") >= 0 || response.indexOf("fail") >= 0) {
          Serial.println(F("[HTTP] ⚠️ Réponse texte d'erreur détectée"));
        }
        
        // Analyse des codes de statut
        if (code >= 200 && code < 300) {
          Serial.printf("[HTTP] ✓ Succès (2xx): %d\n", code);
        } else if (code >= 300 && code < 400) {
          Serial.printf("[HTTP] ⚠️ Redirection (3xx): %d\n", code);
        } else if (code >= 400 && code < 500) {
          Serial.printf("[HTTP] ❌ Erreur client (4xx): %d\n", code);
        } else if (code >= 500) {
          Serial.printf("[HTTP] ❌ Erreur serveur (5xx): %d\n", code);
        }
      } else {
        Serial.println(F("[HTTP] ⚠️ Réponse vide reçue"));
      }
    } else {
      // === LOGS D'ERREUR DÉTAILLÉS v11.32 ===
      unsigned long errorTimeMs = millis();
      Serial.printf("[HTTP] ❌ ERROR %d (attempt %d/%d) at %lu ms\n", code, attempt+1, maxAttempts, errorTimeMs);
      Serial.printf("[HTTP] Error detail: %s\n", _http.errorToString(code).c_str());
      Serial.printf("[HTTP] URL: %s\n", url.c_str());
      
      // État réseau au moment de l'erreur
      Serial.printf("[HTTP] WiFi status: %d (%s)\n", WiFi.status(), WiFi.isConnected() ? "CONNECTED" : "DISCONNECTED");
      Serial.printf("[HTTP] RSSI: %d dBm\n", WiFi.RSSI());
      Serial.printf("[HTTP] IP: %s\n", WiFi.localIP().toString().c_str());
      
      // Mémoire au moment de l'erreur
      size_t freeHeapError = ESP.getFreeHeap();
      Serial.printf("[HTTP] Free heap at error: %u bytes\n", freeHeapError);
      
      // Analyse des erreurs courantes
      if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: Connection refused - serveur indisponible"));
      } else if (code == HTTPC_ERROR_CONNECTION_LOST) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: Connection lost - problème réseau"));
      } else if (code == HTTPC_ERROR_NO_STREAM) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: No stream - problème de flux"));
      } else if (code == HTTPC_ERROR_NO_HTTP_SERVER) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: No HTTP server - serveur non HTTP"));
      } else if (code == HTTPC_ERROR_TOO_LESS_RAM) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: Too less RAM - mémoire insuffisante"));
      } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
        Serial.println(F("[HTTP] 🔍 DIAGNOSTIC: Read timeout - délai de lecture dépassé"));
      } else {
        Serial.printf("[HTTP] 🔍 DIAGNOSTIC: Erreur inconnue %d\n", code);
      }
    }
    _http.end();
    
    // === STATISTIQUES DE LA TENTATIVE ===
    unsigned long attemptDurationMs = millis() - attemptStartMs;
    Serial.printf("[HTTP] Tentative %d/%d terminée en %lu ms\n", attempt+1, maxAttempts, attemptDurationMs);
    
    // Record into diagnostics if available
    extern Diagnostics diag;
    if (code > 0) {
      diag.recordHttpResult(code >= 200 && code < 400, code);
    } else {
      diag.recordHttpResult(false, code);
    }

    // Succès 2xx-3xx : sortir immédiatement
    if (code >= 200 && code < 400) {
      Serial.printf("[HTTP] ✓ Succès détecté (HTTP %d), arrêt des tentatives\n", code);
      break;
    }
    
    // Erreur client 4xx : pas de retry (v11.31)
    if (code >= 400 && code < 500) {
      Serial.printf("[HTTP] ✗ Erreur client (4xx: %d), pas de retry\n", code);
      break;
    }
    
    // Court-circuit si plus de WiFi
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[HTTP] ✗ WiFi déconnecté, arrêt des tentatives"));
      break;
    }
    
    // Incrémenter attempt avant backoff
    attempt++;
    
    // Backoff exponentiel si retry possible (v11.31 amélioré)
    if (attempt < maxAttempts) {
      int backoff = NetworkConfig::BACKOFF_BASE_MS * attempt * attempt;
      Serial.printf("[HTTP] ⚠️ Retry %d/%d dans %d ms...\n", attempt+1, maxAttempts, backoff);
      
      // Log de l'état avant attente
      Serial.printf("[HTTP] État avant retry: WiFi=%d, RSSI=%d, Heap=%u\n", 
                   WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap());
      
      vTaskDelay(pdMS_TO_TICKS(backoff));
      
      // Log de l'état après attente
      Serial.printf("[HTTP] État après retry: WiFi=%d, RSSI=%d, Heap=%u\n", 
                   WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap());
      
      // Note: Watchdog géré par tâche appelante - vTaskDelay() yield le CPU
      // Ne pas appeler esp_task_wdt_reset() ici (erreur "task not found")
    }
  }

  // === RÉSUMÉ FINAL DE LA REQUÊTE ===
  unsigned long totalDurationMs = millis() - requestStartMs;
  Serial.printf("[HTTP] === FIN REQUÊTE HTTP ===\n");
  Serial.printf("[HTTP] Durée totale: %lu ms\n", totalDurationMs);
  Serial.printf("[HTTP] Tentatives: %d/%d\n", attempt+1, maxAttempts);
  Serial.printf("[HTTP] Code final: %d\n", code);
  Serial.printf("[HTTP] Succès: %s\n", (code >= 200 && code < 400) ? "OUI" : "NON");
  Serial.printf("[HTTP] Taille réponse: %u bytes\n", response.length());
  Serial.printf("[HTTP] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("==============================="));
  
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
  
  // Ordre exact aligné sur la liste utilisée côté serveur (voir makeSkeleton)
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
  
  // Envoi sans squelette: l'ordre exact est déjà respecté
  bool success = postRaw(payload, false);
  
  unsigned long smDurationMs = millis() - smStartMs;
  Serial.printf("[SM] === FIN SENDMEASUREMENTS ===\n");
  Serial.printf("[SM] Durée totale: %lu ms\n", smDurationMs);
  Serial.printf("[SM] Succès: %s\n", success ? "OUI" : "NON");
  Serial.printf("[SM] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("==============================="));
  
  return success;
}

bool WebClient::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // === LOGS DÉTAILLÉS GET v11.32 ===
  unsigned long getStartMs = millis();
  Serial.println(F("=== DÉBUT REQUÊTE GET REMOTE STATE ==="));
  Serial.printf("[GET] Timestamp: %lu ms\n", getStartMs);
  
  // Utiliser l'URL complète depuis la configuration serveur
  String url = ServerConfig::getOutputUrl();
  Serial.printf("[GET] URL: %s\n", url.c_str());
  
  // État réseau détaillé
  Serial.printf("[GET] WiFi Status: %d (connected=%s)\n", WiFi.status(), WiFi.isConnected() ? "YES" : "NO");
  Serial.printf("[GET] RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("[GET] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[GET] Free heap: %u bytes\n", ESP.getFreeHeap());
  
  // Sélectionne le bon type de client selon le schéma
  bool secure = url.startsWith("https://");
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
  bool success = httpRequest(ServerConfig::getHeartbeatUrl(), pay2, resp);
  
  unsigned long hbDurationMs = millis() - hbStartMs;
  Serial.printf("[HB] === FIN HEARTBEAT ===\n");
  Serial.printf("[HB] Durée totale: %lu ms\n", hbDurationMs);
  Serial.printf("[HB] Succès: %s\n", success ? "OUI" : "NON");
  Serial.printf("[HB] Réponse: %s\n", resp.c_str());
  Serial.println(F("========================="));
  
  return success;
}

// Nouveau helper : ajoute un squelette de champs vides afin que chaque POST reste complet
// Génère dynamiquement un squelette (champs vides) en excluant ceux déjà présents dans le payload
static String makeSkeleton(const String& payload) {
  static const char* FIELDS[] = {
    "version","TempAir","Humidite","TempEau",
    "EauPotager","EauAquarium","EauReserve","diffMaree","Luminosite",
    "etatPompeAqua","etatPompeTank","etatHeat","etatUV",
    "bouffeMatin","bouffeMidi","bouffeSoir","bouffePetits","bouffeGros",
    "tempsGros","tempsPetits",
    "aqThreshold","tankThreshold","chauffageThreshold",
    "mail","mailNotif","resetMode","tempsRemplissageSec","limFlood","WakeUp","FreqWakeUp"
  };
  const size_t COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);

  String stub;
  for (size_t i = 0; i < COUNT; ++i) {
    const char* key = FIELDS[i];
    // Ajoute uniquement la clé si elle n'existe pas déjà dans le payload fourni
    if (payload.indexOf(String(key) + "=") == -1) {
      stub += "&";
      stub += key;
      stub += "=";
    }
  }
  return stub;
}

bool WebClient::postRaw(const String& payload, bool includeSkeleton){
  // === LOGS DÉTAILLÉS POSTRAW v11.32 ===
  unsigned long prStartMs = millis();
  Serial.println(F("=== DÉBUT POSTRAW ==="));
  Serial.printf("[PR] Timestamp: %lu ms\n", prStartMs);
  Serial.printf("[PR] Payload input: %u bytes\n", payload.length());
  Serial.printf("[PR] Include skeleton: %s\n", includeSkeleton ? "OUI" : "NON");
  
  String full = payload;

  // Ajoute api_key et sensor si absents
  bool hasApi = payload.startsWith("api_key=");
  Serial.printf("[PR] Has API key: %s\n", hasApi ? "OUI" : "NON");
  
  if (!hasApi) {
    if (includeSkeleton) {
      String skeleton = makeSkeleton(payload);
      Serial.printf("[PR] Skeleton generated: %u bytes\n", skeleton.length());
      full = String("api_key=") + _apiKey + "&sensor=" + Config::SENSOR + skeleton;
    } else {
      full = String("api_key=") + _apiKey + "&sensor=" + Config::SENSOR;
    }
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
  bool okPrimary = httpRequest(ServerConfig::getPostDataUrl(), full, respPrimary);
  Serial.printf("[PR] Primary server result: %s\n", okPrimary ? "SUCCESS" : "FAILED");
  
  // Tentative d'envoi vers le serveur secondaire si configuré
  String secondary = ServerConfig::getSecondaryPostDataUrl();
  bool okSecondary = false; // considéré faux si non configuré
  if (secondary.length() > 0) {
    Serial.printf("[PR] Secondary server configured: %s\n", secondary.c_str());
    String respSecondary;
    Serial.println(F("[PR] Sending to secondary server..."));
    okSecondary = httpRequest(secondary, full, respSecondary);
    Serial.printf("[PR] Secondary server result: %s\n", okSecondary ? "SUCCESS" : "FAILED");
    if (!okSecondary) {
      Serial.println(F("[PR] Alerte: échec POST sur serveur secondaire"));
    }
  } else {
    Serial.println(F("[PR] No secondary server configured"));
  }
  
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

// Surcharge par compatibilité : comportement historique (avec squelette)
bool WebClient::postRaw(const String& payload){
  return postRaw(payload, true);
} 