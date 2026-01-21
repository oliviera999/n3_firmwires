#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "log.h"
#include "config.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)
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

WebClient::WebClient(const char* apiKey) {
  if (apiKey) {
    strncpy(_apiKey, apiKey, sizeof(_apiKey) - 1);
    _apiKey[sizeof(_apiKey) - 1] = '\0';
  } else {
    _apiKey[0] = '\0';
  }
  _client.setInsecure();               // accepte tous certificats (à affiner)
  _client.setHandshakeTimeout(12000);  // + de temps pour TLS
  // Désactivation du keep-alive : certaines déconnexions moitié-fermées
  // généraient un blocage interne du client HTTP, empêchant l'appel
  // suivant de se terminer et donc le rafraîchissement du watchdog.
  _http.setReuse(false);
  // NOUVEAU TIMEOUT NON-BLOQUANT (v11.50)
  _http.setTimeout(GlobalTimeouts::HTTP_MAX_MS); // Timeout strict pour éviter blocage
}

// v11.150: Force la libération de mémoire TLS après une requête
// Version simplifiée : juste arrêter le client sans le recréer (évite crash FreeRTOS)
void WebClient::resetTLSClient() {
  uint32_t heapBefore = ESP.getFreeHeap();
  
  // Stopper proprement le client TLS
  _client.stop();
  
  // Petit délai pour laisser le temps à FreeRTOS de libérer les ressources
  vTaskDelay(pdMS_TO_TICKS(50));
  
  uint32_t heapAfter = ESP.getFreeHeap();
  int32_t recovered = (int32_t)heapAfter - (int32_t)heapBefore;
  
  if (recovered > 1000) {
    Serial.printf("[HTTP] 🔄 TLS stop: récupéré %d bytes (heap: %u → %u)\n", 
                  recovered, heapBefore, heapAfter);
  }
}

