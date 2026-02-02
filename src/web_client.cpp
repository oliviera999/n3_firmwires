// v11.162: WebClient simplifié - HTTP par défaut (plus de TLS pour requêtes courantes)
// Réduit fragmentation mémoire en éliminant le besoin de ~32KB contigu pour TLS
// v11.172: Les noms de variables POST sont définis dans gpio_mapping.h (VariableRegistry)
// Source de vérité: GPIOMap::XXX.serverPostName (ex: "chauffageThreshold")
// v11.171: Queue persistante POSTs échoués (offline-first)
#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "log.h"
#include "config.h"
#include "nvs_manager.h"
#include "nvs_keys.h"  // v11.178: Utilisation des clés NVS centralisées (audit)
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <atomic>

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
  // v11.162: Configuration HTTP simple (plus de TLS)
  _http.setReuse(false);  // Désactive keep-alive pour éviter blocages
  _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
}

bool WebClient::httpRequest(const char* url, const char* payload,
                           char* response, size_t responseSize) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  if (url == nullptr || response == nullptr || responseSize == 0) {
    return false;
  }

  uint32_t freeHeap = ESP.getFreeHeap();
  size_t payloadLen = payload ? strlen(payload) : 0;
  uint32_t requestStartMs = millis();
  const bool debugLogging = (LOG_LEVEL >= LOG_DEBUG) && LogConfig::SERIAL_ENABLED;

  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] POST %s (payload=%u bytes)", url, payloadLen);
    LOG(LOG_DEBUG, "[HTTP] Heap libre=%u bytes", freeHeap);
  }
  
  // Délai minimum entre requêtes HTTP pour éviter saturation TCP
  if (_lastRequestMs > 0) {
    unsigned long timeSinceLastRequest = millis() - _lastRequestMs;
    if (timeSinceLastRequest < NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS) {
      uint32_t delayNeeded = NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS - timeSinceLastRequest;
      vTaskDelay(pdMS_TO_TICKS(delayNeeded));
    }
  }
  
  // Désactiver le modem sleep pendant le transfert
  WiFi.setSleep(false);

  const int MAX_ATTEMPTS = 2;
  int code = -1;
  response[0] = '\0';
  bool success = false;

  for (int attempt = 0; attempt < MAX_ATTEMPTS && !success; attempt++) {
    unsigned long attemptStartMs = millis();

    // Timeout global: vérifier AVANT begin() pour éviter end() sur état incohérent
    // (corrige LoadProhibited quand end() était appelé juste après begin() au retry)
    uint32_t elapsedMs = millis() - requestStartMs;
    if (elapsedMs >= NetworkConfig::HTTP_TIMEOUT_MS) {
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u ms", elapsedMs);
      return false;
    }

    // v11.162: Utilise le client HTTP simple (membre de classe)
    if (!_http.begin(_client, url)) {
      LOG(LOG_WARN, "[HTTP] begin() failed for URL");
      // Pas de end() - HTTPClient pas initialisé
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;  // Retry
    }
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    code = _http.POST(payload ? payload : "");
    
    if (code > 0) {
      // Lire la réponse
      const size_t MAX_TEMP_RESPONSE = 1024;
      char tempResponseBuffer[MAX_TEMP_RESPONSE + 1];
      WiFiClient* stream = _http.getStreamPtr();
      size_t responseLen = 0;
      
      if (stream) {
        // v11.176: Ajout timeout pour éviter blocage indéfini (audit robustesse)
        const unsigned long streamReadStart = millis();
        const unsigned long STREAM_READ_TIMEOUT_MS = NetworkConfig::HTTP_TIMEOUT_MS;
        uint8_t emptyReads = 0;
        const uint8_t MAX_EMPTY_READS = 10;
        
        while (responseLen < MAX_TEMP_RESPONSE && 
               (millis() - streamReadStart) < STREAM_READ_TIMEOUT_MS) {
          if (stream->available()) {
            size_t bytesRead = stream->readBytes(
              tempResponseBuffer + responseLen,
              MAX_TEMP_RESPONSE - responseLen
            );
            if (bytesRead == 0) {
              emptyReads++;
              if (emptyReads >= MAX_EMPTY_READS) break;
              vTaskDelay(pdMS_TO_TICKS(10));
            } else {
              responseLen += bytesRead;
              emptyReads = 0;
            }
          } else {
            emptyReads++;
            if (emptyReads >= MAX_EMPTY_READS) break;
            vTaskDelay(pdMS_TO_TICKS(10));
          }
        }
        tempResponseBuffer[responseLen] = '\0';
      } else {
        // v11.179: Désactivation getString() - cause crashes LoadProhibited dans destructeur String
        // Fallback: réponse vide si pas de stream (rare, mais sûr)
        tempResponseBuffer[0] = '\0';
        responseLen = 0;
      }
      
      if (responseLen >= responseSize) {
        responseLen = responseSize - 1;
      }
      strncpy(response, tempResponseBuffer, responseLen);
      response[responseLen] = '\0';
      
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Réponse code=%d bytes=%u", code, responseLen);
      }
    } else {
      // v11.179: Pas de String temporaire pour éviter crash dans destructeur
      LOG(LOG_WARN, "[HTTP] Erreur %d", code);
    }
    // v11.182: SUPPRESSION du drain dangereux via stream->connected()/available()
    // Le drain après erreur ou timeout peut causer LoadProhibited si stream invalide
    // HTTPClient.end() gère le nettoyage proprement
    if (_client.connected()) {
      _http.end();
    }
    // Réinitialiser HTTPClient pour prochain appel (même si end() non appelé)
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);

    // Enregistrer dans diagnostics
    extern Diagnostics diag;
    if (code > 0) {
      diag.recordHttpResult(code >= 200 && code < 400, code);
    } else {
      diag.recordHttpResult(false, code);
    }

    // Succès 2xx-3xx
    if (code >= 200 && code < 400) {
      success = true;
      break;
    }

    // Erreur client 4xx : pas de retry
    if (code >= 400 && code < 500) {
      LOG(LOG_WARN, "[HTTP] Erreur client %d, pas de retry", code);
      break;
    }

    // WiFi perdu
    if (WiFi.status() != WL_CONNECTED) {
      LOG(LOG_WARN, "[HTTP] WiFi déconnecté");
      break;
    }

    // Backoff avant retry
    if (attempt + 1 < MAX_ATTEMPTS) {
      int backoff = NetworkConfig::BACKOFF_BASE_MS * (attempt + 1);
      vTaskDelay(pdMS_TO_TICKS(backoff));
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
    }
  }

  // Log final (v11.182: copie locale pour éviter tout usage de state HTTP après end())
  unsigned long totalDurationMs = millis() - requestStartMs;
  const int finalCode = code;
  const bool finalSuccess = success;
  if (debugLogging || !finalSuccess) {
    const char* succStr = finalSuccess ? "oui" : "non";
    LOG(LOG_INFO, "[HTTP] Requête: %lu ms, code=%d, succès=%s",
        totalDurationMs, finalCode, succStr);
  }

  _lastRequestMs = millis();

  if (!finalSuccess) {
    if (response && responseSize > 0) {
      response[0] = '\0';
    }
  }
  return finalSuccess;
}

