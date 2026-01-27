#include "web_client.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "log.h"
#include "config.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <esp_heap_caps.h>  // Pour heap_caps_get_largest_free_block() (mode PROFILE_TEST uniquement)

// Diagnostic stack/tâche avant opérations TLS (point 4)
static void logTaskAndStack(const char* tag) {
  TaskHandle_t t = xTaskGetCurrentTaskHandle();
  const char* name = pcTaskGetName(t);
  UBaseType_t hwmWords = uxTaskGetStackHighWaterMark(t);
  uint32_t hwmBytes = static_cast<uint32_t>(hwmWords) * sizeof(StackType_t);
  Serial.printf("[%s] Task=%s handle=%p stackHWM=%u bytes\n",
                tag,
                name ? name : "?",
                static_cast<void*>(t),
                static_cast<unsigned>(hwmBytes));
}

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
  _client.setHandshakeTimeout(5000);  // 5s max (conforme .cursorrules: max 5s pour opérations réseau)
  // Désactivation du keep-alive : certaines déconnexions moitié-fermées
  // généraient un blocage interne du client HTTP, empêchant l'appel
  // suivant de se terminer et donc le rafraîchissement du watchdog.
  _http.setReuse(false);
  // NOUVEAU TIMEOUT NON-BLOQUANT (v11.50)
  _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS); // Timeout strict pour éviter blocage
}

// v11.150: Force la libération de mémoire TLS après une requête
// Version simplifiée : juste arrêter le client sans le recréer (évite crash FreeRTOS)
// PISTE 6: LWIP Guards
// Note: Les opérations LWIP (sys_untimeout, etc.) sont appelées par les bibliothèques
// ESP-IDF/Arduino sous-jacentes (WiFiClientSecure, HTTPClient). Ces bibliothèques
// gèrent normalement les locks TCPIP en interne. L'assert LWIP observé se produit
// probablement lors d'un cleanup après un panic TLS, ce qui est difficile à protéger
// directement depuis le code applicatif. La solution est de prévenir les panics TLS
// plutôt que de protéger les opérations LWIP après-coup.
void WebClient::resetTLSClient() {
  uint32_t heapBefore = ESP.getFreeHeap();
  
  // Stopper proprement le client TLS
  _client.stop();
  
  // v11.157: Délai augmenté + garbage collection forcé pour améliorer libération TLS
  // Augmenté de 200ms à 500ms pour laisser plus de temps à FreeRTOS
  vTaskDelay(pdMS_TO_TICKS(200));
  
  // Forcer garbage collection: allouer puis libérer un petit bloc pour encourager défragmentation
  void* tempBlock = heap_caps_malloc(1024, MALLOC_CAP_DEFAULT);
  if (tempBlock) {
    free(tempBlock);
    vTaskDelay(pdMS_TO_TICKS(100));  // Petit délai après free
  }
  
  // Délai supplémentaire pour laisser FreeRTOS nettoyer complètement
  vTaskDelay(pdMS_TO_TICKS(200));
  
  uint32_t heapAfter = ESP.getFreeHeap();
  int32_t recovered = (int32_t)heapAfter - (int32_t)heapBefore;
  
  if (recovered > 1000) {
    Serial.printf("[HTTP] 🔄 TLS stop: récupéré %d bytes (heap: %u → %u)\n", 
                  recovered, heapBefore, heapAfter);
  }
}