bool WebClient::httpRequest(const char* url, const char* payload, char* response, size_t responseSize) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  if (url == nullptr || response == nullptr || responseSize == 0) {
    return false;
  }

  // Guard mémoire avant requête HTTPS (correction crash monitoring)
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  const uint32_t MIN_HEAP_FOR_HTTPS = 45000;  // 45 KB minimum (TLS ~42KB + marge 3KB)
  bool isSecure = (strncmp(url, "https://", 8) == 0);
  
  if (isSecure && freeHeap < MIN_HEAP_FOR_HTTPS) {
    LOG(LOG_WARN, "[HTTP] ⚠️ Heap trop faible (%u bytes < %u bytes), report de la requête HTTPS", 
        freeHeap, MIN_HEAP_FOR_HTTPS);
    LOG(LOG_WARN, "[HTTP] ⚠️ La requête HTTPS nécessite ~43 KB supplémentaires");
    return false;
  }
  
  size_t payloadLen = payload ? strlen(payload) : 0;
  uint32_t requestStartMs = millis();
  const bool debugLogging = (LOG_LEVEL >= LOG_DEBUG) && LogConfig::SERIAL_ENABLED;
  const bool verboseLogging = (LOG_LEVEL >= LOG_VERBOSE) && LogConfig::SERIAL_ENABLED;

  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] POST %s (payload=%u bytes, timeout=%u ms)",
        url, payloadLen, GlobalTimeouts::HTTP_MAX_MS);
    LOG(LOG_DEBUG, "[HTTP] WiFi status=%d connected=%s RSSI=%d dBm",
        WiFi.status(), WiFi.isConnected() ? "YES" : "NO", WiFi.RSSI());
    char ipBuf[16], gwBuf[16], dnsBuf[16];
    IPAddress ip = WiFi.localIP();
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    IPAddress gw = WiFi.gatewayIP();
    snprintf(gwBuf, sizeof(gwBuf), "%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
    IPAddress dns = WiFi.dnsIP();
    snprintf(dnsBuf, sizeof(dnsBuf), "%d.%d.%d.%d", dns[0], dns[1], dns[2], dns[3]);
    LOG(LOG_DEBUG, "[HTTP] IP=%s gateway=%s dns=%s", ipBuf, gwBuf, dnsBuf);
    LOG(LOG_DEBUG, "[HTTP] Heap libre=%u bytes (min=%u)", freeHeap, minHeap);

    if (verboseLogging && payloadLen > 0 && payload) {
      const size_t previewLen = payloadLen > 200 ? 200 : payloadLen;
      char previewBuf[201];
      strncpy(previewBuf, payload, previewLen);
      previewBuf[previewLen] = '\0';
      LOG(LOG_VERBOSE, "[HTTP] Payload (%u/%u): %s%s",
          previewLen,
          payloadLen,
          previewBuf,
          payloadLen > previewLen ? " ..." : "");
    }
  }
  
  // Fix v11.29: Délai minimum entre requêtes HTTP pour éviter saturation TCP
  if (_lastRequestMs > 0) {
    unsigned long timeSinceLastRequest = millis() - _lastRequestMs;
    if (timeSinceLastRequest < NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS) {
      uint32_t delayNeeded = NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS - timeSinceLastRequest;
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
    bool secure = isSecure;
  WiFiClient plain; // client non-TLS (portée limitée à la fonction)

  // Politique de retry exponentiel simple
  const int maxAttempts = 3;
  int attempt = 0;
  int code = -1;
  response[0] = '\0';
  
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
      LOG(LOG_DEBUG, "[HTTP] Headers positionnés, timeout requête=%u", NetworkConfig::REQUEST_TIMEOUT_MS);
    }
    
    // NOUVEAU TIMEOUT GLOBAL NON-BLOQUANT (v11.50)
    uint32_t elapsedMs = millis() - requestStartMs;
    if (elapsedMs >= GlobalTimeouts::HTTP_MAX_MS) {
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u/%u ms", elapsedMs, GlobalTimeouts::HTTP_MAX_MS);
      _http.end();
      resetTLSClient();  // v11.150: Libère mémoire TLS après timeout
      return false;
    }
    
    // Log avant envoi POST
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] POST en cours (elapsed=%lu ms)", attemptStartMs - requestStartMs);
    }
    code = _http.POST(payload ? payload : "");
    unsigned long postDurationMs = millis() - attemptStartMs;
    if (debugLogging) {
      LOG(LOG_DEBUG, "[HTTP] POST terminé en %lu ms", postDurationMs);
    }
    if (code > 0) {
      unsigned long responseStartMs = millis();
      String tempResponse = _http.getString();
      size_t responseLen = tempResponse.length();
      if (responseLen >= responseSize) {
        responseLen = responseSize - 1;
      }
      const char* tempResponseStr = tempResponse.c_str();
      strncpy(response, tempResponseStr, responseLen);
      response[responseLen] = '\0';
      unsigned long responseDurationMs = millis() - responseStartMs;
      
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Réponse reçue code=%d bytes=%u en %lu ms", code, responseLen, responseDurationMs);
      }

      if (verboseLogging) {
        LOG(LOG_VERBOSE, "[HTTP] Content-Type=%s", _http.header("Content-Type").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Server=%s", _http.header("Server").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Connection=%s", _http.header("Connection").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Content-Length=%s", _http.header("Content-Length").c_str());
        LOG(LOG_VERBOSE, "[HTTP] Transfer-Encoding=%s", _http.header("Transfer-Encoding").c_str());

        if (responseLen > 0) {
          const size_t previewLen = responseLen > 200 ? 200 : responseLen;
          char previewBuf[201];
          strncpy(previewBuf, response, previewLen);
          previewBuf[previewLen] = '\0';
          LOG(LOG_VERBOSE, "[HTTP] Response (%u/%u): %s%s",
              previewLen,
              responseLen,
              previewBuf,
              responseLen > previewLen ? " ..." : "");
        }
      }

      if (responseLen == 0) {
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
      LOG(LOG_WARN, "[HTTP] URL=%s", url);
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
      // On tente un reset du watchdog ici pour éviter le timeout lors des retries multiples
      esp_task_wdt_reset(); 
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
        strlen(response),
        ESP.getFreeHeap());
  }
  
  // Fix v11.29: Sauvegarder timestamp pour délai inter-requêtes
  _lastRequestMs = millis();
  
  // v11.150: Reset TLS client pour libérer mémoire (~46KB) après chaque requête
  // Résout la fuite mémoire où le client TLS garde la mémoire après échec
  resetTLSClient();
  
  // Ne pas réactiver le modem-sleep
  WiFi.setSleep(false);
  bool success = (code >= 200 && code < 400);
  if (!success) {
    response[0] = '\0';  // En cas d'échec, vider la réponse
  }
  return success;
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
  char tempWaterStr[16], tempAirStr[16], humidityStr[16];
  snprintf(tempWaterStr, sizeof(tempWaterStr), isnan(tempWater) ? "NaN" : "%.1f", tempWater);
  snprintf(tempAirStr, sizeof(tempAirStr), isnan(tempAir) ? "NaN" : "%.1f", tempAir);
  snprintf(humidityStr, sizeof(humidityStr), isnan(humidity) ? "NaN" : "%.1f", humidity);
  Serial.printf("[SM] Valeurs validées - TempEau: %s, TempAir: %s, Humidité: %s\n",
               tempWaterStr, tempAirStr, humidityStr);

  // Construction d'un payload COMPLET et ORDONNÉ selon la liste attendue par le serveur
  auto fmtFloat = [](float v, char* buf, size_t bufSize) -> void {
    if (isnan(v)) {
      buf[0] = '\0';
    } else {
      snprintf(buf, bufSize, "%.1f", v);
    }
  };
  // Pour les ultrasons: 0 signifie invalide → envoyer champ vide ""
  auto fmtUltrasonic = [](uint16_t v, char* buf, size_t bufSize) -> void {
    if (v == 0) {
      buf[0] = '\0';
    } else {
      snprintf(buf, bufSize, "%u", (unsigned)v);
    }
  };

  char payload[512];
  payload[0] = '\0';
  size_t offset = 0;
  bool truncated = false;
  
  auto appendKV = [&](const char* key, const char* value) {
    if (truncated || offset >= sizeof(payload) - 1) {
      truncated = true;
      return;
    }
    size_t remaining = sizeof(payload) - offset;
    int written = 0;
    if (offset > 0) {
      written = snprintf(payload + offset, remaining, "&");
      if (written < 0 || static_cast<size_t>(written) >= remaining) {
        truncated = true;
        payload[sizeof(payload) - 1] = '\0';
        return;
      }
      offset += static_cast<size_t>(written);
      remaining = sizeof(payload) - offset;
    }
    written = snprintf(payload + offset, remaining, "%s=%s", key, value);
    if (written < 0 || static_cast<size_t>(written) >= remaining) {
      truncated = true;
      payload[sizeof(payload) - 1] = '\0';
      return;
    }
    offset += static_cast<size_t>(written);
  };

  Serial.println(F("[SM] Construction du payload..."));
  
  // Helper strings - UNIQUEMENT les mesures et états actuels (pas les configs!)
  char buf_tempAir[16], buf_humid[16], buf_tempWater[16];
  char buf_wlPota[8], buf_wlAqua[8], buf_wlTank[8];
  char buf_diffMaree[16], buf_lum[16];
  char buf_pumpAqua[2], buf_pumpTank[2], buf_heat[2], buf_uv[2];
  
  fmtFloat(tempAir, buf_tempAir, sizeof(buf_tempAir));
  fmtFloat(humidity, buf_humid, sizeof(buf_humid));
  fmtFloat(tempWater, buf_tempWater, sizeof(buf_tempWater));
  fmtUltrasonic(m.wlPota, buf_wlPota, sizeof(buf_wlPota));
  fmtUltrasonic(m.wlAqua, buf_wlAqua, sizeof(buf_wlAqua));
  fmtUltrasonic(m.wlTank, buf_wlTank, sizeof(buf_wlTank));
  snprintf(buf_diffMaree, sizeof(buf_diffMaree), "%d", m.diffMaree);
  snprintf(buf_lum, sizeof(buf_lum), "%u", m.luminosite);
  snprintf(buf_pumpAqua, sizeof(buf_pumpAqua), "%d", m.pumpAqua ? 1 : 0);
  snprintf(buf_pumpTank, sizeof(buf_pumpTank), "%d", m.pumpTank ? 1 : 0);
  snprintf(buf_heat, sizeof(buf_heat), "%d", m.heater ? 1 : 0);
  snprintf(buf_uv, sizeof(buf_uv), "%d", m.light ? 1 : 0);

  // v11.141: Envoi UNIQUEMENT des mesures et états actuels
  // Les configurations (seuils, durées, heures, etc.) sont gérées côté serveur
  // et ne doivent PAS être envoyées par l'ESP pour éviter d'écraser les valeurs distantes
  appendKV("version", ProjectConfig::VERSION);
  appendKV("TempAir", buf_tempAir);
  appendKV("Humidite", buf_humid);
  appendKV("TempEau", buf_tempWater);
  appendKV("EauPotager", buf_wlPota);
  appendKV("EauAquarium", buf_wlAqua);
  appendKV("EauReserve", buf_wlTank);
  appendKV("diffMaree", buf_diffMaree);
  appendKV("Luminosite", buf_lum);
  appendKV("etatPompeAqua", buf_pumpAqua);
  appendKV("etatPompeTank", buf_pumpTank);
  appendKV("etatHeat", buf_heat);
  appendKV("etatUV", buf_uv);
  
  // Reset mode - envoyé uniquement si demandé explicitement
  if (includeReset) {
    appendKV("resetMode", "0");
  }

  if (truncated) {
    Serial.println(F("[SM] ❌ Payload tronqué - envoi annulé"));
    return false;
  }

  Serial.printf("[SM] Payload construit: %u bytes\n", strlen(payload));
  Serial.printf("[SM] Version: %s\n", ProjectConfig::VERSION);
  Serial.println(F("[SM] Note: Configurations non envoyées (gérées côté serveur)"));

  LOG(LOG_DEBUG, "POST %s", payload);
  
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