bool WebClient::sendMeasurements(const Measurements& m, bool includeReset) {
  // Validation des mesures
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humidity;

  if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
    tempWater = NAN;
  }
  if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
    tempAir = NAN;
  }
  if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
    humidity = NAN;
  }

  auto fmtFloat = [](float v, char* buf, size_t bufSize) {
    if (isnan(v)) {
      buf[0] = '\0';
    } else {
      snprintf(buf, bufSize, "%.1f", v);
    }
  };
  auto fmtUltrasonic = [](uint16_t v, char* buf, size_t bufSize) {
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
        return;
      }
      offset += static_cast<size_t>(written);
      remaining = sizeof(payload) - offset;
    }
    written = snprintf(payload + offset, remaining, "%s=%s", key, value);
    if (written < 0 || static_cast<size_t>(written) >= remaining) {
      truncated = true;
      return;
    }
    offset += static_cast<size_t>(written);
  };
  
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
  
  if (includeReset) {
    appendKV("resetMode", "0");
  }

  if (truncated) {
    return false;
  }
  
  return postRaw(payload);
}

bool WebClient::tryFetchConfigFromServer(JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!config.isRemoteRecvEnabled()) return false;
  return fetchRemoteState(doc);
}

bool WebClient::tryPushStatusToServer(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!config.isRemoteSendEnabled()) return false;
  if (payload == nullptr) return false;
  return postRaw(payload);
}