bool WebClient::httpRequest(const char* url, const char* payload,
                           char* response, size_t responseSize) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  if (url == nullptr || response == nullptr || responseSize == 0) {
    return false;
  }

  // Point 4: savoir quelle tâche exécute réellement les requêtes réseau
  logTaskAndStack("HTTP");

  // Guard mémoire avant requête HTTPS (correction crash monitoring)
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  // v11.157: Aligné avec TLS_MIN_HEAP_BYTES (62KB) pour cohérence
  const uint32_t MIN_HEAP_FOR_HTTPS = TLS_MIN_HEAP_BYTES;  // 62 KB minimum (TLS ~42KB + marge 20KB)
  bool isSecure = (strncmp(url, "https://", 8) == 0);
  
  if (isSecure) {
    // Vérification simple du heap avant TLS
    if (freeHeap < MIN_HEAP_FOR_HTTPS) {
      LOG(LOG_WARN, "[HTTP] ⚠️ Heap trop faible (%u bytes < %u bytes), report de la requête HTTPS", 
          freeHeap, MIN_HEAP_FOR_HTTPS);
      return false;
    }
    
    #if defined(PROFILE_TEST)
    // Diagnostics détaillés uniquement en mode test
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t fragmentation = (freeHeap > 0) ? ((freeHeap - largestBlock) * 100 / freeHeap) : 0;
    LOG(LOG_DEBUG, "[HTTP] 📊 Diagnostic mémoire avant TLS: heap=%u, largest_block=%u, fragmentation=%u%%", 
        freeHeap, largestBlock, fragmentation);
    
    // Vérifier que le plus grand bloc est suffisant pour TLS
    if (largestBlock < 45000) {
      LOG(LOG_WARN, "[HTTP] ⚠️ Plus grand bloc insuffisant (%u bytes < 45KB), fragmentation=%u%%", 
          largestBlock, fragmentation);
      LOG(LOG_WARN, "[HTTP] ⚠️ TLS nécessite ~42-46KB contiguë, report de la requête");
      return false;
    }
    #endif
  }
  
  size_t payloadLen = payload ? strlen(payload) : 0;
  uint32_t requestStartMs = millis();
  const bool debugLogging = (LOG_LEVEL >= LOG_DEBUG) && LogConfig::SERIAL_ENABLED;
  const bool verboseLogging = (LOG_LEVEL >= LOG_VERBOSE) && LogConfig::SERIAL_ENABLED;

  if (debugLogging) {
    LOG(LOG_DEBUG, "[HTTP] POST %s (payload=%u bytes, timeout=%u ms)",
        url, payloadLen, NetworkConfig::HTTP_TIMEOUT_MS);
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
    if (elapsedMs >= NetworkConfig::HTTP_TIMEOUT_MS) {
      LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u/%u ms", elapsedMs, NetworkConfig::HTTP_TIMEOUT_MS);
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
      // Utiliser un buffer fixe au lieu de String pour éviter fragmentation mémoire
      const size_t MAX_TEMP_RESPONSE = 2048; // Taille max pour réponse temporaire
      char tempResponseBuffer[MAX_TEMP_RESPONSE + 1];
      WiFiClient* stream = _http.getStreamPtr();
      size_t responseLen = 0;
      
      if (stream) {
        // Lire directement dans le buffer sans String
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
        // Fallback: getStream() retourne NetworkClient (pas un pointeur), utiliser getString() à la place
        // Note: getStream() n'est plus compatible avec la nouvelle API
        {
          // Dernier recours: getString() si aucun stream disponible
          // Note: HTTPClient::getString() retourne String Arduino (limitation API)
          // La String est copiée immédiatement et détruite pour minimiser fragmentation
          String tempResponse = _http.getString();
          responseLen = tempResponse.length();
          if (responseLen >= MAX_TEMP_RESPONSE) {
            responseLen = MAX_TEMP_RESPONSE - 1;
          }
          // Copier immédiatement pour libérer la String rapidement
          if (responseLen > 0) {
            strncpy(tempResponseBuffer, tempResponse.c_str(), responseLen);
            tempResponseBuffer[responseLen] = '\0';
          } else {
            tempResponseBuffer[0] = '\0';
          }
          // String tempResponse est détruite ici, libérant la mémoire
        }
      }
      
      if (responseLen >= responseSize) {
        responseLen = responseSize - 1;
      }
      strncpy(response, tempResponseBuffer, responseLen);
      response[responseLen] = '\0';
      unsigned long responseDurationMs = millis() - responseStartMs;
      
      if (debugLogging) {
        LOG(LOG_DEBUG, "[HTTP] Réponse reçue code=%d bytes=%u en %lu ms", code, responseLen, responseDurationMs);
      }

      if (verboseLogging) {
        char headerBuf[128];
        strncpy(headerBuf, _http.header("Content-Type").c_str(), sizeof(headerBuf) - 1);
        headerBuf[sizeof(headerBuf) - 1] = '\0';
        LOG(LOG_VERBOSE, "[HTTP] Content-Type=%s", headerBuf);
        strncpy(headerBuf, _http.header("Server").c_str(), sizeof(headerBuf) - 1);
        headerBuf[sizeof(headerBuf) - 1] = '\0';
        LOG(LOG_VERBOSE, "[HTTP] Server=%s", headerBuf);
        strncpy(headerBuf, _http.header("Connection").c_str(), sizeof(headerBuf) - 1);
        headerBuf[sizeof(headerBuf) - 1] = '\0';
        LOG(LOG_VERBOSE, "[HTTP] Connection=%s", headerBuf);
        strncpy(headerBuf, _http.header("Content-Length").c_str(), sizeof(headerBuf) - 1);
        headerBuf[sizeof(headerBuf) - 1] = '\0';
        LOG(LOG_VERBOSE, "[HTTP] Content-Length=%s", headerBuf);
        strncpy(headerBuf, _http.header("Transfer-Encoding").c_str(), sizeof(headerBuf) - 1);
        headerBuf[sizeof(headerBuf) - 1] = '\0';
        LOG(LOG_VERBOSE, "[HTTP] Transfer-Encoding=%s", headerBuf);

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
      char errorBuf2[64];
      strncpy(errorBuf2, _http.errorToString(code).c_str(), sizeof(errorBuf2) - 1);
      errorBuf2[sizeof(errorBuf2) - 1] = '\0';
      LOG(LOG_WARN, "[HTTP] Erreur %d (tentative %d/%d, %lu ms) : %s",
          code,
          attempt + 1,
          maxAttempts,
          errorTimeMs - requestStartMs,
          errorBuf2);
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
    resetTLSClient();  // Libère mémoire TLS après POST
    
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
  // VALIDATION COMPLÈTE DES MESURES AVANT ENVOI
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humid;

  // Validation des températures (rejette 0°C car physiquement impossible)
  if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
    tempWater = NAN;
  }

  if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
    tempAir = NAN;
  }

  // Validation de l'humidité
  if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
    humidity = NAN;
  }

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
    return false;
  }
  
  // Envoi direct: l'ordre exact est déjà respecté
  bool success = postRaw(payload);
  
  return success;
}

