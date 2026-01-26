#include "automatism/automatism_sync.h"
#include "automatism.h"
#include "config.h"
#include "esp_task_wdt.h"
#include <WiFi.h>
#include "app_tasks.h"

AutomatismSync::AutomatismSync(WebClient& web, ConfigManager& cfg)
    : _web(web)
    , _config(cfg)
    , _dataQueue(QUEUE_MAX_ENTRIES)
    , _serverOk(false)
    , _sendState(0)
    , _recvState(0)
    , _lastSend(0)
    , _lastRemoteFetch(0)
    , _limFlood(5)
    , _aqThresholdCm(30)
    , _tankThresholdCm(30)
    , _heaterThresholdC(24.0f)
    , _emailEnabled(false)
    #if defined(PROFILE_TEST)
    , _freqWakeSec(6)  // 6s par défaut pour wroom-test
    #else
    , _freqWakeSec(600)  // 600s par défaut pour production
    #endif
    , _consecutiveSendFailures(0)
    , _currentBackoffMs(0)
    , _lastSendAttemptMs(0)
    , _lastRemoteFeedResetMs(0)
{
    _emailAddress[0] = '\0';
    Serial.println(F("[Sync] Module initialisé"));
}

bool AutomatismSync::begin() {
    // Initialiser la queue de données (après montage LittleFS)
    if (_dataQueue.begin()) {
        Serial.printf("[Sync] ✓ DataQueue initialisée (%u entrées)\n", _dataQueue.size());
        return true;
    }
    Serial.println(F("[Sync] ⚠️ Échec DataQueue"));
    return false;
}

void AutomatismSync::setEmailAddress(const char* address) {
    if (!address) {
        _emailAddress[0] = '\0';
        return;
    }
    strncpy(_emailAddress, address, sizeof(_emailAddress) - 1);
    _emailAddress[sizeof(_emailAddress) - 1] = '\0';
}

void AutomatismSync::update(const SensorReadings& readings, SystemActuators& acts, Automatism& core) {
    // Synchronisation périodique
    uint32_t now = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool sendEnabled = _config.isRemoteSendEnabled();
    bool recvEnabled = _config.isRemoteRecvEnabled();
    uint32_t timeSinceLastSend = now - _lastSend;
    bool intervalReached = (timeSinceLastSend > SEND_INTERVAL_MS);
    
    if (wifiConnected && sendEnabled) {
        if (intervalReached) {
            Serial.printf("[Sync] ✅ Conditions remplies, envoi POST... (dernier envoi il y a %lu ms)\n", timeSinceLastSend);
            sendFullUpdate(readings, acts, core);
        }
    } else {
        // Log seulement si conditions changent pour éviter spam
        static bool lastWifiState = true;
        static bool lastSendEnabledState = true;
        static bool lastRecvEnabledState = true;
        if (wifiConnected != lastWifiState || sendEnabled != lastSendEnabledState || recvEnabled != lastRecvEnabledState) {
            Serial.printf("[Sync] ⚠️ Envoi POST bloqué: WiFi=%s, SendEnabled=%s, RecvEnabled=%s\n",
                          wifiConnected ? "OK" : "NO",
                          sendEnabled ? "YES" : "NO",
                          recvEnabled ? "YES" : "NO");
            lastWifiState = wifiConnected;
            lastSendEnabledState = sendEnabled;
            lastRecvEnabledState = recvEnabled;
        }
    }
}