bool WebClient::fetchRemoteState(JsonDocument& doc) {
  if (!config.isRemoteRecvEnabled()) {
    return false;
  }
  
  // v11.165: Fallback NVS si WiFi non disponible (audit offline-first)
  if (WiFi.status() != WL_CONNECTED) {
    return loadFromNVSFallback(doc);
  }

  WiFi.setSleep(false);
  vTaskDelay(pdMS_TO_TICKS(100));

  const size_t MAX_RESPONSE_SIZE = BufferConfig::OUTPUTS_STATE_READ_BUFFER_SIZE;
  char payloadBuffer[MAX_RESPONSE_SIZE + 1];
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));

  // Timeout dédié plus long pour GET outputs/state (évite -11 read timeout si serveur/latence lents)
  _http.setTimeout(NetworkConfig::OUTPUTS_STATE_HTTP_TIMEOUT_MS);

  // v11.162: Utilise le client HTTP simple (membre de classe)
  // v11.166: Verification retour http.begin() (audit robustesse)
  if (!_http.begin(_client, url)) {
    LOG(LOG_WARN, "[HTTP] Echec initialisation HTTPClient pour %s", url);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc);
  }

  int code = _http.GET();

  if (code <= 0) {
    Serial.printf("[HTTP] outputs/state: code=%d (skip read)\n", code);
    // Nettoyage systématique après timeout (-11) ou autre erreur: évite réutiliser un client en mauvais état
    _http.end();
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc);
  }

  // v11.175: Amélioration lecture HTTP - gestion Content-Length et chunked
  int contentLength = _http.getSize();
  bool hasContentLength = (contentLength > 0);
  size_t totalRead = 0;  // Déclaré ici pour être accessible après le if/else
  
  WiFiClient* stream = _http.getStreamPtr();
  if (!stream) {
    LOG(LOG_WARN, "[HTTP] Pas de stream disponible, fallback NVS");
    _http.end();
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc);
  }

  // v11.185: Quand Content-Length est connu, lecture en un bloc (évite body déjà consommé par
  // le stack TCP/HTTP si on attend entre available() et read).
  if (hasContentLength && (size_t)contentLength <= MAX_RESPONSE_SIZE) {
    stream->setTimeout(3000);
    totalRead = stream->readBytes(payloadBuffer, (size_t)contentLength);
    payloadBuffer[totalRead] = '\0';
  }

  if (totalRead == 0) {
    // Pas de Content-Length ou lecture bloc a échoué: boucle classique
    unsigned long globalReadStart = millis();
    const unsigned long READ_TIMEOUT_MS = NetworkConfig::OUTPUTS_STATE_HTTP_TIMEOUT_MS;
    uint8_t emptyReads = 0;
    const uint8_t MAX_EMPTY_READS = 10;

    while (totalRead < MAX_RESPONSE_SIZE && (millis() - globalReadStart) < READ_TIMEOUT_MS) {
      if (!stream->connected()) break;
      if (stream->available()) {
        size_t bytesRead = stream->readBytes(
          payloadBuffer + totalRead,
          MAX_RESPONSE_SIZE - totalRead
        );
        if (bytesRead > 0) {
          totalRead += bytesRead;
          emptyReads = 0;
          if (hasContentLength && totalRead >= (size_t)contentLength) break;
        } else {
          emptyReads++;
          if (!hasContentLength && emptyReads >= MAX_EMPTY_READS) break;
          vTaskDelay(pdMS_TO_TICKS(20));
        }
      } else {
        emptyReads++;
        if (!hasContentLength && emptyReads >= MAX_EMPTY_READS) break;
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
    payloadBuffer[totalRead] = '\0';
  }

  Serial.printf("[HTTP] outputs/state: code=%d contentLength=%d totalRead=%u\n",
                code, contentLength, (unsigned)totalRead);

  if (totalRead >= MAX_RESPONSE_SIZE || (hasContentLength && contentLength > (int)MAX_RESPONSE_SIZE)) {
    LOG(LOG_WARN, "[HTTP] Réponse outputs/state tronquée (lu=%u, max=%u, contentLength=%d)",
        (unsigned)totalRead, (unsigned)MAX_RESPONSE_SIZE, contentLength);
  }
  _http.end();
  _http.setReuse(false);
  _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);

  if (totalRead == 0) {
    _http.end();
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc);
  }

  // Trouver le début du JSON
  char* jsonStart = strchr(payloadBuffer, '{');
  if (!jsonStart) {
    jsonStart = strchr(payloadBuffer, '[');
  }
  
  // v11.172: Si pas de JSON valide trouvé, fallback silencieux (normal si serveur non configuré)
  if (!jsonStart) {
    return loadFromNVSFallback(doc);
  }
  
  if (jsonStart != payloadBuffer) {
    size_t offset = jsonStart - payloadBuffer;
    size_t newSize = totalRead - offset;
    memmove(payloadBuffer, jsonStart, newSize);
    payloadBuffer[newSize] = '\0';
  }

  DeserializationError err = deserializeJson(doc, payloadBuffer);
  // #region agent log
  {
    size_t previewLen = totalRead < 180 ? totalRead : 180;
    char preview[184];
    memcpy(preview, payloadBuffer, previewLen);
    preview[previewLen] = '\0';
    Serial.printf("[DBG] H1 rawPreview len=%u firstKeys=%s\n", (unsigned)previewLen, preview);
    Serial.printf("[DBG] H1 hasOutputs=%d hasSwitches=%d docSize=%u\n",
                  doc["outputs"].is<JsonObject>() ? 1 : 0,
                  doc["switches"].is<JsonObject>() ? 1 : 0,
                  (unsigned)doc.size());
  }
  // #endregion
  if (err) {
    // v11.173: Rate-limiting des logs JSON error (log les 3 premiers, puis toutes les 30s)
    // Atomic pour éviter race conditions (fetchRemoteState peut être appelé depuis plusieurs tâches)
    static std::atomic<uint32_t> jsonErrorCount{0};
    static std::atomic<unsigned long> lastJsonErrorLog{0};
    uint32_t count = ++jsonErrorCount;
    unsigned long now = millis();
    unsigned long lastLog = lastJsonErrorLog.load();
    if (count <= 3 || now - lastLog > 30000) {
      // v11.179: Protection contre NULL pointer (fix crash LoadProhibited)
      const char* errCStr = err.c_str();
      if (errCStr) {
        LOG(LOG_DEBUG, "[HTTP] JSON parse error: %s (total: %u)", errCStr, count);
      } else {
        LOG(LOG_DEBUG, "[HTTP] JSON parse error: (unknown) (total: %u)", count);
      }
      // Diagnostic troncature: IncompleteInput souvent = body tronqué (buffer trop petit ou lecture arrêtée trop tôt)
      Serial.printf("[HTTP] parse fail: err=%s totalRead=%u maxBuf=%u contentLength=%d heap=%u\n",
                    errCStr ? errCStr : "?", (unsigned)totalRead, (unsigned)MAX_RESPONSE_SIZE,
                    contentLength, (unsigned)ESP.getFreeHeap());
      lastJsonErrorLog.store(now);
    }
    return loadFromNVSFallback(doc);
  }

  // Si le serveur renvoie un objet imbriqué {"outputs": {...}} ou {"switches": {...}},
  // copier l'objet interne dans doc pour que GPIOParser et processFetchedRemoteConfig voient les clés.
  bool didUnwrap = false;
  JsonObject inner;
  if (doc["outputs"].is<JsonObject>()) {
    inner = doc["outputs"].as<JsonObject>();
    didUnwrap = true;
  } else if (doc["switches"].is<JsonObject>()) {
    inner = doc["switches"].as<JsonObject>();
    didUnwrap = true;
    Serial.println(F("[HTTP] Réponse outputs/state: objet \"switches\" détecté, déplié"));
  }
  if (didUnwrap && !inner.isNull()) {
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
    for (JsonPair p : inner) {
      tmp[p.key().c_str()] = p.value();
    }
    doc.clear();
    for (JsonPair p : tmp.as<JsonObject>()) {
      doc[p.key().c_str()] = p.value();
    }
    Serial.println(F("[HTTP] Réponse outputs/state: objet imbriqué déplié"));
  }
  // #region agent log
  Serial.printf("[DBG] H2 afterUnwrap docSize=%u\n", (unsigned)doc.size());
  // #endregion

  // Diagnostic: distinguer serveur (réponse vide) vs ESP (parsing/application)
  Serial.printf("[HTTP] GET outputs/state: body=%u bytes, doc keys=%u (0=vide serveur, ~28=OK)\n",
                (unsigned)totalRead, (unsigned)doc.size());

  return true;
}

