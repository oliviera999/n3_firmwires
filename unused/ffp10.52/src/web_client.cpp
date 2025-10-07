#include "web_client.h"
#include "diagnostics.h"
#include "log.h"
#include "project_config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

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
  // Désactiver le modem sleep juste avant un transfert pour fiabilité
  WiFi.setSleep(false);

  // Logs plus sobres en production
  #if defined(PROFILE_PROD)
    Serial.printf("[HTTP] → %s (%u bytes)\n", url.c_str(), payload.length());
  #else
    Serial.printf("[HTTP] → %s (%u bytes)\n", url.c_str(), payload.length());
    if (payload.length() <= 300) {
      Serial.printf("[HTTP] payload: %s\n", payload.c_str());
    } else {
      Serial.printf("[HTTP] payload: %s ... (truncated)\n", payload.substring(0,300).c_str());
    }
  #endif

  // Choix du client selon le schéma (HTTP = non-TLS / HTTPS = TLS)
  bool secure = url.startsWith("https://");
  WiFiClient plain; // client non-TLS (portée limitée à la fonction)

  // Politique de retry exponentiel simple
  const int maxAttempts = 3;
  int attempt = 0;
  int code = -1;
  response = "";
  while (attempt < maxAttempts) {
    // Ré-initialise la requête
    if (secure) {
      _http.begin(_client, url);
    } else {
      _http.begin(plain, url);
    }
    _http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = _http.POST(payload);
    if (code > 0) {
      response = _http.getString();
      #if !defined(PROFILE_PROD)
      Serial.printf("[HTTP] ← code %d, %u bytes\n", code, response.length());
      if (response.length() < 300) {
        Serial.printf("[HTTP] response: %s\n", response.c_str());
      } else {
        Serial.printf("[HTTP] response: %s ... (truncated)\n", response.substring(0,300).c_str());
      }
      #endif
    } else {
      Serial.printf("[HTTP] error %d (attempt %d/%d)\n", code, attempt+1, maxAttempts);
    }
    _http.end();
    // Record into diagnostics if available
    extern Diagnostics diag;
    if (code > 0) {
      diag.recordHttpResult(code >= 200 && code < 400, code);
    } else {
      diag.recordHttpResult(false, code);
    }

    if (code >= 200 && code < 400) break;
    // Court-circuit si plus de WiFi
    if (WiFi.status() != WL_CONNECTED) break;
    // Backoff exponentiel: 200ms, 600ms, 1400ms...
    int backoff = NetworkConfig::BACKOFF_BASE_MS * (attempt + 1) * (attempt + 1);
    vTaskDelay(pdMS_TO_TICKS(backoff));
    attempt++;
  }

  // Ne pas réactiver le modem-sleep
  WiFi.setSleep(false);
  return (code >= 200 && code < 400);
}

bool WebClient::sendMeasurements(const Measurements& m, bool includeReset) {
  // VALIDATION COMPLÈTE DES MESURES AVANT ENVOI
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humid;

  // Validation des températures (rejette 0°C car physiquement impossible)
  if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
    Serial.printf("[WebClient] Température eau invalide avant envoi: %.1f°C, force NaN\n", tempWater);
    tempWater = NAN;
  }

  if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
    Serial.printf("[WebClient] Température air invalide avant envoi: %.1f°C, force NaN\n", tempAir);
    tempAir = NAN;
  }

  // Validation de l'humidité
  if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
    Serial.printf("[WebClient] Humidité invalide avant envoi: %.1f%%, force NaN\n", humidity);
    humidity = NAN;
  }

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

  LOG(LOG_DEBUG, "POST %s", payload.c_str());
  // Envoi sans squelette: l’ordre exact est déjà respecté
  return postRaw(payload, false);
}

bool WebClient::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  // Utiliser l'URL complète depuis la configuration serveur
  String url = ServerConfig::getOutputUrl();
  // Sélectionne le bon type de client selon le schéma
  bool secure = url.startsWith("https://");
  WiFiClient plain; // client non-TLS local
  if (secure) { _http.begin(_client, url); } else { _http.begin(plain, url); }
  Serial.println("[HTTP] → GET remote state");
  int code = _http.GET();
  Serial.printf("[Web] GET remote state -> HTTP %d\n", code);
  if (code <= 0) {
    _http.end();
    return false;
  }
  size_t size = _http.getSize();
  Serial.printf("[HTTP] ← %u bytes\n", (unsigned)size);
  WiFiClient* stream = _http.getStreamPtr();
  DeserializationError err = deserializeJson(doc, *stream);
  _http.end();
  if (err) {
    Serial.printf("[Web] JSON parse error: %s\n", err.c_str());
    return false;
  }
  Serial.println("[Web] remote JSON ok");
  return !err;
}

bool WebClient::sendHeartbeat(const Diagnostics& diag) {
  String payload;
  payload.reserve(128);
  const DiagnosticStats& s = diag.getStats();
  payload = String("uptime=") + s.uptimeSec + "&free=" + s.freeHeap + "&min=" + s.minFreeHeap + "&reboots=" + s.rebootCount;
  char bufCrc2[15];
  snprintf(bufCrc2,sizeof(bufCrc2),"&crc=%08lX",crc32(payload.c_str()));
  String pay2 = payload + bufCrc2;
  String resp;
  return httpRequest(ServerConfig::getHeartbeatUrl(), pay2, resp);
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
  String full = payload;

  // Ajoute api_key et sensor si absents
  bool hasApi = payload.startsWith("api_key=");
  if (!hasApi) {
    if (includeSkeleton) {
      String skeleton = makeSkeleton(payload);
      full = String("api_key=") + _apiKey + "&sensor=" + Config::SENSOR + skeleton;
    } else {
      full = String("api_key=") + _apiKey + "&sensor=" + Config::SENSOR;
    }
    if (payload.length()) {
      full += "&";
      full += payload;
    }
  }

  String respPrimary;
  bool okPrimary = httpRequest(ServerConfig::getPostDataUrl(), full, respPrimary);
  // Tentative d'envoi vers le serveur secondaire si configuré
  String secondary = ServerConfig::getSecondaryPostDataUrl();
  bool okSecondary = false; // considéré faux si non configuré
  if (secondary.length() > 0) {
    String respSecondary;
    okSecondary = httpRequest(secondary, full, respSecondary);
    if (!okSecondary) {
      Serial.println("[HTTP] Alerte: échec POST sur serveur secondaire");
    }
  }
  // Réussite si au moins un des serveurs a reçu l'update
  return okPrimary || okSecondary;
}

// Surcharge par compatibilité : comportement historique (avec squelette)
bool WebClient::postRaw(const String& payload){
  return postRaw(payload, true);
} 