// v11.162: WebClient simplifié - HTTP par défaut (plus de TLS pour requêtes courantes)
// Réduit fragmentation mémoire en éliminant le besoin de ~32KB contigu pour TLS
// v11.172: Les noms de variables POST sont définis dans gpio_mapping.h (VariableRegistry)
// Source de vérité: GPIOMap::XXX.serverPostName (ex: "chauffageThreshold")
// v11.171: Queue persistante POSTs échoués (offline-first)
#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "gpio_mapping.h"  // SensorMap, GPIOMap (source unique noms POST)
#include "log.h"
#include "config.h"
#include "nvs_manager.h"
#include "nvs_keys.h"  // v11.178: Utilisation des clés NVS centralisées (audit)
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>
#include <atomic>

// Mutex pour sérialiser HTTP (netTask GET et postSenderTask POST)
static SemaphoreHandle_t s_httpMutex = nullptr;

// Buffer pour dernier GET outputs/state : rempli par fetchRemoteState (netTask), lu par copyLastFetchedTo (caller) — évite LoadProhibited (écrire doc depuis netTask)
static char s_lastFetchedJson[BufferConfig::OUTPUTS_STATE_READ_BUFFER_SIZE + 1];
static size_t s_lastFetchedSize = 0;

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
                           char* response, size_t responseSize,
                           uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  if (url == nullptr || response == nullptr || responseSize == 0) {
    return false;
  }

  if (s_httpMutex == nullptr) {
    s_httpMutex = xSemaphoreCreateMutex();
  }
  if (s_httpMutex != nullptr && xSemaphoreTake(s_httpMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  // v11.XXX: Nettoyage systématique du client avant réutilisation (fiabilité WiFi capricieux)
  // Fermer connexion précédente si encore ouverte pour éviter états incohérents
  if (_client.connected()) {
    _client.stop();  // Fermer explicitement la connexion TCP
  }
  _http.end();  // S'assurer que HTTPClient est nettoyé

  uint32_t freeHeap = ESP.getFreeHeap();
  size_t payloadLen = payload ? strlen(payload) : 0;
  // Durée = de l'entrée dans httpRequest jusqu'à la fin (inclut délai inter-requêtes si applicable,
  // begin(), envoi body, attente réponse, lecture body, et éventuels retries)
  uint32_t requestStartMs = millis();
  const bool debugLogging = (LOG_LEVEL >= LOG_DEBUG) && LogConfig::SERIAL_ENABLED;

  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] POST %s (payload=%u bytes)", url, payloadLen);
    LOG(LOG_DEBUG, "[HTTP] Heap libre=%u bytes", freeHeap);
  }
  // Un log avec l'URL permet au script diagnostic_serveur_distant.ps1 de détecter l'endpoint (post-data vs post-data-test)
  if (LogConfig::SERIAL_ENABLED) {
    Serial.printf("[HTTP] POST %s\n", url);
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
  WIFI_APPLY_MODEM_SLEEP(false);

  const int MAX_ATTEMPTS = 2;
  int code = -1;
  response[0] = '\0';
  bool success = false;
  uint32_t connectMs = 0;  // DNS + TCP jusqu'à begin() réussi (pour diagnostic latence)

  for (int attempt = 0; attempt < MAX_ATTEMPTS && !success; attempt++) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();  // P1: feed WDT en début de chaque tentative (évite TWDT si POST long)
    }
    unsigned long attemptStartMs = millis();

    // Timeout global: vérifier AVANT begin() pour éviter end() sur état incohérent
    uint32_t elapsedMs = millis() - requestStartMs;
    if (elapsedMs >= timeoutMs) {
      if (LogConfig::SERIAL_ENABLED) {
        Serial.printf("[HTTP] POST timeout après %u ms\n", elapsedMs);
      }
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u ms", elapsedMs);
      if (s_httpMutex != nullptr) xSemaphoreGive(s_httpMutex);
      return false;
    }

    _http.setTimeout(timeoutMs);
    // v11.162: Utilise le client HTTP simple (membre de classe)
    // v11.XXX: Amélioration gestion erreur begin() avec nettoyage systématique
    if (!_http.begin(_client, url)) {
      LOG(LOG_WARN, "[HTTP] begin() failed for URL");
      _http.end();  // Nettoyer même en cas d'échec
      if (_client.connected()) {
        _client.stop();  // Fermer connexion si ouverte
      }
      vTaskDelay(pdMS_TO_TICKS(500));  // Délai plus long avant retry (WiFi instable)
      continue;  // Retry
    }
    connectMs = (uint32_t)(millis() - requestStartMs);  // DNS + TCP (diagnostic latence)
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    unsigned long postStartMs = millis();
    code = _http.POST(payload ? payload : "");
    unsigned long postDurationMs = millis() - postStartMs;
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    // v11.XXX: Vérification timeout après POST() pour détecter dépassements
    uint32_t totalElapsedMs = millis() - requestStartMs;
    if (totalElapsedMs > timeoutMs) {
      if (LogConfig::SERIAL_ENABLED) {
        Serial.printf("[HTTP] POST a dépassé le timeout: %u ms (limite: %u ms)\n", totalElapsedMs, timeoutMs);
      }
      LOG(LOG_WARN, "[HTTP] POST timeout dépassé: %u ms (limite: %u ms)", totalElapsedMs, timeoutMs);
      // Si code négatif (échec), considérer comme timeout
      if (code <= 0) {
        _http.end();
        if (_client.connected()) {
          _client.stop();
        }
        continue;  // Retry
      }
      // Si code > 0 (succès malgré dépassement), continuer (serveur a répondu)
    }
    
    if (code > 0) {
      // Lire la réponse
      const size_t MAX_TEMP_RESPONSE = 1024;
      char tempResponseBuffer[MAX_TEMP_RESPONSE + 1];
      WiFiClient* stream = _http.getStreamPtr();
      size_t responseLen = 0;
      
      if (stream) {
        // v11.176: Ajout timeout pour éviter blocage indéfini (audit robustesse)
        const unsigned long streamReadStart = millis();
        const unsigned long STREAM_READ_TIMEOUT_MS = timeoutMs;
        uint8_t emptyReads = 0;
        const uint8_t MAX_EMPTY_READS = 10;
        
        while (responseLen < MAX_TEMP_RESPONSE && 
               (millis() - streamReadStart) < STREAM_READ_TIMEOUT_MS) {
          if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
          }
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
        // Drain octets restants de la réponse avant end() (évite "still data in buffer" / timeout readBytes)
        if (code >= 200 && code < 400) {
          unsigned long drainStart = millis();
          const unsigned long POST_DRAIN_TIMEOUT_MS = 500;
          size_t discarded = 0;
          char trash[128];
          while (stream->available() > 0 && discarded < 4096 &&
                 (millis() - drainStart) < POST_DRAIN_TIMEOUT_MS) {
            if (esp_task_wdt_status(NULL) == ESP_OK) {
              esp_task_wdt_reset();
            }
            size_t n = stream->readBytes(trash, sizeof(trash));
            if (n == 0) {
              vTaskDelay(pdMS_TO_TICKS(10));
              continue;
            }
            discarded += n;
            drainStart = millis();
          }
        }
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
    // v11.XXX: Nettoyage systématique - toujours appeler end() même si client déconnecté
    _http.end();  // Toujours nettoyer HTTPClient
    if (_client.connected()) {
      _client.stop();  // Fermer explicitement la connexion TCP
    }
    // Réinitialiser HTTPClient pour prochain appel (timeout standard 5s sauf POST 7s)
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

    // Backoff avant retry avec vérification WiFi
    if (attempt + 1 < MAX_ATTEMPTS) {
      // v11.XXX: Backoff exponentiel pour WiFi instable (1s, 2s)
      int backoff = NetworkConfig::BACKOFF_BASE_MS * (1 << attempt);
      vTaskDelay(pdMS_TO_TICKS(backoff));
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      // Vérifier WiFi avant retry (peut avoir été perdu pendant backoff)
      if (WiFi.status() != WL_CONNECTED) {
        LOG(LOG_WARN, "[HTTP] WiFi perdu pendant backoff");
        break;
      }
    }
  }

  // Log final (v11.182: copie locale pour éviter tout usage de state HTTP après end())
  // connect_ms = DNS + TCP jusqu'à begin() ; request_ms = envoi body + serveur + réception (diagnostic latence)
  unsigned long totalDurationMs = millis() - requestStartMs;
  unsigned long requestPhaseMs = (connectMs > 0 && totalDurationMs >= connectMs) ? (totalDurationMs - connectMs) : totalDurationMs;
  const int finalCode = code;
  const bool finalSuccess = success;
  const char* succStr = finalSuccess ? "oui" : "non";
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  LOG(LOG_INFO, "[HTTP] Requête: %lu ms, connect_ms=%lu, request_ms=%lu, code=%d, succès=%s, rssi=%d",
      totalDurationMs, (unsigned long)connectMs, requestPhaseMs, finalCode, succStr, rssi);

  _lastRequestMs = millis();

  if (!finalSuccess) {
    if (response && responseSize > 0) {
      response[0] = '\0';
    }
  }
  if (s_httpMutex != nullptr) xSemaphoreGive(s_httpMutex);
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

  // Noms POST centralisés: SensorMap (capteurs), GPIOMap (actionneurs)
  appendKV("version", ProjectConfig::VERSION);
  appendKV(SensorMap::TEMP_AIR.serverPostName, buf_tempAir);
  appendKV(SensorMap::HUMIDITY.serverPostName, buf_humid);
  appendKV(SensorMap::TEMP_WATER.serverPostName, buf_tempWater);
  appendKV(SensorMap::WL_POTA.serverPostName, buf_wlPota);
  appendKV(SensorMap::WL_AQUA.serverPostName, buf_wlAqua);
  appendKV(SensorMap::WL_TANK.serverPostName, buf_wlTank);
  appendKV(SensorMap::DIFF_MAREE.serverPostName, buf_diffMaree);
  appendKV(SensorMap::LUMINOSITY.serverPostName, buf_lum);
  appendKV(GPIOMap::PUMP_AQUA.serverPostName, buf_pumpAqua);
  appendKV(GPIOMap::PUMP_TANK.serverPostName, buf_pumpTank);
  appendKV(GPIOMap::HEATER.serverPostName, buf_heat);
  appendKV(GPIOMap::LIGHT.serverPostName, buf_uv);

  if (includeReset) {
    appendKV(GPIOMap::RESET_CMD.serverPostName, "0");
  }

  if (truncated) {
    return false;
  }
  
  return postRaw(payload);
}