bool WebClient::fetchRemoteState(JsonDocument& doc) {
  if (!config.isRemoteRecvEnabled()) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!TLSMutex::acquire(3000)) {
    return false;
  }
  bool hasMutex = true;
  
  WiFi.setSleep(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  
  uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t MIN_HEAP_FOR_HTTPS = TLS_MIN_HEAP_BYTES;
  
  // Vérification simple du heap
  if (freeHeap < MIN_HEAP_FOR_HTTPS) {
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  char url[256];
  ServerConfig::getOutputUrl(url, sizeof(url));
  bool secure = strncmp(url, "https://", 8) == 0;
  WiFiClient plain;
  
  if (secure) { 
    _http.begin(_client, url); 
  } else { 
    _http.begin(plain, url); 
  }
  
  int code = _http.GET();
  
  if (code <= 0) {
    _http.end();
    resetTLSClient();
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // v11.156: Lecture limitée avec buffer fixe pour éviter fragmentation mémoire
  // Limite stricte basée sur BufferConfig::JSON_DOCUMENT_SIZE
  const size_t MAX_RESPONSE_SIZE = BufferConfig::JSON_DOCUMENT_SIZE;
  char payloadBuffer[MAX_RESPONSE_SIZE + 1];
  size_t totalRead = 0;
  
  WiFiClient* stream = _http.getStreamPtr();
  
  if (!stream) {
    _http.end();
    resetTLSClient();
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
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
  resetTLSClient();
  
  if (totalRead == 0) {
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  // Nettoyer le préfixe chunked HTTP si présent
  char* jsonStart = strchr(payloadBuffer, '{');
  if (!jsonStart) {
    jsonStart = strchr(payloadBuffer, '[');
  }
  if (jsonStart && jsonStart != payloadBuffer) {
    size_t offset = jsonStart - payloadBuffer;
    size_t newSize = totalRead - offset;
    memmove(payloadBuffer, jsonStart, newSize);
    payloadBuffer[newSize] = '\0';
    totalRead = newSize;
  }
  
  DeserializationError err = deserializeJson(doc, payloadBuffer);
  if (err) {
    if (hasMutex) TLSMutex::release();
    return false;
  }
  
  if (hasMutex) TLSMutex::release();
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
  
  char bufCrc2[16];
  snprintf(bufCrc2,sizeof(bufCrc2),"&crc=%08lX",crc32(payloadBuf));
  
  char pay2[272];
  snprintf(pay2, sizeof(pay2), "%s%s", payloadBuf, bufCrc2);
  
  char resp[1024];
  char heartbeatUrl[256];
  ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
  return httpRequest(heartbeatUrl, pay2, resp, sizeof(resp));
}

// v11.70: makeSkeleton() supprimé - jamais utilisé (includeSkeleton toujours false)

bool WebClient::postRaw(const char* payload){
  if (!config.isRemoteSendEnabled()) {
    return false;
  }
  
  if (payload == nullptr) {
    return false;
  }

  if (!TLSMutex::acquire(5000)) {
    return false;
  }
  
  WiFi.setSleep(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  
  size_t payloadLen = strlen(payload);
  size_t estimatedSize = payloadLen + strlen(_apiKey) + strlen(ProjectConfig::BOARD_TYPE) + 32;
  char* fullBuffer = (char*)malloc(estimatedSize);
  if (!fullBuffer) {
    TLSMutex::release();
    return false;
  }

  bool hasApi = (strncmp(payload, "api_key=", 8) == 0);
  
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
  } else {
    strncpy(fullBuffer, payload, estimatedSize - 1);
    fullBuffer[estimatedSize - 1] = '\0';
  }

  char respPrimary[1024];
  char postDataUrl[256];
  ServerConfig::getPostDataUrl(postDataUrl, sizeof(postDataUrl));
  bool okPrimary = httpRequest(postDataUrl, fullBuffer, respPrimary, sizeof(respPrimary));
  
  free(fullBuffer);
  
  TLSMutex::release();
  return okPrimary;
}

// Fonction supprimée - redéfinition causait erreur de compilation 