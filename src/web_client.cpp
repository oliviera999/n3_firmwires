// v11.162: WebClient simplifié - HTTP par défaut (plus de TLS pour requêtes courantes)
// Réduit fragmentation mémoire en éliminant le besoin de ~32KB contigu pour TLS
#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "log.h"
#include "config.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

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

  const int maxAttempts = 2;
  int code = -1;
  response[0] = '\0';
  bool success = false;

  for (int attempt = 0; attempt < maxAttempts && !success; attempt++) {
    unsigned long attemptStartMs = millis();
    
    // v11.162: Utilise le client HTTP simple (membre de classe)
    _http.begin(_client, url);
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    // Timeout global
    uint32_t elapsedMs = millis() - requestStartMs;
    if (elapsedMs >= NetworkConfig::HTTP_TIMEOUT_MS) {
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u ms", elapsedMs);
      _http.end();
      return false;
    }
    
    code = _http.POST(payload ? payload : "");
    
    if (code > 0) {
      // Lire la réponse
      const size_t MAX_TEMP_RESPONSE = 1024;
      char tempResponseBuffer[MAX_TEMP_RESPONSE + 1];
      WiFiClient* stream = _http.getStreamPtr();
      size_t responseLen = 0;
      
      if (stream) {
        while (stream->available() && responseLen < MAX_TEMP_RESPONSE) {
          size_t bytesRead = stream->readBytes(
            tempResponseBuffer + responseLen,
            MAX_TEMP_RESPONSE - responseLen
          );
          if (bytesRead == 0) break;
          responseLen += bytesRead;
        }
        tempResponseBuffer[responseLen] = '\0';
      } else {
        // Fallback: getString() si aucun stream
        // Scope réduit pour destruction rapide de la String (minimise fragmentation)
        {
          String tempResponse = _http.getString();
          responseLen = tempResponse.length();
          if (responseLen >= MAX_TEMP_RESPONSE) {
            responseLen = MAX_TEMP_RESPONSE - 1;
          }
          if (responseLen > 0) {
            strncpy(tempResponseBuffer, tempResponse.c_str(), responseLen);
            tempResponseBuffer[responseLen] = '\0';
          } else {
            tempResponseBuffer[0] = '\0';
          }
        } // String détruite ici
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
      char errorBuf[64];
      strncpy(errorBuf, _http.errorToString(code).c_str(), sizeof(errorBuf) - 1);
      errorBuf[sizeof(errorBuf) - 1] = '\0';
      LOG(LOG_WARN, "[HTTP] Erreur %d: %s", code, errorBuf);
    }
    
    _http.end();
    
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
    if (attempt + 1 < maxAttempts) {
      int backoff = NetworkConfig::BACKOFF_BASE_MS * (attempt + 1);
      vTaskDelay(pdMS_TO_TICKS(backoff));
      esp_task_wdt_reset();
    }
  }

  // Log final
  unsigned long totalDurationMs = millis() - requestStartMs;
  if (debugLogging || !success) {
    LOG(LOG_INFO, "[HTTP] Requête: %lu ms, code=%d, succès=%s",
        totalDurationMs, code, success ? "oui" : "non");
  }

  _lastRequestMs = millis();
  
  if (!success) {
    response[0] = '\0';
  }
  return success;
}

bool WebClient::sendMeasurements(const Measurements& m, bool includeReset) {
  // Validation des mesures
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humid;

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

  const size_t MAX_RESPONSE_SIZE = BufferConfig::JSON_DOCUMENT_SIZE;
  char payloadBuffer[MAX_RESPONSE_SIZE + 1];
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));

  // v11.162: Utilise le client HTTP simple (membre de classe)
  // v11.166: Verification retour http.begin() (audit robustesse)
  if (!_http.begin(_client, url)) {
    LOG(LOG_WARN, "[HTTP] Echec initialisation HTTPClient pour %s", url);
    return loadFromNVSFallback(doc);
  }

  int code = _http.GET();

  if (code <= 0) {
    _http.end();
    // v11.165: Fallback NVS si HTTP échoue (audit offline-first)
    return loadFromNVSFallback(doc);
  }

  WiFiClient* stream = _http.getStreamPtr();
  if (!stream) {
    _http.end();
    return loadFromNVSFallback(doc);
  }

  size_t totalRead = 0;
  while (stream->available() && totalRead < MAX_RESPONSE_SIZE) {
    size_t bytesRead = stream->readBytes(
      payloadBuffer + totalRead,
      MAX_RESPONSE_SIZE - totalRead
    );
    if (bytesRead == 0) break;
    totalRead += bytesRead;
  }
  payloadBuffer[totalRead] = '\0';

  _http.end();

  if (totalRead == 0) {
    return loadFromNVSFallback(doc);
  }

  // Trouver le début du JSON
  char* jsonStart = strchr(payloadBuffer, '{');
  if (!jsonStart) {
    jsonStart = strchr(payloadBuffer, '[');
  }
  if (jsonStart && jsonStart != payloadBuffer) {
    size_t offset = jsonStart - payloadBuffer;
    size_t newSize = totalRead - offset;
    memmove(payloadBuffer, jsonStart, newSize);
    payloadBuffer[newSize] = '\0';
  }

  DeserializationError err = deserializeJson(doc, payloadBuffer);
  if (err) {
    LOG(LOG_WARN, "[HTTP] JSON parse error: %s", err.c_str());
    return loadFromNVSFallback(doc);
  }

  return true;
}

// v11.165: Fonction helper pour fallback NVS (audit offline-first)
bool WebClient::loadFromNVSFallback(JsonDocument& doc) {
  char cachedJson[1024];  // Buffer réduit pour économiser stack
  if (config.loadRemoteVars(cachedJson, sizeof(cachedJson))) {
    DeserializationError err = deserializeJson(doc, cachedJson);
    if (!err) {
      LOG(LOG_INFO, "[HTTP] Utilisation cache NVS (fallback)");
      return true;
    }
  }
  return false;  // Pas de log DEBUG pour économiser flash
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
  return httpRequest(postDataUrl, postBuffer, respPrimary, sizeof(respPrimary));
}
