#include "automatism/automatism_sync.h"
#include "automatism.h"
#include "config.h"
#include "gpio_parser.h"
#include "esp_task_wdt.h"
#include <WiFi.h>
#include "app_tasks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cmath>
#include <cstring>
#include <cstdlib>  // v11.183: atoi/atof pour valeurs serveur en string

namespace {
constexpr size_t DEFERRED_JSON_SIZE = 2048;
static char s_deferredRemoteJson[DEFERRED_JSON_SIZE];
static SemaphoreHandle_t s_deferredSaveMutex = nullptr;
static QueueHandle_t s_deferredSaveQueue = nullptr;
}  // namespace

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
    if (!s_deferredSaveMutex) {
        s_deferredSaveMutex = xSemaphoreCreateMutex();
    }
    if (!s_deferredSaveQueue) {
        s_deferredSaveQueue = xQueueCreate(1, 1);  // 1 slot, 1 byte (signal only)
    }
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

void AutomatismSync::seedInitialStateIfFirstPoll(const ArduinoJson::JsonDocument& doc) {
    if (_firstPollAfterBootDone) return;
    GPIOParser::seedFeedStateFromDoc(doc);
    _firstPollAfterBootDone = true;
    Serial.println(F("[Sync] État edge detection initialisé (1er poll)"));
}

void AutomatismSync::onRemoteFeedExecuted(bool isSmall, Automatism& core) {
    // Effets de bord après exécution nourrissage distant (appelé par GPIOParser)
    const uint32_t nowMs = millis();
    if (isSmall) {
        Serial.println(F("[Sync] 🐟 Commande nourrissage PETITS exécutée (front montant)"));
        sendCommandAck("bouffePetits", "executed");
        logRemoteCommandExecution("fd_small", true);
        if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled() &&
            (nowMs - _lastRemoteFeedResetMs) >= REMOTE_FEED_RESET_COOLDOWN_MS) {
            _lastRemoteFeedResetMs = nowMs;
            SensorReadings readings = core.readSensors();
            bool resetOk = core.sendFullUpdate(readings, "bouffePetits=0&108=0");
            Serial.printf("[Sync] 🔁 Reset flags nourrissage %s\n", resetOk ? "envoyé" : "en attente");
        }
        if (_emailEnabled) {
            char messageBuffer[256];
            core.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
                "Bouffe manuelle - Petits poissons",
                core.getFeedBigDur(), core.getFeedSmallDur());
            core.sendEmail("Nourrissage manuel - Petits poissons", messageBuffer,
                "System", core.getEmailAddress());
        }
    } else {
        Serial.println(F("[Sync] 🐠 Commande nourrissage GROS exécutée (front montant)"));
        sendCommandAck("bouffeGros", "executed");
        logRemoteCommandExecution("fd_large", true);
        if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled() &&
            (nowMs - _lastRemoteFeedResetMs) >= REMOTE_FEED_RESET_COOLDOWN_MS) {
            _lastRemoteFeedResetMs = nowMs;
            SensorReadings readings = core.readSensors();
            bool resetOk = core.sendFullUpdate(readings, "bouffeGros=0&109=0");
            Serial.printf("[Sync] 🔁 Reset flags nourrissage %s\n", resetOk ? "envoyé" : "en attente");
        }
        if (_emailEnabled) {
            char messageBuffer[256];
            core.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
                "Bouffe manuelle - Gros poissons",
                core.getFeedBigDur(), core.getFeedSmallDur());
            core.sendEmail("Nourrissage manuel - Gros poissons", messageBuffer,
                "System", core.getEmailAddress());
        }
    }
    core.armMailBlink();
}

// v11.183: Helper pour lire int depuis JSON (serveur envoie parfois string "200")
static int parseIntFromVariant(ArduinoJson::JsonVariantConst v) {
    if (v.is<int>()) return v.as<int>();
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? atoi(s) : 0;
    }
    return v.as<int>();
}

static float parseFloatFromVariant(ArduinoJson::JsonVariantConst v) {
    if (v.is<float>()) return v.as<float>();
    if (v.is<int>()) return static_cast<float>(v.as<int>());
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? static_cast<float>(atof(s)) : 0.0f;
    }
    return v.as<float>();
}