// v11.165: Fonction helper pour fallback NVS (audit offline-first)
// v11.186: Parse dans un document temporaire pour éviter LoadProhibited dans doc.clear()
// v11.189: Ne plus appeler doc.clear() — ArduinoJson MemoryPoolList::clear() peut crasher
// (LoadProhibited à 0x2714) si le doc a déjà été clear() par le caller ou est dans un état
// incohérent. On copie tmpDoc → doc sans clear ; les clés obsolètes restent mais le caller
// ne lit que les clés connues.
bool WebClient::loadFromNVSFallback(JsonDocument& doc) {
  char cachedJson[1024];
  if (!config.loadRemoteVars(cachedJson, sizeof(cachedJson))) {
    return false;
  }
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmpDoc;
  DeserializationError err = deserializeJson(tmpDoc, cachedJson);
  if (err) {
    return false;
  }
  // Ne pas appeler doc.clear() : évite crash dans MemoryPoolList::clear() (EXCVADDR 0x2714)
  for (JsonPair p : tmpDoc.as<JsonObject>()) {
    doc[p.key().c_str()] = p.value();
  }
  LOG(LOG_INFO, "[HTTP] Utilisation cache NVS (fallback)");
  return true;
}

bool WebClient::sendHeartbeat(const Diagnostics& diag) {
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  const DiagnosticStats& s = diag.getStats();
  
  char payloadBuf[256];
  snprintf(payloadBuf, sizeof(payloadBuf), "uptime=%lu&free=%u&min=%u&reboots=%u",
           s.uptimeSec, s.freeHeap, s.minFreeHeap, s.rebootCount);
  
  char bufCrc[16];
  snprintf(bufCrc, sizeof(bufCrc), "&crc=%08lX", crc32(payloadBuf));
  
  char fullPayload[272];
  snprintf(fullPayload, sizeof(fullPayload), "%s%s", payloadBuf, bufCrc);
  
  char resp[1024];
  char heartbeatUrl[256];
  ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
  return httpRequest(heartbeatUrl, fullPayload, resp, sizeof(resp));
}