int WebClient::tryFetchConfigFromServer(JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return 0;
  // #region agent log
  bool recvEn = config.isRemoteRecvEnabled();
  Serial.printf("[DBG] tryFetchConfig recvEnabled=%d hypothesis=H2\n", recvEn ? 1 : 0);
  // #endregion
  if (!recvEn) return 0;
  return fetchRemoteState(doc);
}

bool WebClient::tryPushStatusToServer(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!config.isRemoteSendEnabled()) return false;
  if (payload == nullptr) return false;
  return postRaw(payload);
}

// v11.193: Retourne 0=échec, 1=OK HTTP, 2=OK NVS fallback (caller ne doit pas itérer sur doc si 2)
int WebClient::fetchRemoteState(JsonDocument& doc) {
  if (!config.isRemoteRecvEnabled()) {
    return 0;
  }
  
  // v11.165: Fallback NVS si WiFi non disponible (audit offline-first)
  if (WiFi.status() != WL_CONNECTED) {
    return loadFromNVSFallback(doc) ? 2 : 0;
  }

  // v11.XXX: Nettoyage systématique du client avant réutilisation (fiabilité WiFi capricieux)
  if (_client.connected()) {
    _client.stop();  // Fermer connexion précédente si encore ouverte
  }
  _http.end();  // S'assurer que HTTPClient est nettoyé

  WIFI_APPLY_MODEM_SLEEP(false);
  vTaskDelay(pdMS_TO_TICKS(100));

  const size_t MAX_RESPONSE_SIZE = BufferConfig::OUTPUTS_STATE_READ_BUFFER_SIZE;
  char payloadBuffer[MAX_RESPONSE_SIZE + 1];
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));
  // #region agent log
  Serial.printf("[DBG] fetchRemoteState URL=%s hypothesis=H1\n", url);
  // #endregion

  // Timeout dédié plus long pour GET outputs/state (évite -11 read timeout si serveur/latence lents)
  _http.setTimeout(NetworkConfig::OUTPUTS_STATE_HTTP_TIMEOUT_MS);

  // v11.162: Utilise le client HTTP simple (membre de classe)
  // v11.166: Verification retour http.begin() (audit robustesse)
  // v11.XXX: Amélioration gestion erreur begin() avec nettoyage systématique
  if (!_http.begin(_client, url)) {
    LOG(LOG_WARN, "[HTTP] Echec initialisation HTTPClient pour %s", url);
    _http.end();  // Nettoyer même en cas d'échec
    if (_client.connected()) {
      _client.stop();  // Fermer connexion si ouverte
    }
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc) ? 2 : 0;
  }

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  int code = _http.GET();
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  // Retry une fois sur timeout lecture (-11) pour améliorer le taux de succès GET
  if (code == -11) {
    _http.end();  // Nettoyer avant retry
    if (_client.connected()) {
      _client.stop();  // Fermer connexion avant retry
    }
    _http.setReuse(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    // Vérifier WiFi avant retry
    if (WiFi.status() != WL_CONNECTED) {
      LOG(LOG_WARN, "[HTTP] WiFi perdu avant retry GET");
      _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
      return loadFromNVSFallback(doc) ? 2 : 0;
    }
    if (_http.begin(_client, url)) {
      _http.setTimeout(NetworkConfig::OUTPUTS_STATE_HTTP_TIMEOUT_MS);
      code = _http.GET();
    } else {
      // Échec begin() lors du retry
      _http.end();
      if (_client.connected()) {
        _client.stop();
      }
      _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
      return loadFromNVSFallback(doc) ? 2 : 0;
    }
  }

  if (code <= 0) {
    Serial.printf("[HTTP] outputs/state: code=%d (skip read)\n", code);
    // #region agent log
    Serial.printf("[DBG] fetchRemoteState code=%d useNVS=%d hypothesis=H3\n", code, loadFromNVSFallback(doc) ? 1 : 0);
    // #endregion
    // v11.XXX: Nettoyage systématique même en cas d'échec
    _http.end();  // Toujours nettoyer HTTPClient
    if (_client.connected()) {
      _client.stop();  // Fermer connexion TCP
    }
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    if (loadFromNVSFallback(doc)) {
      return 2;
    }
    if (LogConfig::SERIAL_ENABLED) {
      Serial.printf("[HTTP] GET outputs/state: échec code=%d (pas de cache NVS)\n", code);
    }
    return 0;
  }

  // v11.175: Amélioration lecture HTTP - gestion Content-Length et chunked
  int contentLength = _http.getSize();
  bool hasContentLength = (contentLength > 0);
  size_t totalRead = 0;  // Déclaré ici pour être accessible après le if/else
  
  WiFiClient* stream = _http.getStreamPtr();
  if (!stream) {
    LOG(LOG_WARN, "[HTTP] Pas de stream disponible, fallback NVS");
    // v11.XXX: Nettoyage systématique même en cas d'échec
    _http.end();  // Toujours nettoyer HTTPClient
    if (_client.connected()) {
      _client.stop();  // Fermer connexion TCP
    }
    _http.setReuse(false);
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
    return loadFromNVSFallback(doc) ? 2 : 0;
  }

  // v11.185: Quand Content-Length est connu, lecture en un bloc (évite body déjà consommé par
  // le stack TCP/HTTP si on attend entre available() et read).
  if (hasContentLength && (size_t)contentLength <= MAX_RESPONSE_SIZE) {
    stream->setTimeout(3000);
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    totalRead = stream->readBytes(payloadBuffer, (size_t)contentLength);
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    payloadBuffer[totalRead] = '\0';
  }

  if (totalRead == 0) {
    // Pas de Content-Length ou lecture bloc a échoué: boucle classique (réponse chunked)
    unsigned long globalReadStart = millis();
    const unsigned long READ_TIMEOUT_MS = NetworkConfig::OUTPUTS_STATE_HTTP_TIMEOUT_MS;
    uint8_t emptyReads = 0;
    const uint8_t MAX_EMPTY_READS = NetworkConfig::OUTPUTS_STATE_MAX_EMPTY_READS;

    while (totalRead < MAX_RESPONSE_SIZE && (millis() - globalReadStart) < READ_TIMEOUT_MS) {
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
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

  // Drain restant du stream avant end() pour éviter "still data in buffer" et IncompleteInput
  while (stream->available() > 0 && totalRead < MAX_RESPONSE_SIZE) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    size_t n = stream->readBytes(
      payloadBuffer + totalRead,
      MAX_RESPONSE_SIZE - totalRead
    );
    if (n == 0) break;
    totalRead += n;
    payloadBuffer[totalRead] = '\0';
    vTaskDelay(pdMS_TO_TICKS(10));  // Yield net task (évite WDT si flux gros)
  }

  // Chunked: laisser le temps aux derniers chunks d'arriver (évite IncompleteInput)
  if (!hasContentLength && totalRead > 0) {
    vTaskDelay(pdMS_TO_TICKS(400));
  }
  // Seconde passe: lire tout ce qui est disponible dans le buffer (chunked peut livrer en retard)
  const unsigned long SECOND_DRAIN_MS = 1000;
  unsigned long secondDrainStart = millis();
  while (stream->available() > 0 && totalRead < MAX_RESPONSE_SIZE &&
         (millis() - secondDrainStart) < SECOND_DRAIN_MS) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    size_t n = stream->readBytes(
      payloadBuffer + totalRead,
      MAX_RESPONSE_SIZE - totalRead
    );
    if (n > 0) {
      totalRead += n;
      payloadBuffer[totalRead] = '\0';
      secondDrainStart = millis();
    } else {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }

  // Discard octets restants quand buffer plein ou chunked non fini (évite "still data in buffer")
  size_t discarded = 0;
  const size_t MAX_DISCARD = 16384;
  const unsigned long DISCARD_TIMEOUT_MS = 2000;
  unsigned long discardStart = millis();
  char trash[256];
  while (stream->available() > 0 && discarded < MAX_DISCARD &&
         (millis() - discardStart) < DISCARD_TIMEOUT_MS) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    size_t n = stream->readBytes(trash, sizeof(trash));
    if (n == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    discarded += n;
    discardStart = millis();
  }
  if (discarded > 0) {
    Serial.printf("[HTTP] outputs/state: drain discard=%u (totalRead=%u)\n",
                  (unsigned)discarded, (unsigned)totalRead);
  }

  Serial.printf("[HTTP] outputs/state: code=%d contentLength=%d totalRead=%u\n",
                code, contentLength, (unsigned)totalRead);

  if (totalRead >= MAX_RESPONSE_SIZE || (hasContentLength && contentLength > (int)MAX_RESPONSE_SIZE)) {
    LOG(LOG_WARN, "[HTTP] Réponse outputs/state tronquée (lu=%u, max=%u, contentLength=%d)",
        (unsigned)totalRead, (unsigned)MAX_RESPONSE_SIZE, contentLength);
  }
  // v11.XXX: Nettoyage systématique après lecture réussie
  _http.end();  // Toujours nettoyer HTTPClient
  if (_client.connected()) {
    _client.stop();  // Fermer connexion TCP
  }
  _http.setReuse(false);
  _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);

  if (totalRead == 0) {
    return loadFromNVSFallback(doc) ? 2 : 0;
  }

  // Trouver le début du JSON
  char* jsonStart = strchr(payloadBuffer, '{');
  if (!jsonStart) {
    jsonStart = strchr(payloadBuffer, '[');
  }
  
  // v11.172: Si pas de JSON valide trouvé, fallback silencieux (normal si serveur non configuré)
  if (!jsonStart) {
    return loadFromNVSFallback(doc) ? 2 : 0;
  }
  
  if (jsonStart != payloadBuffer) {
    size_t offset = jsonStart - payloadBuffer;
    size_t newSize = totalRead - offset;
    memmove(payloadBuffer, jsonStart, newSize);
    payloadBuffer[newSize] = '\0';
  }

  // Parser dans un document LOCAL (stack netTask) — ne jamais écrire dans doc depuis netTask (LoadProhibited)
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> parseDoc;
  DeserializationError err = deserializeJson(parseDoc, payloadBuffer);
  if (err) {
    // #region agent log
    Serial.printf("[DBG] fetchRemoteState parse fail err=%s totalRead=%u hypothesis=H4\n", err.c_str(), (unsigned)totalRead);
    // #endregion
    static std::atomic<uint32_t> jsonErrorCount{0};
    static std::atomic<unsigned long> lastJsonErrorLog{0};
    uint32_t count = ++jsonErrorCount;
    unsigned long now = millis();
    unsigned long lastLog = lastJsonErrorLog.load();
    if (count <= 3 || now - lastLog > 30000) {
      const char* errCStr = err.c_str();
      Serial.printf("[HTTP] parse fail: err=%s totalRead=%u heap=%u\n",
                    errCStr ? errCStr : "?", (unsigned)totalRead, (unsigned)ESP.getFreeHeap());
      lastJsonErrorLog.store(now);
    }
    return loadFromNVSFallback(doc) ? 2 : 0;
  }

  // Stocker le JSON brut pour que le caller (même tâche que doc) fasse copyLastFetchedTo(doc)
  size_t jsonLen = strlen(payloadBuffer);
  if (jsonLen >= sizeof(s_lastFetchedJson)) jsonLen = sizeof(s_lastFetchedJson) - 1;
  memcpy(s_lastFetchedJson, payloadBuffer, jsonLen);
  s_lastFetchedJson[jsonLen] = '\0';
  s_lastFetchedSize = jsonLen;

  Serial.printf("[HTTP] GET outputs/state: body=%u bytes, stored for copyLastFetchedTo\n", (unsigned)totalRead);
  // #region agent log
  Serial.printf("[DBG] fetchRemoteState result=1 totalRead=%u hypothesis=H1,H3,H4\n", (unsigned)totalRead);
  // #endregion
  return 1;  // OK depuis HTTP
}