// Helpers pour parsing JSON
bool isTrue(ArduinoJson::JsonVariantConst v) {
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

void AutomatismSync::handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    const uint32_t nowMs = millis();
    auto tryResetRemoteFlags = [&](const char* extraPairs) {
        if (WiFi.status() != WL_CONNECTED || !_config.isRemoteSendEnabled()) {
            return;
        }
        if (nowMs - _lastRemoteFeedResetMs < REMOTE_FEED_RESET_COOLDOWN_MS) {
            return;
        }
        _lastRemoteFeedResetMs = nowMs;
        SensorReadings readings = autoCtrl.readSensors();
        bool resetOk = autoCtrl.sendFullUpdate(readings, extraPairs);
        Serial.printf("[Sync] 🔁 Reset flags nourrissage %s\n", resetOk ? "envoyé" : "en attente");
    };

    // Commande "bouffePetits" (ou GPIO 108 en fallback)
    bool triggerSmall = false;
    if (doc.containsKey("bouffePetits")) {
        triggerSmall = isTrue(doc["bouffePetits"]);
    } else if (doc.containsKey("108")) {
        triggerSmall = isTrue(doc["108"]);
        Serial.println(F("[Sync] 🔧 Utilisation GPIO 108 pour nourrissage petits (fallback)"));
    }
    
    if (triggerSmall) {
        Serial.println(F("[Sync] 🐟 Commande nourrissage PETITS reçue du serveur distant"));
        autoCtrl.manualFeedSmall();
        sendCommandAck("bouffePetits", "executed");
        logRemoteCommandExecution("fd_small", true);
        tryResetRemoteFlags("bouffePetits=0&108=0");
        
        if (_emailEnabled) {
            char messageBuffer[256];
            autoCtrl.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
                "Bouffe manuelle - Petits poissons",
                autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
            // Envoyer l'email via autoCtrl (qui délègue au Mailer)
            autoCtrl._mailer.send("Nourrissage manuel - Petits poissons", 
                                  messageBuffer, 
                                  "System", 
                                  autoCtrl.getEmailAddress());
        }
        autoCtrl.armMailBlink();
    }
    
    // Commande "bouffeGros" (ou GPIO 109 en fallback)
    bool triggerBig = false;
    if (doc.containsKey("bouffeGros")) {
        triggerBig = isTrue(doc["bouffeGros"]);
    } else if (doc.containsKey("109")) {
        triggerBig = isTrue(doc["109"]);
        Serial.println(F("[Sync] 🔧 Utilisation GPIO 109 pour nourrissage gros (fallback)"));
    }
    
    if (triggerBig) {
        Serial.println(F("[Sync] 🐠 Commande nourrissage GROS reçue du serveur distant"));
        autoCtrl.manualFeedBig();
        sendCommandAck("bouffeGros", "executed");
        logRemoteCommandExecution("fd_large", true);
        tryResetRemoteFlags("bouffeGros=0&109=0");
        
        if (_emailEnabled) {
            char messageBuffer[256];
            autoCtrl.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
                "Bouffe manuelle - Gros poissons",
                autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
            // Envoyer l'email via autoCtrl (qui délègue au Mailer)
            autoCtrl._mailer.send("Nourrissage manuel - Gros poissons", 
                                  messageBuffer, 
                                  "System", 
                                  autoCtrl.getEmailAddress());
        }
        autoCtrl.armMailBlink();
    }
}

