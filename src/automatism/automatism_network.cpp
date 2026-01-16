#include "automatism/automatism_network.h"
#include "nvs_manager.h" // v11.115
#include "automatism.h"
#include "automatism_persistence.h"
#include "config.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "pins.h"
#include <WiFi.h>
#include <cstring>
#include <cctype>

namespace {
constexpr size_t FEED_MESSAGE_BUFFER_SIZE = 256;
}

// ============================================================================
// Module: AutomatismNetwork
// Responsabilité: Communication avec serveur distant
// ============================================================================

AutomatismNetwork::AutomatismNetwork(WebClient& web, ConfigManager& cfg)
    : _web(web)
    , _config(cfg)
    , _dataQueue(QUEUE_MAX_ENTRIES)
    , _emailEnabled(false)
    #if defined(PROFILE_TEST)
    , _freqWakeSec(6)  // 6s par défaut pour wroom-test
    #else
    , _freqWakeSec(600)  // 10 min par défaut
    #endif
    , _limFlood(5)
    , _aqThresholdCm(30)
    , _tankThresholdCm(30)
    , _heaterThresholdC(24.0f)
    , _lastSend(0)
    , _lastRemoteFetch(0)
    , _serverOk(false)
    , _sendState(0)
    , _recvState(0)
    , _prevPumpTank(false)
    , _prevPumpAqua(false)
    , _prevBouffeMatin(false)
    , _prevBouffeMidi(false)
    , _prevBouffeSoir(false)
{
    _emailAddress[0] = '\0';
    Serial.println(F("[Network] Module initialisé (constructeur)"));
    
    // NOTE (v11.32): DataQueue::begin() NE PEUT PAS être appelé ici
    // car LittleFS n'est pas encore monté (constructeur global)
    // L'initialisation sera faite dans begin() après LittleFS.begin()
}

void AutomatismNetwork::setEmailAddress(const char* address) {
    if (!address) {
        _emailAddress[0] = '\0';
        return;
    }

    while (*address && isspace(static_cast<unsigned char>(*address))) {
        ++address;
    }

    size_t len = strnlen(address, EmailConfig::MAX_EMAIL_LENGTH - 1);
    const char* end = address + len;
    while (end > address && isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
        --len;
    }

    memcpy(_emailAddress, address, len);
    _emailAddress[len] = '\0';
}

bool AutomatismNetwork::begin() {
    // Initialiser la queue de données (après montage LittleFS)
    if (_dataQueue.begin()) {
        Serial.printf("[Network] ✓ DataQueue initialisée (%u entrées en attente)\n", _dataQueue.size());
        return true;
    } else {
        Serial.println(F("[Network] ⚠️ Échec initialisation DataQueue"));
        return false;
    }
}

// v11.70: normalizeServerKeys() supprimé - standardisation des clés JSON
// Le serveur et l'ESP32 utilisent maintenant les mêmes noms de clés

// ============================================================================
// COMMUNICATION SERVEUR - Méthodes Simples
// ============================================================================

bool AutomatismNetwork::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    bool ok = _web.fetchRemoteState(doc);
    if (ok && doc.size() > 0) {
        // v11.70: Pas de normalisation - clés déjà standardisées
        String jsonStr;
        serializeJson(doc, jsonStr);
        _config.saveRemoteVars(jsonStr);
        _serverOk = true;
        _recvState = 1;  // OK
        Serial.println(F("[Network] État distant récupéré avec succès (clés standardisées)"));
    } else {
        _serverOk = false;
        _recvState = -1;  // Erreur
        Serial.println(F("[Network] Échec récupération état distant"));
    }
    _lastRemoteFetch = millis();
    return ok;
}

void AutomatismNetwork::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
  auto parseIntValue = [](ArduinoJson::JsonVariantConst value) -> int {
    if (value.is<int>()) return value.as<int>();
    if (value.is<float>()) return static_cast<int>(value.as<float>());
    if (value.is<const char*>()) return atoi(value.as<const char*>());
    return value.as<int>();
  };

  auto parseFloatValue = [](ArduinoJson::JsonVariantConst value) -> float {
    if (value.is<float>()) return value.as<float>();
    if (value.is<int>()) return static_cast<float>(value.as<int>());
    if (value.is<const char*>()) return atof(value.as<const char*>());
    return value.as<float>();
  };

  // 100: Email
  if (doc.containsKey("mail") || doc.containsKey("100")) {
    const char* emailPtr = doc.containsKey("mail") ? doc["mail"].as<const char*>() : doc["100"].as<const char*>();
    setEmailAddress(emailPtr);
  }

  // 101: Notifications Email
  if (doc.containsKey("mailNotif") || doc.containsKey("101")) {
    auto v = doc.containsKey("mailNotif") ? doc["mailNotif"] : doc["101"];
    bool enabled = _emailEnabled;
    if (v.is<bool>()) {
      enabled = v.as<bool>();
    } else if (v.is<int>()) {
      enabled = (v.as<int>() == 1);
    } else if (v.is<const char*>()) {
        // Utiliser buffer statique au lieu de String pour éviter fragmentation
        const char* str = v.as<const char*>();
        char tempStr[32];
        size_t len = strlen(str);
        if (len >= sizeof(tempStr)) len = sizeof(tempStr) - 1;
        strncpy(tempStr, str, len);
        tempStr[len] = '\0';
        
        // Convertir en minuscules et comparer
        for (size_t i = 0; i < len; i++) {
          if (tempStr[i] >= 'A' && tempStr[i] <= 'Z') {
            tempStr[i] = tempStr[i] - 'A' + 'a';
          }
        }
        
        // Comparer avec les valeurs acceptées
        enabled = (strcmp(tempStr, "1") == 0 || 
                   strcmp(tempStr, "true") == 0 || 
                   strcmp(tempStr, "on") == 0 || 
                   strcmp(tempStr, "yes") == 0);
    }
    setEmailEnabled(enabled);
  }

  // 116: Fréquence réveil
  if (doc.containsKey("FreqWakeUp") || doc.containsKey("116")) {
    auto v = doc.containsKey("FreqWakeUp") ? doc["FreqWakeUp"] : doc["116"];
    int value = parseIntValue(v);
    if (value >= 0) {
      setFreqWakeSec(static_cast<uint16_t>(value));
    }
  }

  // 114: Limite inondation
  if (doc.containsKey("limFlood") || doc.containsKey("114")) {
    auto v = doc.containsKey("limFlood") ? doc["limFlood"] : doc["114"];
    int value = parseIntValue(v);
    if (value >= 0) {
      setLimFlood(static_cast<uint16_t>(value));
    }
  }

  // 102: Seuil Aquarium
  if (doc.containsKey("aqThreshold") || doc.containsKey("102")) {
    auto v = doc.containsKey("aqThreshold") ? doc["aqThreshold"] : doc["102"];
    int value = parseIntValue(v);
    if (value > 0) {
      setAqThresholdCm(static_cast<uint16_t>(value));
      // Note: Synchronisation avec Automatism sera faite dans parseRemoteConfig
    }
  }

  // 103: Seuil Réservoir
  if (doc.containsKey("tankThreshold") || doc.containsKey("103")) {
    auto v = doc.containsKey("tankThreshold") ? doc["tankThreshold"] : doc["103"];
    int value = parseIntValue(v);
    if (value > 0) {
      setTankThresholdCm(static_cast<uint16_t>(value));
      // Note: Synchronisation avec Automatism sera faite dans parseRemoteConfig
    }
  }

  // 104: Seuil Chauffage
  if (doc.containsKey("chauffageThreshold") || doc.containsKey("104")) {
    auto v = doc.containsKey("chauffageThreshold") ? doc["chauffageThreshold"] : doc["104"];
    float value = parseFloatValue(v);
    if (value > 0.0f && !isnan(value)) {
      setHeaterThresholdC(value);
      // Note: Synchronisation avec Automatism sera faite dans parseRemoteConfig
    }
  }

  // 113: Durée remplissage (sera appliquée dans parseRemoteConfig qui a accès à Automatism)
  if (doc.containsKey("tempsRemplissageSec") || doc.containsKey("refillDuration") || doc.containsKey("113")) {
    // Log seulement ici, l'application sera faite dans parseRemoteConfig
    Serial.println(F("[Network] tempsRemplissageSec détecté (sera appliqué via parseRemoteConfig)"));
  }

  Serial.println(F("[Network] Configuration appliquée depuis JSON (support clés mixtes)"));
}