bool WebClient::copyLastFetchedTo(ArduinoJson::JsonDocument& doc) {
  if (s_lastFetchedSize == 0 || s_lastFetchedJson[0] == '\0') {
    // #region agent log
    Serial.printf("[DBG] copyLastFetchedTo skip size=%u hypothesis=H4\n", (unsigned)s_lastFetchedSize);
    // #endregion
    return false;
  }
  DeserializationError err = deserializeJson(doc, s_lastFetchedJson);
  if (err) {
    // #region agent log
    Serial.printf("[DBG] copyLastFetchedTo deserialize err=%s hypothesis=H4\n", err.c_str());
    // #endregion
    return false;
  }
  // Unwrap outputs/switches si présent (même logique qu'avant, exécutée dans le contexte du caller)
  bool didUnwrap = false;
  JsonObject inner;
  if (doc["outputs"].is<JsonObject>()) {
    inner = doc["outputs"].as<JsonObject>();
    didUnwrap = true;
  } else if (doc["switches"].is<JsonObject>()) {
    inner = doc["switches"].as<JsonObject>();
    didUnwrap = true;
  }
  if (didUnwrap && !inner.isNull()) {
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
    char keyBuf[64];
    for (JsonPair p : inner) {
      strncpy(keyBuf, p.key().c_str(), sizeof(keyBuf) - 1);
      keyBuf[sizeof(keyBuf) - 1] = '\0';
      tmp[keyBuf] = p.value();
    }
    doc.clear();
    for (JsonPair p : tmp.as<JsonObject>()) {
      strncpy(keyBuf, p.key().c_str(), sizeof(keyBuf) - 1);
      keyBuf[sizeof(keyBuf) - 1] = '\0';
      doc[keyBuf] = p.value();
    }
  }
  size_t nKeys = doc.size();
  int has108 = doc.containsKey("108") ? 1 : 0;
  int has109 = doc.containsKey("109") ? 1 : 0;
  // #region agent log
  Serial.printf("[DBG] copyLastFetchedTo ok docSize=%u unwrap=%d has108=%d has109=%d hypothesis=H4,H6\n",
                 (unsigned)nKeys, didUnwrap ? 1 : 0, has108, has109);
  // #endregion
  return true;
}