// Applique seuils + email + FreqWakeUp (clés 100-104, 116). Autres clés dans Automatism::applyConfigFromJson.
// Référence: GPIOMap::ALL_MAPPINGS (include/gpio_mapping.h).
void AutomatismSync::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
    if (doc.containsKey("mail")) {
        setEmailAddress(doc["mail"].as<const char*>());
    }

    if (doc.containsKey("mailNotif")) {
        setEmailEnabled(doc["mailNotif"].as<bool>() || doc["mailNotif"].as<int>() == 1);
    }

    if (doc.containsKey("FreqWakeUp")) {
        int val = parseIntFromVariant(doc["FreqWakeUp"]);
        if (val >= 0) setFreqWakeSec(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("limFlood")) {
        int val = parseIntFromVariant(doc["limFlood"]);
        if (val >= 0) setLimFlood(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("aqThreshold")) {
        int val = parseIntFromVariant(doc["aqThreshold"]);
        if (val > 0) setAqThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("tankThreshold")) {
        int val = parseIntFromVariant(doc["tankThreshold"]);
        if (val > 0) setTankThresholdCm(static_cast<uint16_t>(val));
    }

    if (doc.containsKey("chauffageThreshold")) {
        float val = parseFloatFromVariant(doc["chauffageThreshold"]);
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

    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    uint32_t sendStart = millis();
    bool success = AppTasks::netPostRaw(payloadBuffer, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS);
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

bool AutomatismSync::processFetchedRemoteConfig(ArduinoJson::JsonDocument& doc) {
    if (doc.size() == 0) return false;

    // v11.192: Ne pas itérer directement sur doc — quand il provient de loadFromNVSFallback,
    // l'état interne peut provoquer LoadProhibited (EXCVADDR 0x4). On sérialise puis désérialise
    // dans une copie pour itérer sur un document sain.
    char docJson[2048];
    size_t len = serializeJson(doc, docJson, sizeof(docJson));
    if (len == 0 || len >= sizeof(docJson)) return false;
    StaticJsonDocument<2048> inputCopy;
    DeserializationError err = deserializeJson(inputCopy, docJson);
    if (err) return false;

    // Harmonisation config: normaliser les clés (canoniques serveur distant) avant sauvegarde NVS
    StaticJsonDocument<2048> normalizedDoc;
    JsonObject out = normalizedDoc.to<JsonObject>();
    // v11.177: Normalisation complète de toutes les clés GPIO numériques vers clés canoniques
    for (ArduinoJson::JsonPair p : inputCopy.as<ArduinoJson::JsonObject>()) {
        const char* k = p.key().c_str();
        // GPIO 100-104: Email et seuils
        if (strcmp(k, "100") == 0) {
            if (!out.containsKey("mail")) out["mail"] = p.value();
        } else if (strcmp(k, "101") == 0) {
            if (!out.containsKey("mailNotif")) out["mailNotif"] = p.value();
        } else if (strcmp(k, "102") == 0) {
            if (!out.containsKey("aqThreshold")) out["aqThreshold"] = p.value();
        } else if (strcmp(k, "103") == 0) {
            if (!out.containsKey("tankThreshold")) out["tankThreshold"] = p.value();
        } else if (strcmp(k, "104") == 0) {
            if (!out.containsKey("chauffageThreshold")) out["chauffageThreshold"] = p.value();
        }
        // GPIO 105-107: Heures nourrissage
        else if (strcmp(k, "105") == 0) {
            if (!out.containsKey("bouffeMatin")) out["bouffeMatin"] = p.value();
        } else if (strcmp(k, "106") == 0) {
            if (!out.containsKey("bouffeMidi")) out["bouffeMidi"] = p.value();
        } else if (strcmp(k, "107") == 0) {
            if (!out.containsKey("bouffeSoir")) out["bouffeSoir"] = p.value();
        }
        // GPIO 111-116: Durées et config
        else if (strcmp(k, "111") == 0) {
            if (!out.containsKey("tempsGros")) out["tempsGros"] = p.value();
        } else if (strcmp(k, "112") == 0) {
            if (!out.containsKey("tempsPetits")) out["tempsPetits"] = p.value();
        } else if (strcmp(k, "113") == 0) {
            if (!out.containsKey("tempsRemplissageSec")) out["tempsRemplissageSec"] = p.value();
        } else if (strcmp(k, "114") == 0) {
            if (!out.containsKey("limFlood")) out["limFlood"] = p.value();
        } else if (strcmp(k, "115") == 0) {
            if (!out.containsKey("forceWakeUp")) out["forceWakeUp"] = p.value();
        } else if (strcmp(k, "116") == 0) {
            if (!out.containsKey("FreqWakeUp")) out["FreqWakeUp"] = p.value();
        }
        // Clés legacy textuelles à normaliser
        else if (strcmp(k, "heaterThreshold") == 0) {
            if (!out.containsKey("chauffageThreshold")) out["chauffageThreshold"] = p.value();
        } else if (strcmp(k, "refillDuration") == 0) {
            if (!out.containsKey("tempsRemplissageSec")) out["tempsRemplissageSec"] = p.value();
        } else {
            // Autres clés: copier telles quelles (déjà canoniques ou non mappées)
            out[k] = p.value();
        }
    }
    char jsonStr[DEFERRED_JSON_SIZE];
    serializeJson(normalizedDoc, jsonStr, sizeof(jsonStr));
    // v11.192: Différer la sauvegarde NVS vers automation task pour éviter assert
    // xTaskPriorityDisinherit (mutex/priorité) quand NVS est écrit depuis net task ou juste après notify.
    if (s_deferredSaveMutex && s_deferredSaveQueue &&
        xSemaphoreTake(s_deferredSaveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_deferredRemoteJson, jsonStr, DEFERRED_JSON_SIZE - 1);
        s_deferredRemoteJson[DEFERRED_JSON_SIZE - 1] = '\0';
        xSemaphoreGive(s_deferredSaveMutex);
        uint8_t one = 1;
        xQueueOverwrite(s_deferredSaveQueue, &one);
    }

    // v11.191: Ne pas appeler doc.clear() ni deserializeJson(doc) — ArduinoJson MemoryPoolList::clear()
    // peut crasher (LoadProhibited) quand doc provient de loadFromNVSFallback. La config est déjà
    // en file pour sauvegarde NVS ; le caller n'a pas besoin que doc soit mis à jour.
    if (!_configSyncedOnce) {
        Serial.printf("[Sync] Données normalisées en file pour NVS (%u clés)\n", normalizedDoc.size());
    }

    _serverOk = true;
    _recvState = 1;
    _lastRemoteFetch = millis();
    if (!_configSyncedOnce) {
        _configSyncedOnce = true;
        Serial.println(F("[Sync] configSynced=true (1er sync serveur réussi)"));
    }
    return true;
}

void AutomatismSync::processDeferredRemoteVarsSave() {
    if (!s_deferredSaveQueue || !s_deferredSaveMutex) return;
    uint8_t one = 0;
    if (xQueueReceive(s_deferredSaveQueue, &one, 0) != pdTRUE) return;
    char copy[DEFERRED_JSON_SIZE];
    if (xSemaphoreTake(s_deferredSaveMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    strncpy(copy, s_deferredRemoteJson, DEFERRED_JSON_SIZE - 1);
    copy[DEFERRED_JSON_SIZE - 1] = '\0';
    xSemaphoreGive(s_deferredSaveMutex);
    _config.saveRemoteVars(copy);
}

bool AutomatismSync::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    bool fromNVSFallback = false;
    // v11.195: RPC timeout plus long que HTTP (queue wait + GET) — évite "Timeout abandon" avant fin GET
    bool ok = AppTasks::netFetchRemoteState(doc, NetworkConfig::FETCH_REMOTE_STATE_RPC_TIMEOUT_MS, &fromNVSFallback);
    // #region agent log
    Serial.printf("[DBG] fetchRemoteState ok=%d fromNVS=%d hypothesis=H3,H4\n", ok ? 1 : 0, fromNVSFallback ? 1 : 0);
    // #endregion
    // v11.193: Données HTTP → copier dans doc depuis le caller (évite LoadProhibited en écrivant doc depuis netTask)
    if (ok && !fromNVSFallback) {
        bool copied = _web.copyLastFetchedTo(doc);
        size_t docSize = doc.size();
        // #region agent log
        Serial.printf("[DBG] fetchRemoteState copyLastFetchedTo=%d docSize=%u hypothesis=H4,H6\n", copied ? 1 : 0, (unsigned)docSize);
        // #endregion
        if (copied && doc.size() > 0) {
            processFetchedRemoteConfig(doc);
        }
    } else if (!ok) {
        _serverOk = false;
        _recvState = -1;
    }
    _lastRemoteFetch = millis();
    return ok;
}

bool AutomatismSync::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis) {
    // #region agent log
    if (!_config.isRemoteRecvEnabled()) {
        Serial.printf("[DBG] pollRemoteState skip recvDisabled hypothesis=H2,H5\n");
        return false;
    }
    if (currentMillis - _lastRemoteFetch < REMOTE_FETCH_INTERVAL_MS) return false;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[DBG] pollRemoteState skip wifi hypothesis=H5\n");
        return false;
    }
    Serial.printf("[DBG] pollRemoteState calling fetchRemoteState hypothesis=H5\n");
    // #endregion
    return fetchRemoteState(doc);
}

// Helpers simplifiés (backoff supprimé - géré par web_client retry)
bool AutomatismSync::canAttemptSend(uint32_t nowMs) const {
    (void)nowMs;  // Pas de backoff, toujours autoriser
    return true;
}

void AutomatismSync::registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter) {
    (void)payloadBytes;
    (void)heapBefore;
    (void)heapAfter;
    _lastPostDurationMs = durationMs;
    if (success) {
        _consecutiveSendFailures = 0;
        _postOkCount++;
    } else {
        _consecutiveSendFailures++;
        _postFailCount++;
    }
}

uint16_t AutomatismSync::replayQueuedData() {
    uint16_t sent = 0;
    while (_dataQueue.size() > 0 && sent < 2) {
        char payload[1024];
        if (_dataQueue.peek(payload, sizeof(payload))) {
            if (AppTasks::netPostRaw(payload, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS)) {
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
             "api_key=%s&sensor=%s&version=%s&ack_command=%s&ack_status=%s&ack_timestamp=%lu",
             ApiConfig::API_KEY, ProjectConfig::BOARD_TYPE, ProjectConfig::VERSION, command, status, millis());
    return AppTasks::netPostRaw(ackPayload, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS);
}

void AutomatismSync::logRemoteCommandExecution(const char* command, bool success) {
    // Logique simplifiée sans NVS pour l'instant
    Serial.printf("[Sync] Command '%s': %s\n", command, success ? "OK" : "FAILED");
}