// ============================================================================
// SEND FULL UPDATE
// NOTE: Cette méthode nécessite BEAUCOUP de variables d'Automatism
// Pour l'instant, elle reste dans automatism.cpp et sera déléguée plus tard
// quand on aura refactorisé les variables membres
// ============================================================================

bool AutomatismNetwork::sendFullUpdate(
    const SensorReadings& readings,
    SystemActuators& acts,
    int diffMaree,
    uint8_t bouffeMatin, uint8_t bouffeMidi, uint8_t bouffeSoir,
    uint16_t tempsGros, uint16_t tempsPetits,
    const String& bouffePetits, const String& bouffeGros,
    bool forceWakeUp, uint16_t freqWakeSec,
    uint32_t refillDurationSec,
    const char* extraPairs
) {
    uint32_t attemptStartMs = millis();
    if (!canAttemptSend(attemptStartMs)) {
        uint32_t elapsed = attemptStartMs - _lastSendAttemptMs;
        uint32_t waitRemaining = (_currentBackoffMs > elapsed) ? (_currentBackoffMs - elapsed) : 0;
        if (waitRemaining > 0 && (attemptStartMs - _lastBackoffLogMs) > 3000) {
            _lastBackoffLogMs = attemptStartMs;
            Serial.printf("[Network] ⏳ Backoff actif (%u ms restants, failures=%u)\n", waitRemaining, _consecutiveSendFailures);
        }
        return false;
    }
    _lastSendAttemptMs = attemptStartMs;

    float tempWater = readings.tempWater;
    float tempAir = readings.tempAir;
    float humidity = readings.humidity;

    if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
        Serial.printf("[Network] Temp eau invalide: %.1f°C, force NaN\n", tempWater);
        tempWater = NAN;
    }

    if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN ||
        tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
        Serial.printf("[Network] Temp air invalide: %.1f°C, force NaN\n", tempAir);
        tempAir = NAN;
    }

    if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN ||
        humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
        Serial.printf("[Network] Humidité invalide: %.1f%%, force NaN\n", humidity);
        humidity = NAN;
    }

    uint32_t heapBefore = ESP.getFreeHeap();
    if (heapBefore < BufferConfig::LOW_MEMORY_THRESHOLD_BYTES) {
        Serial.printf("[Network] Mémoire faible (%u bytes), report envoi\n", heapBefore);
        registerSendResult(false, 0, 0, heapBefore, heapBefore);
        return false;
    }

    char payloadBuffer[1024];
    int len = snprintf(payloadBuffer, sizeof(payloadBuffer),
        "api_key=%s&sensor=%s&version=%s&TempAir=%.1f&Humidite=%.1f&TempEau=%.1f&"
        "EauPotager=%u&EauAquarium=%u&EauReserve=%u&diffMaree=%d&Luminosite=%u&"
        "etatPompeAqua=%d&etatPompeTank=%d&etatHeat=%d&etatUV=%d&"
        "bouffeMatin=%u&bouffeMidi=%u&bouffeSoir=%u&tempsGros=%u&tempsPetits=%u&"
        "aqThreshold=%u&tankThreshold=%u&chauffageThreshold=%.1f&"
        "tempsRemplissageSec=%u&limFlood=%u&WakeUp=%d&FreqWakeUp=%u&"
        "bouffePetits=%s&bouffeGros=%s&mail=%s&mailNotif=%s",
        ApiConfig::API_KEY, ProjectConfig::BOARD_TYPE, ProjectConfig::VERSION,
        tempAir, humidity, tempWater,
        readings.wlPota, readings.wlAqua, readings.wlTank, diffMaree, readings.luminosite,
        acts.isAquaPumpRunning(), acts.isTankPumpRunning(), acts.isHeaterOn(), acts.isLightOn(),
        bouffeMatin, bouffeMidi, bouffeSoir, tempsGros, tempsPetits,
        _aqThresholdCm, _tankThresholdCm, _heaterThresholdC,
        refillDurationSec, _limFlood,
        forceWakeUp ? 1 : 0, freqWakeSec,
        bouffePetits.c_str(), bouffeGros.c_str(),
        _emailAddress, _emailEnabled ? "checked" : "");

    if (len < 0) {
        Serial.println(F("[Network] ❌ Échec formatage payload"));
        registerSendResult(false, 0, 0, heapBefore, heapBefore);
        return false;
    }

    if (len >= static_cast<int>(sizeof(payloadBuffer))) {
        Serial.printf("[Network] ⚠️ Payload tronqué (limite: %u bytes)\n", sizeof(payloadBuffer) - 1);
        payloadBuffer[sizeof(payloadBuffer) - 1] = '\0';
    }

    if (extraPairs && extraPairs[0] != '\0') {
        strncat(payloadBuffer, "&", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
        strncat(payloadBuffer, extraPairs, sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
    }

    if (strstr(payloadBuffer, "resetMode=") == nullptr) {
        strncat(payloadBuffer, "&resetMode=0", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
        Serial.println(F("[Network] 🔒 resetMode=0 ajouté automatiquement pour sécurité"));
    } else if (strstr(payloadBuffer, "resetMode=1") != nullptr) {
        Serial.println(F("[Network] ⚠️ resetMode=1 détecté dans payload"));
    } else {
        Serial.println(F("[Network] ✅ resetMode=0 confirmé dans payload"));
    }

    size_t payloadBytes = strlen(payloadBuffer);
    if (payloadBytes > MAX_PAYLOAD_BYTES) {
        Serial.printf("[Network] ❌ Payload trop volumineux (%u > %u)\n", static_cast<unsigned>(payloadBytes), static_cast<unsigned>(MAX_PAYLOAD_BYTES));
        registerSendResult(false, payloadBytes, 0, heapBefore, heapBefore);
        if (_dataQueue.size() >= QUEUE_MAX_ENTRIES) {
            _dataQueue.pop();
            logQueueState("drop-oldest", _dataQueue.size());
        }
        if (_dataQueue.push(String(payloadBuffer))) {
            if (_dataQueue.size() > _queueHighWaterMark) {
                _queueHighWaterMark = _dataQueue.size();
            }
            logQueueState("oversize-enqueued", _dataQueue.size());
        }
        return false;
    }

    if (!_config.isRemoteSendEnabled()) {
        Serial.println(F("[Network] ⛔ Envoi distant désactivé (config) - SKIP"));
        registerSendResult(false, payloadBytes, 0, heapBefore, heapBefore);
        return false;
    }

    esp_task_wdt_reset();
    uint32_t sendStart = millis();
    bool success = _web.postRaw(payloadBuffer);
    uint32_t durationMs = millis() - sendStart;
    uint32_t heapAfter = ESP.getFreeHeap();

    registerSendResult(success, payloadBytes, durationMs, heapBefore, heapAfter);

    if (success) {
        _sendState = 1;
        Serial.println(F("[Network] sendFullUpdate SUCCESS"));
        if (_dataQueue.size() > 0) {
            uint16_t replayed = replayQueuedData();
            if (replayed > 0) {
                Serial.printf("[Network] ✓ %u données rejouées avec succès\n", replayed);
            } else {
                Serial.println(F("[Network] ⚠️ Échec rejeu queue - données conservées"));
            }
        }
    } else {
        _sendState = -1;
        Serial.println(F("[Network] sendFullUpdate FAILED"));
        uint16_t queueSize = _dataQueue.size();
        if (queueSize >= QUEUE_MAX_ENTRIES) {
            _dataQueue.pop();
            queueSize = _dataQueue.size();
            logQueueState("drop-oldest", queueSize);
        }
        if (_dataQueue.push(String(payloadBuffer))) {
            queueSize = _dataQueue.size();
            if (queueSize > _queueHighWaterMark) {
                _queueHighWaterMark = queueSize;
            }
            if (queueSize >= QUEUE_HIGH_WATER) {
                logQueueState("high-water", queueSize);
            } else {
                Serial.printf("[Network] 📥 Donnée ajoutée à la queue (%u entrées)\n", queueSize);
            }
        } else {
            Serial.println(F("[Network] ⚠️ Queue pleine - donnée perdue (LittleFS)"));
        }
    }

    _lastSend = millis();
    return success;
}

bool AutomatismNetwork::canAttemptSend(uint32_t nowMs) const {
    if (_consecutiveSendFailures == 0 || _currentBackoffMs == 0) {
        return true;
    }
    uint32_t elapsed = static_cast<uint32_t>(nowMs - _lastSendAttemptMs);
    return elapsed >= _currentBackoffMs;
}

void AutomatismNetwork::registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter) {
    _totalSendAttempts++;
    _totalSendDurationMs += durationMs;
    _lastSendDurationMs = durationMs;
    _lastHeapBeforeSend = heapBefore;
    _lastHeapAfterSend = heapAfter;
    if (payloadBytes > _maxPayloadBytesSeen) {
        _maxPayloadBytesSeen = payloadBytes;
    }

    int32_t heapDelta = static_cast<int32_t>(heapAfter) - static_cast<int32_t>(heapBefore);

    if (success) {
        _totalSendSuccesses++;
        _totalPayloadBytesSent += payloadBytes;
        _consecutiveSendFailures = 0;
        _currentBackoffMs = 0;
        _lastBackoffLogMs = 0;
        if (durationMs > 500 || payloadBytes > (MAX_PAYLOAD_BYTES - 32)) {
            Serial.printf("[Network] ✅ Telemetry: payload=%uB dur=%ums heapΔ=%d queue=%u\n",
                          static_cast<unsigned>(payloadBytes), static_cast<unsigned>(durationMs),
                          heapDelta, _dataQueue.size());
        }
    } else {
        _totalSendFailures++;
        if (_consecutiveSendFailures < 32) {
            _consecutiveSendFailures++;
        }
        uint32_t backoff = BASE_BACKOFF_MS << (_consecutiveSendFailures > 0 ? (_consecutiveSendFailures - 1) : 0);
        if (backoff > MAX_BACKOFF_MS) {
            backoff = MAX_BACKOFF_MS;
        }
        _currentBackoffMs = backoff;
        Serial.printf("[Network] ❌ Telemetry: payload=%uB dur=%ums heapΔ=%d failures=%u backoff=%ums\n",
                      static_cast<unsigned>(payloadBytes), static_cast<unsigned>(durationMs),
                      heapDelta, _consecutiveSendFailures, _currentBackoffMs);
    }
}

void AutomatismNetwork::logQueueState(const char* reason, uint16_t size) const {
    Serial.printf("[Network] Queue %s (%u entrées, pic=%u, backoff=%ums)\n",
                  reason, size, _queueHighWaterMark, _currentBackoffMs);
}

// ============================================================================
// HELPERS PRIVÉS (isTrue, isFalse)
// ============================================================================

bool AutomatismNetwork::isTrue(ArduinoJson::JsonVariantConst v) {
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 1;
    if (v.is<const char*>()) {
        const char* p = v.as<const char*>();
        if (!p) return false;
        char buffer[16];
        size_t len = strnlen(p, sizeof(buffer) - 1);
        memcpy(buffer, p, len);
        buffer[len] = '\0';
        char* start = buffer;
        while (*start && isspace(static_cast<unsigned char>(*start))) ++start;
        char* end = start + strlen(start);
        while (end > start && isspace(static_cast<unsigned char>(end[-1]))) --end;
        *end = '\0';
        for (char* c = start; *c; ++c) {
            *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        }
        return strcmp(start, "1") == 0 || strcmp(start, "true") == 0 ||
               strcmp(start, "on") == 0 || strcmp(start, "checked") == 0;
    }
    return false;
}

bool AutomatismNetwork::isFalse(ArduinoJson::JsonVariantConst v) {
    if (v.is<bool>()) return !v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 0;
    if (v.is<const char*>()) {
        const char* p = v.as<const char*>();
        if (!p) return false;
        char buffer[16];
        size_t len = strnlen(p, sizeof(buffer) - 1);
        memcpy(buffer, p, len);
        buffer[len] = '\0';
        char* start = buffer;
        while (*start && isspace(static_cast<unsigned char>(*start))) ++start;
        char* end = start + strlen(start);
        while (end > start && isspace(static_cast<unsigned char>(end[-1]))) --end;
        *end = '\0';
        for (char* c = start; *c; ++c) {
            *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        }
        return strcmp(start, "0") == 0 || strcmp(start, "false") == 0 ||
               strcmp(start, "off") == 0 || strcmp(start, "unchecked") == 0;
    }
    return false;
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 1: pollRemoteState()
// Polling serveur + cache + état UI
// ============================================================================

bool AutomatismNetwork::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis, Automatism& autoCtrl) {
    // Flag de réception distante
    if (!_config.isRemoteRecvEnabled()) {
        Serial.println(F("[Network] ⛔ Réception distante désactivée (config)"));
        return false;
    }
    // Vérification intervalle
    if ((uint32_t)(currentMillis - _lastRemoteFetch) < REMOTE_FETCH_INTERVAL_MS) {
        return false;
    }
    _lastRemoteFetch = currentMillis;
    
    // Vérification WiFi
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    // Indique téléchargement en cours (flèche vide)
    _recvState = 0;
    
    // Mise à jour affichage OLED avec état marée
    // NOTE: Nécessite accès à _disp, _sensors, mailBlinkUntil depuis autoCtrl
    // Simplifié ici - l'affichage reste géré par automatism.cpp
    
    // Tentative récupération depuis serveur
    bool okRecv = _web.fetchRemoteState(doc);
    bool remoteSuccess = okRecv;
    
    // v11.72: Fallback - aplatir éventuel wrapper { "outputs": { ... } }
    if (okRecv && doc.containsKey("outputs") && doc["outputs"].is<JsonObject>()) {
        Serial.println(F("[Network] Wrapper 'outputs' détecté - aplatissement"));
        ArduinoJson::DynamicJsonDocument tmp(BufferConfig::JSON_DOCUMENT_SIZE);
        tmp.set(doc["outputs"].as<ArduinoJson::JsonObjectConst>());
        doc.clear();
        for (auto kv : tmp.as<ArduinoJson::JsonObjectConst>()) {
            doc[kv.key().c_str()] = kv.value();
        }
    }
    
    // === LOGS DÉTAILLÉS v11.07 - DIAGNOSTIC JSON ===
    if (okRecv && doc.size() > 0) {
        Serial.println(F("[Network] === JSON REÇU DU SERVEUR ==="));
        String jsonDebug;
        serializeJsonPretty(doc, jsonDebug);
        // Afficher premiers 1000 caractères pour éviter overflow
        Serial.println(jsonDebug.substring(0, min((int)jsonDebug.length(), 1000)));
        if (jsonDebug.length() > 1000) {
            Serial.printf("[Network] ... (tronqué, %u bytes restants)\n", jsonDebug.length() - 1000);
        }
        Serial.println(F("[Network] === FIN JSON ==="));
    } else if (!okRecv) {
        Serial.println(F("[Network] ⚠️ Échec fetchRemoteState() - aucune donnée reçue"));
    }
    
    if (!okRecv) {
        // Fallback: tentative rechargement depuis la flash
        String cachedJson;
        if (_config.loadRemoteVars(cachedJson)) {
            DeserializationError err = deserializeJson(doc, cachedJson);
            if (!err) {
                okRecv = true;
                Serial.println(F("[Network] Variables distantes chargées depuis sauvegarde (offline)"));
            }
        }
    }
    
    // Mise à jour états
    _recvState = (remoteSuccess && doc.size() > 0) ? 1 : -1;
    _serverOk = remoteSuccess;
    
    if (!okRecv) {
        return false;
    }
    
    // v11.74: Sauvegarde + diagnostics clés critiques
    String jsonToSave;
    serializeJson(doc, jsonToSave);
    _config.saveRemoteVars(jsonToSave);
    // Diagnostics: clés critiques présentes ?
    const char* crit[] = {"16","18","2","15","108","109","110","115"};
    for (size_t i=0;i<sizeof(crit)/sizeof(crit[0]);++i){
        const char* k = crit[i];
        bool has = doc.containsKey(k);
        Serial.printf("[Network] Key %s: %s\n", k, has?"OK":"MISSING");
    }
    
    return true;
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 2: handleResetCommand()
// Gestion commandes reset distant
// ============================================================================

bool AutomatismNetwork::handleResetCommand(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    bool needReset = false;
    const char* resetKey = nullptr;
    
    // Détection clés reset
    if (doc.containsKey("reset") && doc["reset"].as<int>() == 1) {
        needReset = true;
        resetKey = "reset";
    }
    if (doc.containsKey("resetMode") && doc["resetMode"].as<int>() == 1) {
        needReset = true;
        resetKey = "resetMode"; // Priorité à resetMode si présent
    }
    
    if (!needReset) {
        return false;
    }
    
    Serial.println(F("[Network] Reset distant demandé"));
    
    // === PROTECTION ANTI-BOUCLE INFINIE ===
    // Vérifier si un reset a déjà été effectué récemment
    unsigned long lastResetTime;
    g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, "reset_lastReset", lastResetTime, 0);
    uint32_t currentTime = millis();
    
    // Si reset dans les 30 dernières secondes, ignorer pour éviter la boucle
    if (lastResetTime > 0 && (currentTime - lastResetTime) < 30000) {
        Serial.printf("[Network] ⚠️ Reset ignoré - déjà effectué il y a %lu ms\n", 
                      currentTime - lastResetTime);
        return false;
    }
    
    // Marquer le reset en cours
    g_nvsManager.saveULong(NVS_NAMESPACES::SYSTEM, "reset_lastReset", currentTime);
    
    // Email notification si activé
    if (_emailEnabled) {
        // NOTE: Nécessite accès au Mailer
        // Pour l'instant, déléguer via autoCtrl
        autoCtrl.armMailBlink();
    }
    
    // === ACQUITTEMENT ROBUSTE AVEC RETRY ET CONFIRMATION ===
    bool ackSuccess = false;
    if (resetKey) {
        char override[64];
        snprintf(override, sizeof(override), "%s=0", resetKey);
        SensorReadings curR = autoCtrl.readSensors();
        
        // Tentative d'acquittement avec retry (5 tentatives pour plus de robustesse)
        for (int attempt = 1; attempt <= 5; attempt++) {
            Serial.printf("[Network] Tentative acquittement %d/5...\n", attempt);
            ackSuccess = autoCtrl.sendFullUpdate(curR, override);
            if (ackSuccess) {
                Serial.println(F("[Network] ✅ Acquittement réussi"));
                
                // v11.70: Attendre confirmation serveur avant restart
                Serial.println(F("[Network] ⏳ Attente confirmation serveur (5 secondes)..."));
                vTaskDelay(pdMS_TO_TICKS(5000));
                
                // Vérifier si resetMode est toujours à 1 côté serveur (boucle infinie)
                ArduinoJson::DynamicJsonDocument confirmDoc(BufferConfig::JSON_DOCUMENT_SIZE);
                bool confirmOk = _web.fetchRemoteState(confirmDoc);
                if (confirmOk && confirmDoc.containsKey("110")) {
                    int resetValue = confirmDoc["110"].as<int>();
                    if (resetValue == 1) {
                        Serial.println(F("[Network] ⚠️ ResetMode toujours à 1 côté serveur - risque boucle infinie"));
                        Serial.println(F("[Network] 🔄 Redémarrage forcé malgré le risque"));
                    } else {
                        Serial.println(F("[Network] ✅ Confirmation: resetMode=0 reçu côté serveur"));
                    }
                } else {
                    Serial.println(F("[Network] ⚠️ Impossible de confirmer reset côté serveur - redémarrage forcé"));
                }
                break;
            }
            if (attempt < 5) {
                Serial.printf("[Network] ⚠️ Échec tentative %d, retry dans 1s\n", attempt);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        
        if (!ackSuccess) {
            Serial.println(F("[Network] ❌ Échec acquittement après 5 tentatives"));
            Serial.println(F("[Network] ⚠️ Redémarrage sans confirmation (risque de boucle infinie)"));
        }
    }
    
    // Sauvegarde paramètres critiques NVS
    // NOTE: Ces méthodes sont dans Automatism, on les appelle via autoCtrl
    // Pour simplifier, on laisse Automatism gérer ça
    
    Serial.println(F("[Network] 🔄 Redémarrage ESP32..."));
    
    // Reset ESP
    ESP.restart();
    
    return true; // Jamais atteint mais pour cohérence
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 3: parseRemoteConfig()
// Parse et applique configuration distante
// ============================================================================

void AutomatismNetwork::parseRemoteConfig(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
  auto parseIntValue = [](ArduinoJson::JsonVariantConst value) -> int {
    if (value.is<int>()) return value.as<int>();
    if (value.is<float>()) return static_cast<int>(value.as<float>());
    if (value.is<const char*>()) return atoi(value.as<const char*>());
    return value.as<int>();
  };

  auto parseFloatValue = [](ArduinoJson::JsonVariantConst value) -> float {
    if (value.is<float>()) return value.as<float>();
    if (value.is<int>()) return static_cast<float>(value.as<int>());
    if (value.is<const char*>()) return atof(value.as<const char*>());
    return value.as<float>();
  };

  // 102: Seuil Aquarium
  if (doc.containsKey("aqThreshold") || doc.containsKey("102")) {
    auto v = doc.containsKey("aqThreshold") ? doc["aqThreshold"] : doc["102"];
    int value = parseIntValue(v);
    if (value > 0) {
      setAqThresholdCm(static_cast<uint16_t>(value));
      autoCtrl.setAqThresholdCm(static_cast<uint16_t>(value));
      Serial.printf("[Network] ✅ Seuil aquarium synchronisé: %d cm\n", value);
    }
  }

  // 103: Seuil Réservoir
  if (doc.containsKey("tankThreshold") || doc.containsKey("103")) {
    auto v = doc.containsKey("tankThreshold") ? doc["tankThreshold"] : doc["103"];
    int value = parseIntValue(v);
    if (value > 0) {
      setTankThresholdCm(static_cast<uint16_t>(value));
      autoCtrl.setTankThresholdCm(static_cast<uint16_t>(value));
      Serial.printf("[Network] ✅ Seuil réservoir synchronisé: %d cm\n", value);
    }
  }

  // 114: Limite inondation
  if (doc.containsKey("limFlood") || doc.containsKey("114")) {
    auto v = doc.containsKey("limFlood") ? doc["limFlood"] : doc["114"];
    int value = parseIntValue(v);
    if (value >= 0) {
      setLimFlood(static_cast<uint16_t>(value));
      autoCtrl.setLimFlood(static_cast<uint16_t>(value));
      Serial.printf("[Network] ✅ Limite inondation synchronisée: %d cm\n", value);
    }
  }

  // 113: Durée remplissage (tempsRemplissageSec ou refillDuration)
  if (doc.containsKey("tempsRemplissageSec") || doc.containsKey("refillDuration") || doc.containsKey("113")) {
    auto v = doc.containsKey("tempsRemplissageSec") ? doc["tempsRemplissageSec"] : 
             (doc.containsKey("refillDuration") ? doc["refillDuration"] : doc["113"]);
    int refillSec = parseIntValue(v);
    if (refillSec > 0 && refillSec <= 600) { // Validation: entre 1s et 600s (10 min max)
      uint32_t refillMs = static_cast<uint32_t>(refillSec) * 1000UL;
      autoCtrl.setRefillDurationMs(refillMs);
      Serial.printf("[Network] ✅ Durée remplissage mise à jour: %d s (%lu ms)\n", refillSec, refillMs);
    } else {
      Serial.printf("[Network] ⚠️ tempsRemplissageSec invalide: %d s (ignoré, doit être entre 1 et 600)\n", refillSec);
    }
  }

  // 111: Durée nourrissage gros (tempsGros)
  if (doc.containsKey("tempsGros") || doc.containsKey("111")) {
    auto v = doc.containsKey("tempsGros") ? doc["tempsGros"] : doc["111"];
    int value = parseIntValue(v);
    if (value > 0 && value <= 300) { // Validation: entre 1s et 300s (5 min max)
      autoCtrl.setTempsGros(static_cast<uint16_t>(value));
      Serial.printf("[Network] ✅ Durée nourrissage gros mise à jour: %d s\n", value);
    }
  }

  // 112: Durée nourrissage petits (tempsPetits)
  if (doc.containsKey("tempsPetits") || doc.containsKey("112")) {
    auto v = doc.containsKey("tempsPetits") ? doc["tempsPetits"] : doc["112"];
    int value = parseIntValue(v);
    if (value > 0 && value <= 300) { // Validation: entre 1s et 300s (5 min max)
      autoCtrl.setTempsPetits(static_cast<uint16_t>(value));
      Serial.printf("[Network] ✅ Durée nourrissage petits mise à jour: %d s\n", value);
    }
  }

  // 105: Heure nourrissage matin (bouffeMatin)
  if (doc.containsKey("bouffeMatin") || doc.containsKey("bm") || doc.containsKey("105")) {
    auto v = doc.containsKey("bouffeMatin") ? doc["bouffeMatin"] : 
             (doc.containsKey("bm") ? doc["bm"] : doc["105"]);
    int value = parseIntValue(v);
    if (value >= 0 && value <= 23) {
      autoCtrl.setBouffeMatin(static_cast<uint8_t>(value));
      Serial.printf("[Network] ✅ Heure nourrissage matin mise à jour: %d h\n", value);
    }
  }

  // 106: Heure nourrissage midi (bouffeMidi)
  if (doc.containsKey("bouffeMidi") || doc.containsKey("bmi") || doc.containsKey("106")) {
    auto v = doc.containsKey("bouffeMidi") ? doc["bouffeMidi"] : 
             (doc.containsKey("bmi") ? doc["bmi"] : doc["106"]);
    int value = parseIntValue(v);
    if (value >= 0 && value <= 23) {
      autoCtrl.setBouffeMidi(static_cast<uint8_t>(value));
      Serial.printf("[Network] ✅ Heure nourrissage midi mise à jour: %d h\n", value);
    }
  }

  // 107: Heure nourrissage soir (bouffeSoir)
  if (doc.containsKey("bouffeSoir") || doc.containsKey("bs") || doc.containsKey("107")) {
    auto v = doc.containsKey("bouffeSoir") ? doc["bouffeSoir"] : 
             (doc.containsKey("bs") ? doc["bs"] : doc["107"]);
    int value = parseIntValue(v);
    if (value >= 0 && value <= 23) {
      autoCtrl.setBouffeSoir(static_cast<uint8_t>(value));
      Serial.printf("[Network] ✅ Heure nourrissage soir mise à jour: %d h\n", value);
    }
  }

  if (doc.containsKey("chauffageThreshold") || doc.containsKey("104")) {
    auto v = doc.containsKey("chauffageThreshold") ? doc["chauffageThreshold"] : doc["104"];
    float value = parseFloatValue(v);
    if (value > 0.0f && !isnan(value)) {
      setHeaterThresholdC(value);
      autoCtrl.setHeaterThresholdC(value);
      Serial.printf("[Network] 🔥 Seuil chauffage synchronisé: %.1f°C\n", value);
    }
  }

  if (doc.containsKey("mail")) {
    const char* m = doc["mail"].as<const char*>();
    setEmailAddress(m);
  }

  if (doc.containsKey("mailNotif")) {
    auto v = doc["mailNotif"];
    bool enabled = _emailEnabled;
    if (v.is<bool>()) {
      enabled = v.as<bool>();
    } else if (v.is<int>()) {
      enabled = (v.as<int>() == 1);
    } else if (v.is<const char*>()) {
      const char* p = v.as<const char*>();
      if (p) {
        char buffer[16];
        size_t len = strnlen(p, sizeof(buffer) - 1);
        memcpy(buffer, p, len);
        buffer[len] = '\0';
        char* start = buffer;
        while (*start && isspace(static_cast<unsigned char>(*start))) ++start;
        char* end = start + strlen(start);
        while (end > start && isspace(static_cast<unsigned char>(end[-1]))) --end;
        *end = '\0';
        for (char* c = start; *c; ++c) {
          *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        }
        enabled = (strcmp(start, "1") == 0 || strcmp(start, "true") == 0 ||
                   strcmp(start, "on") == 0 || strcmp(start, "checked") == 0 ||
                   strcmp(start, "yes") == 0);
      }
    }
    setEmailEnabled(enabled);
  }

  if (doc.containsKey("FreqWakeUp")) {
    int fv = parseIntValue(doc["FreqWakeUp"]);
    if (fv > 0) {
      setFreqWakeSec(static_cast<uint16_t>(fv));
    }
  }

  Serial.println(F("[Network] Configuration distante appliquée"));
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 4: handleRemoteFeedingCommands()
// Gestion nourrissage manuel distant
// ============================================================================

void AutomatismNetwork::handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    // Commande "bouffePetits" (ou GPIO 108 en fallback)
    bool triggerSmall = false;
    if (doc.containsKey("bouffePetits")) {
        triggerSmall = isTrue(doc["bouffePetits"]);
    } else if (doc.containsKey("108")) {
        // Fallback: chercher GPIO 108 si "bouffePetits" absent
        triggerSmall = isTrue(doc["108"]);
        Serial.println(F("[Network] 🔧 Utilisation GPIO 108 pour nourrissage petits (fallback)"));
    }
    
    if (triggerSmall) {
        Serial.println(F("[Network] 🐟 Commande nourrissage PETITS reçue du serveur distant"));
        
        // Exécution de la commande
        autoCtrl.manualFeedSmall();
        
        // v11.31: ACK immédiat + log
        sendCommandAck("bouffePetits", "executed");
        logRemoteCommandExecution("fd_small", true);
        
        if (_emailEnabled) {
            char messageBuffer[FEED_MESSAGE_BUFFER_SIZE];
            size_t msgLen = autoCtrl.createFeedingMessage(
                messageBuffer,
                sizeof(messageBuffer),
                "Bouffe manuelle - Petits poissons",
                autoCtrl.getFeedBigDur(),
                autoCtrl.getFeedSmallDur()
            );
            (void)msgLen;
            // NOTE: Mailer nécessaire - autoCtrl doit le gérer
        }
        
        autoCtrl.armMailBlink();
        // v11.66: Reset géré par callback de completion dans automatism_feeding.cpp
        // autoCtrl.sendFullUpdate(autoCtrl.readSensors(), "bouffePetits=0&108=0");  // SUPPRIMÉ
    }
    
    // Commande "bouffeGros" (ou GPIO 109 en fallback)
    bool triggerBig = false;
    if (doc.containsKey("bouffeGros")) {
        triggerBig = isTrue(doc["bouffeGros"]);
    } else if (doc.containsKey("109")) {
        // Fallback: chercher GPIO 109 si "bouffeGros" absent
        triggerBig = isTrue(doc["109"]);
        Serial.println(F("[Network] 🔧 Utilisation GPIO 109 pour nourrissage gros (fallback)"));
    }
    
    if (triggerBig) {
        Serial.println(F("[Network] 🐟 Commande nourrissage GROS reçue du serveur distant"));
        
        // Exécution de la commande
        autoCtrl.manualFeedBig();
        
        // v11.31: ACK immédiat + log
        sendCommandAck("bouffeGros", "executed");
        logRemoteCommandExecution("fd_large", true);
        
        if (_emailEnabled) {
            char messageBuffer[FEED_MESSAGE_BUFFER_SIZE];
            size_t msgLen = autoCtrl.createFeedingMessage(
                messageBuffer,
                sizeof(messageBuffer),
                "Bouffe manuelle - Gros poissons",
                autoCtrl.getFeedBigDur(),
                autoCtrl.getFeedSmallDur()
            );
            (void)msgLen;
            // NOTE: Mailer nécessaire
        }
        
        autoCtrl.armMailBlink();
        // v11.66: Reset géré par callback de completion dans automatism_feeding.cpp
        // autoCtrl.sendFullUpdate(autoCtrl.readSensors(), "bouffeGros=0&109=0");  // SUPPRIMÉ
    }
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 5: handleRemoteActuators()
// Gestion actionneurs + GPIO dynamiques
// NOTE: Cette méthode est TRÈS complexe (250 lignes) et nécessite de nombreux
// accès aux membres privés d'Automatism. Pour Phase 2.11, on la laisse simplifiée
// et on la complètera dans une phase ultérieure quand on aura refactorisé
// les variables membres dans des modules dédiés.
// ============================================================================

void AutomatismNetwork::handleRemoteActuators(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    // v11.69: DÉSACTIVÉ - Utilisation exclusive de GPIOParser pour éviter conflits
    // Cette méthode créait une double gestion des commandes et bloquait les commandes distantes
    // pendant 5 secondes après toute action locale, empêchant le contrôle à distance.
    Serial.println(F("[Network] handleRemoteActuators DÉSACTIVÉ (v11.69) - GPIOParser utilisé"));
    return; // Désactivation complète
    
    // NOTE: Cette méthode gère les actionneurs par NOM (light, heat, pump_aqua, etc.)
    // Le code dans automatism.cpp gère les GPIO par NUMÉRO (fallback rétrocompatibilité)
    // Cela permet de supporter les deux formats: noms ET numéros
    
    // ========================================
    // PRIORITÉ LOCALE (v11.38): Bloquer serveur distant pendant 5s après action locale
    // FIX v11.38: Bloquer AUSSI si action récente (même si sync réussie)
    // ========================================
    const uint32_t LOCAL_PRIORITY_TIMEOUT_MS = 5000; // 5 secondes de priorité locale
    
    // PRIORITÉ 1: Bloquer si action locale récente (MÊME si sync déjà réussie)
    // Cela évite que le serveur distant n'écrase l'état pendant la fenêtre de 5 secondes
    if (AutomatismPersistence::hasRecentLocalAction(LOCAL_PRIORITY_TIMEOUT_MS)) {
        uint32_t lastLocal = AutomatismPersistence::getLastLocalActionTime();
        uint32_t elapsed = millis() - lastLocal;
        Serial.printf("[Network] ⚠️ PRIORITÉ LOCALE ACTIVE - Commandes distantes bloquées (%lu ms écoulées)\n", elapsed);
        return; // Ignorer TOUTES les commandes distantes
    }
    
    // PRIORITÉ 2: Bloquer si sync en attente (backup si sync lente)
    if (AutomatismPersistence::hasPendingSync()) {
        uint32_t pendingCount = AutomatismPersistence::getPendingSyncCount();
        uint32_t lastPending = AutomatismPersistence::getLastPendingSyncTime();
        uint32_t elapsed = millis() - lastPending;
        Serial.printf("[Network] ⏳ PRIORITÉ LOCALE - Sync en attente (%u items, %lu ms écoulées)\n", 
                      pendingCount, elapsed);
        return; // Ignorer les commandes distantes tant que sync pending
    }
    
    // === LOGS DÉTAILLÉS POUR DIAGNOSTIC ===
    Serial.println(F("[Network] === ACTIONNEURS REÇUS DU SERVEUR ==="));
    for (auto kv : doc.as<ArduinoJson::JsonObjectConst>()) {
        const char* key = kv.key().c_str();
        // N'afficher que les clés d'actionneurs connues
        if (strcmp(key, "light") == 0 || strcmp(key, "lightCmd") == 0 ||
            strcmp(key, "heat") == 0 || strcmp(key, "heatCmd") == 0 ||
            strcmp(key, "pump_aqua") == 0 || strcmp(key, "pump_tank") == 0) {
            Serial.printf("[Network] Clé '%s' = %s\n", key, kv.value().as<String>().c_str());
        }
    }
    Serial.println(F("[Network] === FIN ACTIONNEURS ==="));
    
    // ========================================
    // LUMIÈRE (lightCmd prioritaire sur light)
    // ========================================
    if (doc.containsKey("lightCmd")) {
        auto v = doc["lightCmd"];
        if (isTrue(v)) {
            autoCtrl.startLightManualLocal();
            Serial.println(F("[Network] Lumière ON (commande explicite lightCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopLightManualLocal();
            Serial.println(F("[Network] Lumière OFF (commande explicite lightCmd)"));
        }
    } else if (doc.containsKey("light")) {
        auto v = doc["light"];
        if (isTrue(v)) {
            autoCtrl.startLightManualLocal();
            Serial.println(F("[Network] Lumière ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopLightManualLocal();
            Serial.println(F("[Network] Lumière OFF (état distant)"));
        }
    }
    
    // ========================================
    // CHAUFFAGE (heatCmd prioritaire sur heat)
    // ========================================
    if (doc.containsKey("heatCmd")) {
        auto v = doc["heatCmd"];
        if (isTrue(v)) {
            autoCtrl.startHeaterManualLocal();
            Serial.println(F("[Network] Chauffage ON (commande explicite heatCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopHeaterManualLocal();
            Serial.println(F("[Network] Chauffage OFF (commande explicite heatCmd)"));
        }
    } else if (doc.containsKey("heat")) {
        auto v = doc["heat"];
        if (isTrue(v)) {
            autoCtrl.startHeaterManualLocal();
            Serial.println(F("[Network] Chauffage ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopHeaterManualLocal();
            Serial.println(F("[Network] Chauffage OFF (état distant)"));
        }
    }
    
    // ========================================
    // POMPE AQUARIUM (pump_aquaCmd prioritaire sur pump_aqua)
    // ========================================
    if (doc.containsKey("pump_aquaCmd")) {
        auto v = doc["pump_aquaCmd"];
        if (isTrue(v)) {
            autoCtrl.startAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium ON (commande explicite pump_aquaCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium OFF (commande explicite pump_aquaCmd)"));
        }
    } else if (doc.containsKey("pump_aqua")) {
        auto v = doc["pump_aqua"];
        if (isTrue(v)) {
            autoCtrl.startAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium OFF (état distant)"));
        }
    }
    
    // ========================================
    // POMPE RÉSERVOIR (pump_tankCmd prioritaire sur pump_tank) - v11.31: ACK + logs
    // ========================================
    if (doc.containsKey("pump_tankCmd")) {
        auto v = doc["pump_tankCmd"];
        if (isTrue(v)) {
            Serial.println(F("[Network] 💧 Commande pompe TANK ON reçue du serveur distant"));
            autoCtrl.startTankPumpManual();
            sendCommandAck("pump_tank", "on");
            logRemoteCommandExecution("ptank_on", true);
            
            // v11.59: Reset GPIO virtuals to prevent infinite loops
            SensorReadings readings = autoCtrl.readSensors();
            autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
            Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
        } else if (isFalse(v)) {
            Serial.println(F("[Network] 💧 Commande pompe TANK OFF reçue du serveur distant"));
            autoCtrl.stopTankPumpManual();
            sendCommandAck("pump_tank", "off");
            logRemoteCommandExecution("ptank_off", true);
            
            // v11.59: Reset GPIO virtuals to prevent infinite loops
            SensorReadings readings = autoCtrl.readSensors();
            autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
            Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
        }
    } else if (doc.containsKey("pump_tank")) {
        auto v = doc["pump_tank"];
        if (isTrue(v)) {
            // Protection: Ne redémarrer QUE si pompe arrêtée
            if (!autoCtrl.isTankPumpRunning()) {
                autoCtrl.startTankPumpManual();
                sendCommandAck("pump_tank", "on");
                logRemoteCommandExecution("ptank_on", true);
                Serial.println(F("[Network] Pompe réservoir démarrée (commande distante)"));
                
                // v11.66: Reset GPIO virtuals to prevent infinite loops
                SensorReadings readings = autoCtrl.readSensors();
                autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
                Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
            } else {
                Serial.println(F("[Network] ⚠️ Commande pump_tank=1 IGNORÉE - cycle déjà en cours"));
            }
        } else if (isFalse(v)) {
            // Arrêt seulement si effectivement en cours
            if (autoCtrl.isTankPumpRunning()) {
                autoCtrl.stopTankPumpManual();
                sendCommandAck("pump_tank", "off");
                logRemoteCommandExecution("ptank_off", true);
                Serial.println(F("[Network] Pompe réservoir arrêtée (commande distante)"));
                
                // v11.66: Reset GPIO virtuals to prevent infinite loops
                SensorReadings readings = autoCtrl.readSensors();
                autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
                Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
            } else {
                Serial.println(F("[Network] ⚠️ Commande pump_tank=0 IGNORÉE - pompe déjà arrêtée"));
            }
        }
    }
    
    Serial.println(F("[Network] handleRemoteActuators() - v11.07 COMPLET (light, heat, pumps)"));
}

// ============================================================================
// CHECK CRITICAL CHANGES
// NOTE: Méthode ÉNORME (285 lignes) pour détecter changements critiques
// ============================================================================

void AutomatismNetwork::checkCriticalChanges(const SensorReadings& readings) {
    // TODO: Méthode de 285 lignes à implémenter
    // Détecte changements actionneurs, bouffe, etc.
    
    // Pour l'instant, reste dans automatism.cpp
    // Sera migrée dans Phase 2.7
    
    Serial.println(F("[Network] checkCriticalChanges() - Implémentation temporaire"));
}

// ============================================================================
// PERSISTANCE ET ACK - v11.31
// ============================================================================

uint16_t AutomatismNetwork::replayQueuedData() {
    uint16_t sent = 0;
    const uint16_t MAX_REPLAYS_PER_CYCLE = 5;  // v11.70: LIMITÉ à 5 pour stabilité
    
    while (_dataQueue.size() > 0 && sent < MAX_REPLAYS_PER_CYCLE) {
        // Lire sans supprimer
        String payload = _dataQueue.peek();
        if (payload.length() == 0) {
            Serial.println(F("[Network] ⚠️ Empty payload in queue, skipping"));
            _dataQueue.pop();
            continue;
        }
        
        // Tentative envoi
        Serial.printf("[Network] Replaying queued payload (%u bytes)...\n", payload.length());
        if (_web.postRaw(payload)) {
            // Succès : supprimer de la queue
            _dataQueue.pop();
            sent++;
            Serial.printf("[Network] ✓ Replayed payload %u/%u\n", sent, MAX_REPLAYS_PER_CYCLE);
            
            // Note: Watchdog géré par la tâche appelante (automationTask)
            // Ne pas appeler esp_task_wdt_reset() ici (erreur "task not found")
        } else {
            // Échec : arrêter pour ne pas vider la batterie
            Serial.println(F("[Network] ✗ Replay failed, stopping"));
            break;
        }
    }
    
    if (sent > 0) {
        Serial.printf("[Network] Replay summary: %u sent, %u remaining\n", sent, _dataQueue.size());
    }
    
    return sent;
}

bool AutomatismNetwork::sendCommandAck(const char* command, const char* status) {
    // Payload minimal pour ACK
    char ackPayload[256];
    snprintf(ackPayload, sizeof(ackPayload),
             "api_key=%s&sensor=%s&ack_command=%s&ack_status=%s&ack_timestamp=%lu",
             ApiConfig::API_KEY, ProjectConfig::BOARD_TYPE, command, status, millis());
    
    // Envoi non-bloquant vers endpoint principal
    // NOTE: Le serveur peut ignorer les champs ack_* si endpoint non prévu
    // C'est acceptable car l'envoi périodique contient l'état complet
    String response;
    bool ok = _web.postRaw(String(ackPayload));
    
    if (ok) {
        Serial.printf("[Network] ✓ ACK sent: %s=%s\n", command, status);
    } else {
        Serial.printf("[Network] ✗ ACK failed: %s=%s (non-bloquant)\n", command, status);
    }
    
    return ok;
}

void AutomatismNetwork::logRemoteCommandExecution(const char* command, bool success) {
    // Compteurs globaux
    int totalCmds, successCmds;
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "cmd_total", totalCmds, 0);
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "cmd_success", successCmds, 0);
    totalCmds++;
    if (success) successCmds++;

    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "cmd_total", totalCmds);
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "cmd_success", successCmds);

    // Compteurs par commande
    char key[32];
    snprintf(key, sizeof(key), "cmd_%s", command);
    int cmdTotal;
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, key, cmdTotal, 0);
    cmdTotal++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, key, cmdTotal);
    
    // Log avec statistiques
    float successRate = (totalCmds > 0) ? ((float)successCmds / totalCmds * 100.0f) : 0.0f;
    Serial.printf("[Network] Command '%s': %s (Global: %.1f%% success, Total: %u, This cmd: %u times)\n", 
                  command, success ? "✓ OK" : "✗ FAILED", successRate, totalCmds, cmdTotal);
}