// v11.165: Fonction helper pour fallback NVS (audit offline-first)
// v11.194: Ne plus écrire dans doc — l'écriture doc[key]=value provoque LoadProhibited (EXCVADDR 0x4)
// quand doc est sur la stack d'une autre tâche. Le caller (fetchRemoteState) ne lit pas doc quand
// fromNVSFallback, donc on valide seulement la présence du cache NVS et on retourne true.
bool WebClient::loadFromNVSFallback(JsonDocument& doc) {
  (void)doc;
  char cachedJson[1024];
  if (!config.loadRemoteVars(cachedJson, sizeof(cachedJson))) {
    return false;
  }
  // Valider que le JSON est parsable (évite retourner true avec cache corrompu)
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmpDoc;
  DeserializationError err = deserializeJson(tmpDoc, cachedJson);
  if (err) {
    return false;
  }
  LOG(LOG_INFO, "[HTTP] Utilisation cache NVS (fallback)");
  return true;
}

bool WebClient::buildHeartbeatPayload(const Diagnostics& diag, char* buf, size_t bufSize) {
  if (buf == nullptr || bufSize < 320) return false;
  const DiagnosticStats& s = diag.getStats();
  char payloadBuf[256];
  snprintf(payloadBuf, sizeof(payloadBuf), "uptime=%lu&free=%u&min=%u&reboots=%u",
           s.uptimeSec, s.freeHeap, s.minFreeHeap, s.rebootCount);
  char bufCrc[16];
  snprintf(bufCrc, sizeof(bufCrc), "&crc=%08lX", crc32(payloadBuf));
  IPAddress addr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
  snprintf(buf, bufSize, "%s%s&ip=%s", payloadBuf, bufCrc, ipStr);
  return true;
}

bool WebClient::postToUrl(const char* url, const char* payload, uint32_t timeoutMs) {
  if (url == nullptr || payload == nullptr) return false;
  char resp[1024];
  return httpRequest(url, payload, resp, sizeof(resp), timeoutMs);
}

bool WebClient::sendHeartbeat(const Diagnostics& diag) {
  if (!config.isRemoteSendEnabled()) return false;
  char fullPayload[320];
  if (!buildHeartbeatPayload(diag, fullPayload, sizeof(fullPayload))) return false;
  char heartbeatUrl[256];
  ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
  return postToUrl(heartbeatUrl, fullPayload, NetworkConfig::HTTP_TIMEOUT_MS);
}

bool WebClient::postRaw(const char* payload) {
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  if (payload == nullptr) {
    return false;
  }
  
  WIFI_APPLY_MODEM_SLEEP(false);
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
  bool success = httpRequest(postDataUrl, postBuffer, respPrimary, sizeof(respPrimary),
                             NetworkConfig::HTTP_POST_TIMEOUT_MS);
  
  // v11.171: Queue si échec (offline-first). Étendu aux échecs HTTP (timeout, 4xx/5xx) en plus du WiFi déconnecté.
  if (!success) {
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