bool WebClient::fetchRemoteState(JsonDocument& doc) {
  if (!config.isRemoteRecvEnabled()) {
    Serial.println(F("[GET] ⛔ Réception distante désactivée (config)"));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // === v11.152: Protection TLS mutex STRICTE pour éviter collision avec SMTP ===
  // Le GET DOIT avoir le mutex pour éviter les crashs par épuisement mémoire
  // (coredump: strcat(NULL) dans ESP Mail Client quand SMTP+HTTPS concurrent)
  if (!TLSMutex::acquire(3000)) {  // Timeout court pour GET
    Serial.println(F("[GET] ⛔ Mutex TLS non disponible - GET ANNULÉ (collision SMTP probable)"));
    return false;  // On abandonne au lieu de risquer un crash
  }
  bool hasMutex = true;  // Pour compatibilité avec le code existant
  
  // === LOGS DÉTAILLÉS GET v11.32 ===
  unsigned long getStartMs = millis();
  Serial.println(F("=== DÉBUT REQUÊTE GET REMOTE STATE ==="));
  Serial.printf("[GET] Timestamp: %lu ms\n", getStartMs);
  
  // Guard mémoire avant requête HTTPS (correction crash monitoring)
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  const uint32_t MIN_HEAP_FOR_HTTPS = 45000;  // 45 KB minimum (TLS ~42KB + marge 3KB)
  
  Serial.printf("[GET] Heap before GET: %u bytes\n", freeHeap);
  Serial.printf("[GET] Free heap: %u bytes\n", freeHeap);
  Serial.printf("[GET] Min heap: %u bytes\n", minHeap);
  
  if (freeHeap < MIN_HEAP_FOR_HTTPS) {
    Serial.printf("[GET] ⚠️ Heap trop faible (%u bytes < %u bytes), report de la requête\n", 
                  freeHeap, MIN_HEAP_FOR_HTTPS);
    Serial.printf("[GET] ⚠️ La requête HTTPS nécessite ~43 KB supplémentaires\n");
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // Utiliser l'URL complète depuis la configuration serveur
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));
  Serial.printf("[GET] URL: %s\n", url);
  
  // État réseau détaillé
  Serial.printf("[GET] WiFi Status: %d (connected=%s)\n", WiFi.status(), WiFi.isConnected() ? "YES" : "NO");
  Serial.printf("[GET] RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("[GET] IP: %s\n", WiFi.localIP().toString().c_str());
  
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
  
  Serial.printf("[GET] Sending GET request (timeout: %u ms)...\n", NetworkConfig::REQUEST_TIMEOUT_MS);
  int code = _http.GET();
  unsigned long getDurationMs = millis() - getStartMs;
  Serial.printf("[GET] GET completed in %lu ms, HTTP code: %d\n", getDurationMs, code);
  Serial.printf("[GET] Heap after GET: %u bytes\n", ESP.getFreeHeap());
  
  if (code <= 0) {
    Serial.printf("[GET] ❌ ERROR %d: %s\n", code, _http.errorToString(code).c_str());
    Serial.printf("[GET] WiFi status at error: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
    _http.end();
    resetTLSClient();  // v11.150: Libère mémoire TLS après erreur
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // v11.156: Lecture limitée avec buffer fixe pour éviter fragmentation mémoire
  // Limite stricte basée sur BufferConfig::JSON_DOCUMENT_SIZE
  const size_t MAX_RESPONSE_SIZE = BufferConfig::JSON_DOCUMENT_SIZE;
  char payloadBuffer[MAX_RESPONSE_SIZE + 1];
  size_t totalRead = 0;
  
  unsigned long responseStartMs = millis();
  WiFiClient* stream = _http.getStreamPtr();
  
  if (!stream) {
    Serial.println(F("[GET] ❌ No stream available"));
    _http.end();
    resetTLSClient();
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // Lire avec limite stricte pour éviter épuisement mémoire
  while (stream->available() && totalRead < MAX_RESPONSE_SIZE) {
    size_t bytesRead = stream->readBytes(
      payloadBuffer + totalRead,
      MAX_RESPONSE_SIZE - totalRead
    );
    if (bytesRead == 0) break;
    totalRead += bytesRead;
  }
  payloadBuffer[totalRead] = '\0';
  
  unsigned long responseDurationMs = millis() - responseStartMs;
  _http.end();
  resetTLSClient();  // v11.150: Libère mémoire TLS (~46KB)
  
  if (totalRead >= MAX_RESPONSE_SIZE) {
    Serial.printf("[GET] ⚠️ Response truncated at %u bytes (limite sécurité)\n", MAX_RESPONSE_SIZE);
  }
  
  Serial.printf("[GET] Response received in %lu ms, size: %u bytes\n", responseDurationMs, totalRead);
  Serial.printf("[GET] Heap after TLS reset: %u bytes\n", ESP.getFreeHeap());
  
  if (totalRead == 0) {
    Serial.println(F("[GET] ⚠️ Empty response from server"));
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // Log détaillé de la réponse
  Serial.println(F("[GET] === RÉPONSE REMOTE STATE ==="));
  if (totalRead <= 600) {
    Serial.printf("[GET] %s\n", payloadBuffer);
  } else {
    char preview[601];
    strncpy(preview, payloadBuffer, 600);
    preview[600] = '\0';
    Serial.printf("[GET] %s ... (truncated)\n", preview);
    Serial.printf("[GET] ... (%u bytes restants)\n", totalRead - 600);
  }
  Serial.println(F("[GET] === FIN RÉPONSE ==="));
  
  // Parser le JSON depuis le buffer directement
  unsigned long parseStartMs = millis();
  DeserializationError err = deserializeJson(doc, payloadBuffer);
  unsigned long parseDurationMs = millis() - parseStartMs;
  
  if (err) {
    Serial.printf("[GET] ❌ JSON parse error: %s (parsing took %lu ms)\n", err.c_str(), parseDurationMs);
    char preview[201];
    strncpy(preview, payloadBuffer, 200);
    preview[200] = '\0';
    Serial.printf("[GET] Payload preview (first 200 chars): %s\n", preview);
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  Serial.printf("[GET] ✓ Remote JSON parsed successfully in %lu ms\n", parseDurationMs);
  
    // Analyse du contenu JSON
    if (doc.is<JsonObject>()) {
      JsonObject obj = doc.as<JsonObject>();
      Serial.printf("[GET] JSON object with %d keys\n", obj.size());
      
      // Log des clés principales pour debugging
      if (obj["version"].is<const char*>()) {
        Serial.printf("[GET] Server version: %s\n", obj["version"].as<const char*>());
      }
      if (obj["timestamp"].is<const char*>()) {
        Serial.printf("[GET] Server timestamp: %s\n", obj["timestamp"].as<const char*>());
      }
      if (obj["status"].is<const char*>()) {
        Serial.printf("[GET] Server status: %s\n", obj["status"].as<const char*>());
      }
    }
  
  unsigned long totalDurationMs = millis() - getStartMs;
  Serial.printf("[GET] === FIN REQUÊTE GET ===\n");
  Serial.printf("[GET] Durée totale: %lu ms\n", totalDurationMs);
  Serial.printf("[GET] Succès: OUI\n");
  Serial.printf("[GET] Mémoire finale: %u bytes\n", ESP.getFreeHeap());
  Serial.println(F("==============================="));
  
  // v11.149: Libérer le mutex TLS
  if (hasMutex) TLSMutex::release();
  
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
  
  const DiagnosticStats& s = diag.getStats();
  
  // Construction du payload avec logs détaillés
  char payloadBuf[256];
  snprintf(payloadBuf, sizeof(payloadBuf), "uptime=%lu&free=%u&min=%u&reboots=%u",
           s.uptimeSec, s.freeHeap, s.minFreeHeap, s.rebootCount);
  
  Serial.printf("[HB] Payload avant CRC: %s\n", payloadBuf);
  
  char bufCrc2[16];
  snprintf(bufCrc2,sizeof(bufCrc2),"&crc=%08lX",crc32(payloadBuf));
  
  char pay2[272];
  snprintf(pay2, sizeof(pay2), "%s%s", payloadBuf, bufCrc2);
  
  Serial.printf("[HB] Payload final: %s\n", pay2);
  Serial.printf("[HB] Taille payload: %u bytes\n", strlen(pay2));
  
  // État système au moment du heartbeat
  Serial.printf("[HB] Uptime: %lu sec\n", s.uptimeSec);
  Serial.printf("[HB] Free heap: %u bytes\n", s.freeHeap);
  Serial.printf("[HB] Min free heap: %u bytes\n", s.minFreeHeap);
  Serial.printf("[HB] Reboot count: %u\n", s.rebootCount);
  Serial.printf("[HB] WiFi status: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
  
  char resp[1024];
  char heartbeatUrl[256];
  ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
  bool success = httpRequest(heartbeatUrl, pay2, resp, sizeof(resp));
  
  unsigned long hbDurationMs = millis() - hbStartMs;
  Serial.printf("[HB] === FIN HEARTBEAT ===\n");
  Serial.printf("[HB] Durée totale: %lu ms\n", hbDurationMs);
  Serial.printf("[HB] Succès: %s\n", success ? "OUI" : "NON");
  Serial.printf("[HB] Réponse: %s\n", resp);
  Serial.println(F("========================="));
  
  return success;
}

// v11.70: makeSkeleton() supprimé - jamais utilisé (includeSkeleton toujours false)

bool WebClient::postRaw(const char* payload){
  if (!config.isRemoteSendEnabled()) {
    Serial.println(F("[PR] ⛔ Envoi distant désactivé (config) - SKIP"));
    return false;
  }
  
  if (payload == nullptr) {
    Serial.println(F("[PR] ⛔ Payload null - SKIP"));
    return false;
  }
  
  // === v11.149: Protection TLS mutex pour éviter collision avec SMTP ===
  if (!TLSMutex::acquire(5000)) {  // Timeout 5s pour POST
    Serial.println(F("[PR] ⛔ Impossible d'acquérir mutex TLS - SKIP"));
    return false;
  }
  
  // === LOGS DÉTAILLÉS POSTRAW v11.70 ===
  unsigned long prStartMs = millis();
  size_t payloadLen = strlen(payload);
  Serial.println(F("=== DÉBUT POSTRAW ==="));
  Serial.printf("[PR] Timestamp: %lu ms\n", prStartMs);
  Serial.printf("[PR] Payload input: %u bytes\n", payloadLen);
  
  // Utiliser un buffer dynamique pour éviter String
  // Taille estimée: payload + api_key + sensor + clés + padding
  size_t estimatedSize = payloadLen + strlen(_apiKey) + strlen(ProjectConfig::BOARD_TYPE) + 32;
  char* fullBuffer = (char*)malloc(estimatedSize);
  if (!fullBuffer) {
    Serial.println(F("[PR] ❌ Malloc failed for payload"));
    TLSMutex::release();
    return false;
  }

  // Ajoute api_key et sensor si absents
  bool hasApi = (strncmp(payload, "api_key=", 8) == 0); // Starts with
  Serial.printf("[PR] Has API key: %s\n", hasApi ? "OUI" : "NON");
  
  if (!hasApi) {
    snprintf(fullBuffer, estimatedSize, "api_key=%s&sensor=%s", _apiKey, ProjectConfig::BOARD_TYPE);
    if (payloadLen > 0) {
      size_t currentLen = strlen(fullBuffer);
      if (currentLen + 1 < estimatedSize) {
        strncat(fullBuffer, "&", estimatedSize - currentLen - 1);
      }
      currentLen = strlen(fullBuffer);
      if (currentLen < estimatedSize) {
        strncat(fullBuffer, payload, estimatedSize - currentLen - 1);
      }
    }
    Serial.printf("[PR] Full payload constructed: %u bytes\n", strlen(fullBuffer));
  } else {
    strncpy(fullBuffer, payload, estimatedSize - 1);
    fullBuffer[estimatedSize - 1] = '\0';
    Serial.println(F("[PR] Using payload as-is (already has API key)"));
  }

  Serial.printf("[PR] Final payload size: %u bytes\n", strlen(fullBuffer));
  Serial.printf("[PR] API Key: %s\n", _apiKey);
  Serial.printf("[PR] Sensor: %s\n", ProjectConfig::BOARD_TYPE);

  char respPrimary[1024];
  Serial.println(F("[PR] Sending to primary server..."));
  char postDataUrl[256];
  ServerConfig::getPostDataUrl(postDataUrl, sizeof(postDataUrl));
  bool okPrimary = httpRequest(postDataUrl, fullBuffer, respPrimary, sizeof(respPrimary));
  Serial.printf("[PR] Primary server result: %s\n", okPrimary ? "SUCCESS" : "FAILED");
  
  free(fullBuffer); // Libérer la mémoire
  
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
  
  // v11.149: Libérer le mutex TLS
  TLSMutex::release();
  
  return finalSuccess;
}

// Fonction supprimée - redéfinition causait erreur de compilation 