#include "automatism/automatism_sync.h"
#include "automatism.h"
#include "config.h"
#include "esp_task_wdt.h"
#include <WiFi.h>
#include "app_tasks.h"
#include <cmath>
#include <cstring>

// v11.172: Les noms de variables utilisés ici sont définis dans gpio_mapping.h (VariableRegistry)
// Source de vérité: GPIOMap::XXX.serverPostName pour noms POST serveur
// Ex: GPIOMap::HEAT_THRESHOLD.serverPostName == "chauffageThreshold"

AutomatismSync::AutomatismSync(WebClient& web, ConfigManager& cfg)
    : _web(web)
    , _config(cfg)
    , _dataQueue(QUEUE_MAX_ENTRIES)
    , _serverOk(false)
    , _sendState(0)
    , _recvState(0)
    , _lastSend(0)
    , _lastRemoteFetch(0)
    , _lastRemoteFeedResetMs(0)
    // v11.168: Flag configSynced - false tant qu'aucun poll serveur réussi
    , _configSyncedOnce(false)
    // v11.172: AutomatismSync est la source de vérité pour toutes les variables de config
    , _limFlood(8)  // Harmonisé avec ancienne valeur Automatism
    , _aqThresholdCm(ActuatorConfig::Default::AQUA_LEVEL_CM)
    , _tankThresholdCm(ActuatorConfig::Default::TANK_LEVEL_CM)
    , _heaterThresholdC(ActuatorConfig::Default::HEATER_THRESHOLD_C)
    , _emailEnabled(true)  // v11.172: true par défaut (harmonisé avec ancien mailNotif)
    #if defined(PROFILE_TEST)
    , _freqWakeSec(6)  // 6s par défaut pour wroom-test
    #else
    , _freqWakeSec(600)  // 600s par défaut pour production
    #endif
    , _consecutiveSendFailures(0)
    , _lastSendAttemptMs(0)
{
    _emailAddress[0] = '\0';
    Serial.println(F("[Sync] Module initialisé (configSynced=false)"));
}

bool AutomatismSync::begin() {
    // Initialiser la queue de données (RAM simple)
    if (_dataQueue.begin()) {
        Serial.printf("[Sync] ✓ DataQueue RAM initialisée (%u entrées max)\n", QUEUE_MAX_ENTRIES);
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
        triggerSmall = doc["bouffePetits"].as<bool>() || doc["bouffePetits"].as<int>() == 1;
    } else if (doc.containsKey("108")) {
        triggerSmall = doc["108"].as<bool>() || doc["108"].as<int>() == 1;
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
            autoCtrl.sendEmail("Nourrissage manuel - Petits poissons", 
                              messageBuffer, 
                              "System", 
                              autoCtrl.getEmailAddress());
        }
        autoCtrl.armMailBlink();
    }
    
    // Commande "bouffeGros" (ou GPIO 109 en fallback)
    bool triggerBig = false;
    if (doc.containsKey("bouffeGros")) {
        triggerBig = doc["bouffeGros"].as<bool>() || doc["bouffeGros"].as<int>() == 1;
    } else if (doc.containsKey("109")) {
        triggerBig = doc["109"].as<bool>() || doc["109"].as<int>() == 1;
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
            autoCtrl.sendEmail("Nourrissage manuel - Gros poissons", 
                              messageBuffer, 
                              "System", 
                              autoCtrl.getEmailAddress());
        }
        autoCtrl.armMailBlink();
    }
}

void AutomatismSync::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
    if (doc.containsKey("mail")) {
        setEmailAddress(doc["mail"].as<const char*>());
    }

    if (doc.containsKey("mailNotif")) {
        setEmailEnabled(doc["mailNotif"].as<bool>() || doc["mailNotif"].as<int>() == 1);
    }

    if (doc.containsKey("FreqWakeUp")) {
        int val = doc["FreqWakeUp"].as<int>();
        if (val >= 0) setFreqWakeSec(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("limFlood")) {
        int val = doc["limFlood"].as<int>();
        if (val >= 0) setLimFlood(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("aqThreshold")) {
        int val = doc["aqThreshold"].as<int>();
        if (val > 0) setAqThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("tankThreshold")) {
        int val = doc["tankThreshold"].as<int>();
        if (val > 0) setTankThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("chauffageThreshold")) {
        float val = doc["chauffageThreshold"].as<float>();
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
    
    // v11.168: Ajouter configSynced pour indiquer si la config ESP est fiable
    // Si configSynced=0, le serveur distant doit IGNORER les variables de config
    // pour éviter l'écrasement par des valeurs par défaut
    char configSyncedStr[16];
    snprintf(configSyncedStr, sizeof(configSyncedStr), "&configSynced=%d", _configSyncedOnce ? 1 : 0);
    strncat(payloadBuffer, configSyncedStr, sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
    
    if (!_configSyncedOnce) {
        Serial.println(F("[Sync] ⚠️ configSynced=0 - variables config ignorées par serveur"));
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
    bool ok = AppTasks::netFetchRemoteState(doc, 12000);
    if (ok && doc.size() > 0) {
        // Harmonisation config: normaliser les clés (canoniques serveur distant) avant sauvegarde NVS
        StaticJsonDocument<2048> normalizedDoc;
        JsonObject out = normalizedDoc.to<JsonObject>();
        for (ArduinoJson::JsonPair p : doc.as<ArduinoJson::JsonObject>()) {
            const char* k = p.key().c_str();
            if (strcmp(k, "105") == 0) {
                if (!out.containsKey("bouffeMatin")) out["bouffeMatin"] = p.value();
            } else if (strcmp(k, "106") == 0) {
                if (!out.containsKey("bouffeMidi")) out["bouffeMidi"] = p.value();
            } else if (strcmp(k, "107") == 0) {
                if (!out.containsKey("bouffeSoir")) out["bouffeSoir"] = p.value();
            } else if (strcmp(k, "heaterThreshold") == 0) {
                if (!out.containsKey("chauffageThreshold")) out["chauffageThreshold"] = p.value();
            } else if (strcmp(k, "refillDuration") == 0) {
                if (!out.containsKey("tempsRemplissageSec")) out["tempsRemplissageSec"] = p.value();
            } else {
                // Ne pas recopier les clés legacy (déjà mappées en canoniques)
                out[k] = p.value();
            }
        }
        char jsonStr[2048];
        serializeJson(normalizedDoc, jsonStr, sizeof(jsonStr));
        _config.saveRemoteVars(jsonStr);
        _serverOk = true;
        _recvState = 1;

        if (!_configSyncedOnce) {
            _configSyncedOnce = true;
            Serial.println(F("[Sync] ✅ configSynced=true (1er poll serveur réussi)"));
        }
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

// Helpers simplifiés (backoff supprimé - géré par web_client retry)
bool AutomatismSync::canAttemptSend(uint32_t nowMs) const {
    (void)nowMs;  // Pas de backoff, toujours autoriser
    return true;
}

void AutomatismSync::registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter) {
    (void)payloadBytes;
    (void)durationMs;
    (void)heapBefore;
    (void)heapAfter;
    // Backoff supprimé - les retries sont gérés par web_client
    if (success) {
        _consecutiveSendFailures = 0;
    } else {
        _consecutiveSendFailures++;
    }
}

uint16_t AutomatismSync::replayQueuedData() {
    uint16_t sent = 0;
    while (_dataQueue.size() > 0 && sent < 2) {
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