bool WebClient::postRaw(const char* payload) {
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  if (payload == nullptr) {
    return false;
  }
  
  WiFi.setSleep(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Buffer sur la stack
  char postBuffer[BufferConfig::POST_PAYLOAD_MAX_SIZE];
  size_t payloadLen = strlen(payload);
  size_t estimatedSize = payloadLen + strlen(_apiKey) + strlen(ProjectConfig::BOARD_TYPE) + 32;
  if (estimatedSize > sizeof(postBuffer)) {
    return false;
  }

  bool hasApi = (strncmp(payload, "api_key=", 8) == 0);
  if (!hasApi) {
    snprintf(postBuffer, sizeof(postBuffer), "api_key=%s&sensor=%s", _apiKey, ProjectConfig::BOARD_TYPE);
    if (payloadLen > 0) {
      size_t currentLen = strlen(postBuffer);
      if (currentLen + 1 < sizeof(postBuffer)) {
        strncat(postBuffer, "&", sizeof(postBuffer) - currentLen - 1);
      }
      currentLen = strlen(postBuffer);
      if (currentLen < sizeof(postBuffer)) {
        strncat(postBuffer, payload, sizeof(postBuffer) - currentLen - 1);
      }
    }
  } else {
    strncpy(postBuffer, payload, sizeof(postBuffer) - 1);
    postBuffer[sizeof(postBuffer) - 1] = '\0';
  }

  char respPrimary[1024];
  char postDataUrl[256];
  ServerConfig::getPostDataUrl(postDataUrl, sizeof(postDataUrl));
  bool success = httpRequest(postDataUrl, postBuffer, respPrimary, sizeof(respPrimary));
  
  // v11.171: Queue si échec (offline-first)
  if (!success && WiFi.status() != WL_CONNECTED) {
    queueFailedPost(postBuffer);
  }
  
  return success;
}

// =============================================================================
// v11.171: Queue persistante pour POSTs échoués (offline-first)
// Stocke jusqu'à MAX_QUEUED_POSTS payloads dans NVS pour ré-envoi ultérieur
// =============================================================================

bool WebClient::queueFailedPost(const char* payload) {
  if (payload == nullptr || strlen(payload) == 0) {
    return false;
  }
  
  size_t payloadLen = strlen(payload);
  if (payloadLen >= MAX_POST_SIZE) {
    LOG(LOG_WARN, "[HTTP] Payload trop long pour queue: %u bytes", payloadLen);
    return false;
  }
  
  // v11.178: Utilisation des clés NVS centralisées (audit nvs-keys)
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  // Queue pleine: supprimer le plus ancien (FIFO)
  if (count >= MAX_QUEUED_POSTS) {
    // Décaler tous les éléments
    for (int i = 0; i < count - 1; i++) {
      char srcKey[16], dstKey[16];
      snprintf(srcKey, sizeof(srcKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i + 1);
      snprintf(dstKey, sizeof(dstKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
      
      char tempPayload[MAX_POST_SIZE];
      g_nvsManager.loadString(NVS_NAMESPACES::STATE, srcKey, tempPayload, sizeof(tempPayload), "");
      if (strlen(tempPayload) > 0) {
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, dstKey, tempPayload);
      }
    }
    count = MAX_QUEUED_POSTS - 1;
  }
  
  // Ajouter le nouveau payload
  char key[16];
  snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, count);
  g_nvsManager.saveString(NVS_NAMESPACES::STATE, key, payload);
  count++;
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count);
  
  LOG(LOG_INFO, "[HTTP] POST mis en queue (total: %d)", count);
  return true;
}

bool WebClient::processQueuedPosts() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  if (count == 0) {
    return true;  // Rien à traiter
  }
  
  LOG(LOG_INFO, "[HTTP] Traitement queue: %d POSTs en attente", count);
  
  int processed = 0;
  int failed = 0;
  
  for (int i = 0; i < count && i < MAX_QUEUED_POSTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
    
    char payload[MAX_POST_SIZE];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, key, payload, sizeof(payload), "");
    
    if (strlen(payload) == 0) {
      continue;
    }
    
    char resp[1024];
    char postDataUrl[256];
    ServerConfig::getPostDataUrl(postDataUrl, sizeof(postDataUrl));
    
    if (httpRequest(postDataUrl, payload, resp, sizeof(resp))) {
      processed++;
      // Marquer comme traité (effacer)
      g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key);
    } else {
      failed++;
      // Arrêter si échec (réseau peut être parti)
      if (WiFi.status() != WL_CONNECTED) {
        LOG(LOG_WARN, "[HTTP] WiFi perdu, arrêt traitement queue");
        break;
      }
    }
    
    // Délai entre requêtes
    vTaskDelay(pdMS_TO_TICKS(500));
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
  }
  
  // Mettre à jour le compteur
  int remaining = count - processed;
  if (remaining < 0) remaining = 0;
  
  // Compacter la queue si nécessaire
  if (processed > 0 && remaining > 0) {
    int newIdx = 0;
    for (int i = 0; i < count; i++) {
      char srcKey[16];
      snprintf(srcKey, sizeof(srcKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
      
      char payload[MAX_POST_SIZE];
      g_nvsManager.loadString(NVS_NAMESPACES::STATE, srcKey, payload, sizeof(payload), "");
      
      if (strlen(payload) > 0) {
        if (newIdx != i) {
          char dstKey[16];
          snprintf(dstKey, sizeof(dstKey), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, newIdx);
          g_nvsManager.saveString(NVS_NAMESPACES::STATE, dstKey, payload);
          g_nvsManager.removeKey(NVS_NAMESPACES::STATE, srcKey);
        }
        newIdx++;
      }
    }
    remaining = newIdx;
  }
  
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, remaining);
  
  LOG(LOG_INFO, "[HTTP] Queue traitée: %d envoyés, %d échoués, %d restants", 
      processed, failed, remaining);
  
  return (failed == 0);
}

uint8_t WebClient::getQueuedPostsCount() {
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  return (uint8_t)count;
}

void WebClient::clearQueuedPosts() {
  int count = 0;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, count, 0);
  
  for (int i = 0; i < count && i < MAX_QUEUED_POSTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVSKeys::WebClient::POST_Q_PREFIX, i);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key);
  }
  
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::WebClient::POST_Q_COUNT, 0);
  LOG(LOG_INFO, "[HTTP] Queue vidée (%d entrées supprimées)", count);
}