void AutomatismSync::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
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

    if (doc.containsKey("mail")) {
        setEmailAddress(doc["mail"].as<const char*>());
    }

    if (doc.containsKey("mailNotif")) {
        setEmailEnabled(isTrue(doc["mailNotif"]));
    }

    if (doc.containsKey("FreqWakeUp")) {
        int val = parseIntValue(doc["FreqWakeUp"]);
        if (val >= 0) setFreqWakeSec(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("limFlood")) {
        int val = parseIntValue(doc["limFlood"]);
        if (val >= 0) setLimFlood(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("aqThreshold")) {
        int val = parseIntValue(doc["aqThreshold"]);
        if (val > 0) setAqThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("tankThreshold")) {
        int val = parseIntValue(doc["tankThreshold"]);
        if (val > 0) setTankThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("chauffageThreshold")) {
        float val = parseFloatValue(doc["chauffageThreshold"]);
        if (val > 0.0f && !isnan(val)) setHeaterThresholdC(val);
    }

    Serial.println(F("[Sync] Configuration appliquée depuis JSON"));
}

bool AutomatismSync::sendFullUpdate(const SensorReadings& readings,
                                    SystemActuators& acts,
                                    Automatism& core,
                                    const char* extraPairs) {
    uint32_t attemptStartMs = millis();
    if (!canAttemptSend(attemptStartMs)) {
        return false;
    }
    _lastSendAttemptMs = attemptStartMs;

    uint32_t heapBefore = ESP.getFreeHeap();
    if (heapBefore < BufferConfig::LOW_MEMORY_THRESHOLD_BYTES) {
        Serial.printf("[Sync] Mémoire faible (%u bytes), report envoi\n", heapBefore);
        return false;
    }

    char payloadBuffer[1024];
    // Construction du payload (logique migrée de AutomatismNetwork)
    // Nécessite des accesseurs sur Automatism (core)
    int diffMaree = core.computeDiffMaree(readings.wlAqua);
    int len = snprintf(payloadBuffer, sizeof(payloadBuffer),
        "api_key=%s&sensor=%s&version=%s&TempAir=%.1f&Humidite=%.1f&TempEau=%.1f&"
        "EauPotager=%u&EauAquarium=%u&EauReserve=%u&diffMaree=%d&Luminosite=%u&"
        "etatPompeAqua=%d&etatPompeTank=%d&etatHeat=%d&etatUV=%d&"
        "bouffeMatin=%u&bouffeMidi=%u&bouffeSoir=%u&tempsGros=%u&tempsPetits=%u&"
        "aqThreshold=%u&tankThreshold=%u&chauffageThreshold=%.1f&"
        "tempsRemplissageSec=%u&limFlood=%u&WakeUp=%d&FreqWakeUp=%u&"
        "bouffePetits=%s&bouffeGros=%s&mail=%s&mailNotif=%s",
        ApiConfig::API_KEY, ProjectConfig::BOARD_TYPE, ProjectConfig::VERSION,
        readings.tempAir, readings.humidity, readings.tempWater,
        readings.wlPota, readings.wlAqua, readings.wlTank, diffMaree, readings.luminosite,
        acts.isAquaPumpRunning(), acts.isTankPumpRunning(), acts.isHeaterOn(), acts.isLightOn(),
        core.getBouffeMatin(), core.getBouffeMidi(), core.getBouffeSoir(),
        core.getTempsGros(), core.getTempsPetits(),
        _aqThresholdCm, _tankThresholdCm, _heaterThresholdC,
        core.getRefillDurationSec(), _limFlood,
        core.getForceWakeUp() ? 1 : 0, _freqWakeSec,
        core.getBouffePetitsFlag(), core.getBouffeGrosFlag(),
        _emailAddress, _emailEnabled ? "checked" : "");

    if (len < 0 || len >= (int)sizeof(payloadBuffer)) {
        Serial.println(F("[Sync] Erreur formatage payload"));
        return false;
    }

    if (extraPairs && extraPairs[0] != '\0') {
        strncat(payloadBuffer, "&", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
        strncat(payloadBuffer, extraPairs, sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
    }

    if (strstr(payloadBuffer, "resetMode=") == nullptr) {
        strncat(payloadBuffer, "&resetMode=0", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
    }

    esp_task_wdt_reset();
    uint32_t sendStart = millis();
    bool success = AppTasks::netPostRaw(payloadBuffer, 30000);
    uint32_t durationMs = millis() - sendStart;
    
    registerSendResult(success, strlen(payloadBuffer), durationMs, heapBefore, ESP.getFreeHeap());
    
    if (success) {
        _sendState = 1;
        _lastSend = millis();
        if (_dataQueue.size() > 0) replayQueuedData();
    } else {
        _sendState = -1;
        _dataQueue.push(payloadBuffer);
    }

    return success;
}

bool AutomatismSync::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    // v11.158: Réduire timeout de 30s à 12s pour éviter blocages longs
    // Le timeout absolu dans netRpc() est de 15s, donc 12s laisse une marge
    bool ok = AppTasks::netFetchRemoteState(doc, 12000);
    if (ok && doc.size() > 0) {
        char jsonStr[2048];
        serializeJson(doc, jsonStr, sizeof(jsonStr));
        _config.saveRemoteVars(jsonStr);
        _serverOk = true;
        _recvState = 1;
    } else {
        _serverOk = false;
        _recvState = -1;
    }
    _lastRemoteFetch = millis();
    return ok;
}

bool AutomatismSync::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis) {
    if (!_config.isRemoteRecvEnabled()) return false;
    if (currentMillis - _lastRemoteFetch < REMOTE_FETCH_INTERVAL_MS) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    return fetchRemoteState(doc);
}

// Helpers
bool AutomatismSync::canAttemptSend(uint32_t nowMs) const {
    if (_consecutiveSendFailures == 0) return true;
    uint32_t elapsed = nowMs - _lastSendAttemptMs;
    return elapsed >= _currentBackoffMs;
}

void AutomatismSync::registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter) {
    if (success) {
        _consecutiveSendFailures = 0;
        _currentBackoffMs = 0;
    } else {
        if (_consecutiveSendFailures < 32) _consecutiveSendFailures++;
        _currentBackoffMs = 2000 << (_consecutiveSendFailures > 0 ? (_consecutiveSendFailures - 1) : 0);
        if (_currentBackoffMs > 60000) _currentBackoffMs = 60000;
    }
}

uint16_t AutomatismSync::replayQueuedData() {
    uint16_t sent = 0;
    while (_dataQueue.size() > 0 && sent < 5) {
        char payload[1024];
        if (_dataQueue.peek(payload, sizeof(payload))) {
            if (AppTasks::netPostRaw(payload, 30000)) {
                _dataQueue.pop(payload, sizeof(payload));
                sent++;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    return sent;
}

bool AutomatismSync::sendCommandAck(const char* command, const char* status) {
    char ackPayload[256];
    snprintf(ackPayload, sizeof(ackPayload),
             "api_key=%s&sensor=%s&ack_command=%s&ack_status=%s&ack_timestamp=%lu",
             ApiConfig::API_KEY, ProjectConfig::BOARD_TYPE, command, status, millis());
    return AppTasks::netPostRaw(ackPayload, 30000);
}

void AutomatismSync::logRemoteCommandExecution(const char* command, bool success) {
    // Logique simplifiée sans NVS pour l'instant
    Serial.printf("[Sync] Command '%s': %s\n", command, success ? "OK" : "FAILED");
}

void AutomatismSync::logQueueState(const char* reason, uint16_t size) const {
    Serial.printf("[Sync] Queue %s (%u)\n", reason, size);
}

