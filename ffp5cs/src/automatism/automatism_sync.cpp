#include "automatism/automatism_sync.h"
#include "automatism.h"
#include "config.h"
#include "ffp3_post_body.h"
#include "sensor_reading_fallback.h"
#include "gpio_parser.h"
#include "esp_task_wdt.h"
#include <WiFi.h>
#include "app_tasks.h"
#include "sd_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cmath>
#include <cstring>
#include <cstdlib>  // v11.183: atoi/atof pour valeurs serveur en string
#include <memory>   // C3: unique_ptr pour buffers de travail en heap (réduction pile)
#include <time.h>

namespace {
static char s_deferredRemoteJson[BufferConfig::REMOTE_JSON_CACHE_SIZE];
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

void AutomatismSync::update(const SensorReadings& readings, IActuators& acts, Automatism& core) {
    // Synchronisation périodique
    uint32_t now = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool sendEnabled = _config.isRemoteSendEnabled();
    bool recvEnabled = _config.isRemoteRecvEnabled();
    uint32_t timeSinceLastSend = now - _lastSend;
    bool intervalReached = (timeSinceLastSend > SEND_INTERVAL_MS);
    
    if (wifiConnected && sendEnabled) {
        if (AppTasks::isInWakeProtectionWindow()) {
            return;
        }
        // v12.33: Garde heap — différer POST si heap < 20 KB (évite allocation HTTP en état critique)
        constexpr uint32_t MIN_HEAP_FOR_POST = 20000;
        if (ESP.getFreeHeap() < MIN_HEAP_FOR_POST) {
            static unsigned long s_lastHeapSkipLog = 0;
            if (now - s_lastHeapSkipLog >= 60000) {
                Serial.printf("[Sync] ⏸️ POST différé: heap faible (%u < %u)\n",
                              (unsigned)ESP.getFreeHeap(), (unsigned)MIN_HEAP_FOR_POST);
                s_lastHeapSkipLog = now;
            }
        } else {
        // Throttle : différer POST quand tous les slots POST sont occupés (v12.40: seuil dédié)
        size_t poolUsed = AppTasks::netRequestPoolUsedCount();
        size_t threshold = AppTasks::netRequestPoolPostSlotsFullThreshold();
        if (poolUsed >= threshold) {
            // Log au plus une fois par minute pour limiter le spam
            static unsigned long s_lastThrottleLog = 0;
            if (now - s_lastThrottleLog >= 60000) {
                Serial.printf("[Sync] ⏸️ POST différé: pool netRPC plein (%u >= %u)\n", (unsigned)poolUsed, (unsigned)threshold);
                s_lastThrottleLog = now;
            }
        } else {
            // Drainer la file des POSTs en attente dès qu'un slot netRPC est dispo (même si sendFullUpdate échoue ou n'a pas lieu)
            if (_dataQueue.size() > 0) {
                replayQueuedData();
            }
            if (intervalReached) {
                Serial.printf("[Sync] Déclenchement envoi mesures (dernier POST il y a %lu ms ; résultat HTTP dans [postSender]/[HTTP])\n",
                              static_cast<unsigned long>(timeSinceLastSend));
                sendFullUpdate(readings, acts, core);
            } else {
                TideEventType tideEvent = checkInflectionPoint(readings.wlAqua, now);
                if (tideEvent != TideEventType::None) {
                    Serial.printf("[Sync] 📈 POST inflexion marée (event=%s, extreme=%u, wlAqua=%u, trend=%d)\n",
                                  toTideEventString(tideEvent), _lastInflectionWlAqua, readings.wlAqua, _trendDir);
                    if (sendFullUpdate(readings, acts, core, nullptr, AppTasks::PostCategory::EventAck, tideEvent)) {
                    _lastSend = now;
                    }
                }
            }
        }
        }  // fin garde heap
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
            bool resetOk = core.sendFullUpdate(readings, "bouffePetits=0&108=0", AppTasks::PostCategory::EventAck);
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
            bool resetOk = core.sendFullUpdate(readings, "bouffeGros=0&109=0", AppTasks::PostCategory::EventAck);
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

// Charge le cache NVS remote_vars dans doc (fallback offline quand GET HTTP indisponible).
static bool loadRemoteVarsIntoDoc(ConfigManager& cfg, ArduinoJson::JsonDocument& doc) {
    char cachedJson[BufferConfig::REMOTE_JSON_CACHE_SIZE];
    if (!cfg.loadRemoteVars(cachedJson, sizeof(cachedJson)) || cachedJson[0] == '\0') {
        return false;
    }
    doc.clear();
    DeserializationError err = deserializeJson(doc, cachedJson);
    if (err || doc.size() == 0) {
        if (LogConfig::SERIAL_ENABLED) {
            Serial.printf("[Sync] NVS remote_vars invalide (%s)\n", err ? err.c_str() : "vide");
        }
        return false;
    }
    Serial.printf("[Sync] Config NVS chargée (%u clés) pour application RAM\n", (unsigned)doc.size());
    return true;
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
                                    IActuators& acts,
                                    Automatism& core,
                                    const char* extraPairs,
                                    AppTasks::PostCategory category,
                                    TideEventType tideEvent) {
    uint32_t attemptStartMs = millis();
    if (!canAttemptSend(attemptStartMs)) {
        return false;
    }
    _lastSendAttemptMs = attemptStartMs;

    uint32_t heapBefore = ESP.getFreeHeap();
    if (heapBefore < BufferConfig::LOW_MEMORY_THRESHOLD_BYTES) {
        _lastDataSkipReason = SKIP_LOW_MEMORY;
        Serial.printf("[Sync] Mémoire faible (%u bytes), report envoi\n", heapBefore);
        return false;
    }

    char payloadBuffer[BufferConfig::POST_PAYLOAD_MAX_SIZE];
    Ffp3PostBody::FullUpdateValues body{};
    int diffMaree = SensorValidation::isWaterLevelKnown(readings.wlAqua)
                      ? core.computeDiffMaree(readings.wlAqua)
                      : 0;
    float pressureForPost = isnan(readings.pressureHpa) ? 0.0f : readings.pressureHpa;

    snprintf(body.apiKey, sizeof(body.apiKey), "%s", ApiConfig::API_KEY);
    snprintf(body.sensor, sizeof(body.sensor), "%s", ProjectConfig::BOARD_TYPE);
    snprintf(body.version, sizeof(body.version), "%s", ProjectConfig::VERSION);
    snprintf(body.tempAir, sizeof(body.tempAir), "%.1f", readings.tempAir);
    snprintf(body.humidite, sizeof(body.humidite), "%.1f", readings.humidity);
    snprintf(body.pression, sizeof(body.pression), "%.1f", pressureForPost);
    snprintf(body.tempEau, sizeof(body.tempEau), "%.1f", readings.tempWater);
    SensorReadingFallback::formatWaterLevelPost(body.eauPotager, sizeof(body.eauPotager), readings.wlPota);
    SensorReadingFallback::formatWaterLevelPost(body.eauAquarium, sizeof(body.eauAquarium), readings.wlAqua);
    SensorReadingFallback::formatWaterLevelPost(body.eauReserve, sizeof(body.eauReserve), readings.wlTank);
    snprintf(body.diffMaree, sizeof(body.diffMaree), "%d", diffMaree);
    snprintf(body.luminosite, sizeof(body.luminosite), "%u", readings.luminosite);
    snprintf(body.etatPompeAqua, sizeof(body.etatPompeAqua), "%d", acts.isAquaPumpRunning() ? 1 : 0);
    snprintf(body.etatPompeTank, sizeof(body.etatPompeTank), "%d", acts.isTankPumpRunning() ? 1 : 0);
    snprintf(body.etatHeat, sizeof(body.etatHeat), "%d", acts.isHeaterOn() ? 1 : 0);
    snprintf(body.etatUV, sizeof(body.etatUV), "%d", acts.isLightOn() ? 1 : 0);
    snprintf(body.bouffeMatin, sizeof(body.bouffeMatin), "%u", core.getBouffeMatin());
    snprintf(body.bouffeMidi, sizeof(body.bouffeMidi), "%u", core.getBouffeMidi());
    snprintf(body.bouffeSoir, sizeof(body.bouffeSoir), "%u", core.getBouffeSoir());
    snprintf(body.tempsGros, sizeof(body.tempsGros), "%u", core.getTempsGros());
    snprintf(body.tempsPetits, sizeof(body.tempsPetits), "%u", core.getTempsPetits());
    snprintf(body.aqThreshold, sizeof(body.aqThreshold), "%u", _aqThresholdCm);
    snprintf(body.tankThreshold, sizeof(body.tankThreshold), "%u", _tankThresholdCm);
    snprintf(body.chauffageThreshold, sizeof(body.chauffageThreshold), "%.1f", _heaterThresholdC);
    snprintf(body.tempsRemplissageSec, sizeof(body.tempsRemplissageSec), "%u", core.getRefillDurationSec());
    snprintf(body.limFlood, sizeof(body.limFlood), "%u", _limFlood);
    snprintf(body.wakeUp, sizeof(body.wakeUp), "%d", core.getForceWakeUp() ? 1 : 0);
    snprintf(body.freqWakeUp, sizeof(body.freqWakeUp), "%u", _freqWakeSec);
    snprintf(body.bouffePetits, sizeof(body.bouffePetits), "%s", core.getBouffePetitsFlag());
    snprintf(body.bouffeGros, sizeof(body.bouffeGros), "%s", core.getBouffeGrosFlag());
    snprintf(body.mail, sizeof(body.mail), "%s", _emailAddress);
    snprintf(body.mailNotif, sizeof(body.mailNotif), "%s", _emailEnabled ? "checked" : "");
    snprintf(body.resetMode, sizeof(body.resetMode), "0");
    snprintf(body.tideEvent, sizeof(body.tideEvent), "%s", toTideEventString(tideEvent));
    snprintf(body.tideTrend, sizeof(body.tideTrend), "%d", static_cast<int>(_trendDir));
    snprintf(body.tideNoiseMm, sizeof(body.tideNoiseMm), "%u", static_cast<unsigned>(INFLECTION_NOISE_MM));
    snprintf(body.tideWindowMs, sizeof(body.tideWindowMs), "%lu",
             static_cast<unsigned long>(core.getTideWindowMs()));
    snprintf(body.tideExtremeMm, sizeof(body.tideExtremeMm), "%u",
             static_cast<unsigned>(_lastInflectionWlAqua));
    snprintf(body.configSynced, sizeof(body.configSynced), "%d", _configSyncedOnce ? 1 : 0);

    if (!_configSyncedOnce) {
        Serial.println(F("[Sync] ⚠️ configSynced=0 - variables config ignorées par serveur"));
    }

    time_t nowEpoch;
    time(&nowEpoch);
    uint32_t epochSec = (nowEpoch > 1700000000) ? (uint32_t)nowEpoch : (uint32_t)(millis() / 1000);
    {
        static uint32_t s_postSeq = 0;
        snprintf(body.postId, sizeof(body.postId), "%s-%lu-%lu",
                 ProjectConfig::BOARD_TYPE, (unsigned long)epochSec, (unsigned long)(++s_postSeq));
    }

    Ffp3PostBody::applyExtraPairs(body, extraPairs);

    int len = Ffp3PostBody::buildFullUpdateBody(payloadBuffer, sizeof(payloadBuffer), body);
    if (len < 0 || len >= (int)sizeof(payloadBuffer)) {
        Serial.println(F("[Sync] Erreur formatage payload canonique"));
        return false;
    }

#if defined(BOARD_S3)
    SdLogger::QueueResult sdQ = SdLogger::logAndQueue(payloadBuffer, epochSec);
    const uint32_t sdSeqNum = sdQ.ok ? sdQ.seqNum : 0;
#else
    const uint32_t sdSeqNum = 0;
#endif

    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    uint32_t sendStart = millis();
    AppTasks::NetPostFailureReason failReason = AppTasks::NetPostFailureReason::None;
    // fire-and-forget : retourne immédiatement (true = mis en queue, false = pool plein)
    bool queued = AppTasks::netPostRaw(payloadBuffer, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS, category, &failReason, sdSeqNum);
    uint32_t durationMs = millis() - sendStart;  // ~0 ms désormais

    registerSendResult(queued, strlen(payloadBuffer), durationMs, heapBefore, ESP.getFreeHeap());

    if (queued) {
        // POST transmis à netTask — résultat HTTP traité en arrière-plan
        // En cas d'échec HTTP, web_client queue en NVS automatiquement
        static bool s_loggedAsyncPostHint = false;
        if (LogConfig::SERIAL_ENABLED && !s_loggedAsyncPostHint) {
            s_loggedAsyncPostHint = true;
            Serial.println(F("[Sync] Note: « mis en file » ≠ succès HTTP — le verdict réel est dans [postSender] puis [HTTP] Verdict."));
        }
        _sendState = 1;
        _lastSend = millis();
        _lastDataSkipReason = SKIP_NONE;
        // S3: suppression file SD par postSenderTask après succès HTTP réel.
        if (_dataQueue.size() > 0) replayQueuedData();
    } else {
        // Pool netRPC plein : conserver en RAM pour retry au prochain cycle
        _sendState = -1;
        _lastDataSkipReason = SKIP_NET_FAIL;
        Serial.println(F("[Sync] POST différé (pool netRPC plein)"));
        _dataQueue.push(payloadBuffer);
    }

    return queued;
}

bool AutomatismSync::processFetchedRemoteConfig(ArduinoJson::JsonDocument& doc) {
    if (doc.size() == 0) return false;

    // C3 (profondeur de pile) : ces buffers de travail (~6 Ko : docJson + 2 JsonDocument +
    // jsonStr) étaient sur la pile, ce qui poussait autoTask près du débordement (HWM ~95%).
    // On les déplace en HEAP local, alloués/libérés à chaque appel. PAS en static : la fonction
    // est multi-tâches (netTask via app_tasks_net.cpp + autoTask via fetchRemoteState) donc un
    // buffer partagé créerait une course (pire que la pile économisée — cf. BACKLOG §3/§4).
    // unique_ptr garantit la libération sur tous les chemins de retour.
    struct ProcessBuffers {
        char docJson[2048];
        StaticJsonDocument<2048> inputCopy;
        StaticJsonDocument<2048> normalizedDoc;
        char jsonStr[BufferConfig::REMOTE_JSON_CACHE_SIZE];
    };
    std::unique_ptr<ProcessBuffers> buf(new (std::nothrow) ProcessBuffers());
    if (!buf) {
        Serial.println(F("[Sync] processFetchedRemoteConfig: heap insuffisant pour buffers"));
        return false;
    }

    // v11.192: Ne pas itérer directement sur doc — quand il provient de loadFromNVSFallback,
    // l'état interne peut provoquer LoadProhibited (EXCVADDR 0x4). On sérialise puis désérialise
    // dans une copie pour itérer sur un document sain.
    size_t len = serializeJson(doc, buf->docJson, sizeof(buf->docJson));
    if (len == 0 || len >= sizeof(buf->docJson)) return false;
    DeserializationError err = deserializeJson(buf->inputCopy, buf->docJson);
    if (err) return false;

    // Harmonisation config: normaliser les clés (canoniques serveur distant) avant sauvegarde NVS
    JsonObject out = buf->normalizedDoc.to<JsonObject>();
    // v11.177: Normalisation complète de toutes les clés GPIO numériques vers clés canoniques
    for (ArduinoJson::JsonPair p : buf->inputCopy.as<ArduinoJson::JsonObject>()) {
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
    serializeJson(buf->normalizedDoc, buf->jsonStr, sizeof(buf->jsonStr));
    // v11.192: Différer la sauvegarde NVS vers automation task pour éviter assert
    // xTaskPriorityDisinherit (mutex/priorité) quand NVS est écrit depuis net task ou juste après notify.
    if (s_deferredSaveMutex && s_deferredSaveQueue &&
        xSemaphoreTake(s_deferredSaveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_deferredRemoteJson, buf->jsonStr, BufferConfig::REMOTE_JSON_CACHE_SIZE - 1);
        s_deferredRemoteJson[BufferConfig::REMOTE_JSON_CACHE_SIZE - 1] = '\0';
        xSemaphoreGive(s_deferredSaveMutex);
        uint8_t one = 1;
        xQueueOverwrite(s_deferredSaveQueue, &one);
    }

    // v11.191: Ne pas appeler doc.clear() ni deserializeJson(doc) — ArduinoJson MemoryPoolList::clear()
    // peut crasher (LoadProhibited) quand doc provient de loadFromNVSFallback. La config est déjà
    // en file pour sauvegarde NVS ; le caller n'a pas besoin que doc soit mis à jour.
    if (!_configSyncedOnce) {
        Serial.printf("[Sync] Données normalisées en file pour NVS (%u clés)\n", buf->normalizedDoc.size());
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
    char copy[BufferConfig::REMOTE_JSON_CACHE_SIZE];
    if (xSemaphoreTake(s_deferredSaveMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
        strncpy(copy, s_deferredRemoteJson, BufferConfig::REMOTE_JSON_CACHE_SIZE - 1);
        copy[BufferConfig::REMOTE_JSON_CACHE_SIZE - 1] = '\0';
    xSemaphoreGive(s_deferredSaveMutex);
    _config.saveRemoteVars(copy);
}

bool AutomatismSync::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    bool fromNVSFallback = false;
    // v11.195: RPC timeout plus long que HTTP (queue wait + GET) — évite "Timeout abandon" avant fin GET
    bool ok = AppTasks::netFetchRemoteState(doc, NetworkConfig::FETCH_REMOTE_STATE_RPC_TIMEOUT_MS, &fromNVSFallback);
    bool hasConfig = false;

    if (ok && !fromNVSFallback) {
        bool copied = _web.copyLastFetchedTo(doc);
        if (copied && doc.size() > 0) {
            processFetchedRemoteConfig(doc);
            hasConfig = true;
        } else {
            Serial.println(F("[Sync] GET OK mais JSON vide — tentative cache NVS"));
            hasConfig = loadRemoteVarsIntoDoc(_config, doc);
        }
    } else if (ok && fromNVSFallback) {
        hasConfig = loadRemoteVarsIntoDoc(_config, doc);
        if (hasConfig) {
            Serial.println(F("[Sync] Fallback NVS (GET HTTP indisponible)"));
        }
    } else {
        _serverOk = false;
        _recvState = -1;
        hasConfig = loadRemoteVarsIntoDoc(_config, doc);
        if (hasConfig) {
            Serial.println(F("[Sync] GET échoué — config RAM depuis cache NVS"));
        }
    }

    _lastRemoteFetch = millis();
    if (hasConfig) {
        _serverOk = true;
        _recvState = 1;
    }
    return hasConfig;
}

bool AutomatismSync::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis) {
    if (!_config.isRemoteRecvEnabled()) {
        return false;
    }
    if (currentMillis - _lastRemoteFetch < REMOTE_FETCH_INTERVAL_MS) return false;
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    if (AppTasks::shouldDeferRemoteStateFetch()) {
        return false;
    }
    return fetchRemoteState(doc);
}

AutomatismSync::TideEventType AutomatismSync::checkInflectionPoint(uint16_t wlAqua, uint32_t nowMs) {
    if (wlAqua == 0) return TideEventType::None;

    if (_trendDir == 0) {
        if (_extremeWlAqua == 0) {
            _extremeWlAqua = wlAqua;
            return TideEventType::None;
        }
        int16_t diff = (int16_t)wlAqua - (int16_t)_extremeWlAqua;
        if (diff >= (int16_t)INFLECTION_NOISE_MM) {
            _trendDir = 1;
            _extremeWlAqua = wlAqua;
        } else if (diff <= -(int16_t)INFLECTION_NOISE_MM) {
            _trendDir = -1;
            _extremeWlAqua = wlAqua;
        }
        return TideEventType::None;
    }

    if (_trendDir == 1) {
        if (wlAqua >= _extremeWlAqua) {
            _extremeWlAqua = wlAqua;
        } else if ((_extremeWlAqua - wlAqua) >= INFLECTION_NOISE_MM) {
            uint16_t confirmedPeak = _extremeWlAqua;
            _trendDir = -1;
            _extremeWlAqua = wlAqua;
            if ((nowMs - _lastInflectionPostMs) >= MIN_INFLECTION_INTERVAL_MS) {
                _lastInflectionPostMs = nowMs;
                _lastInflectionWlAqua = confirmedPeak;
                return TideEventType::Peak;
            }
        }
    } else {
        if (wlAqua <= _extremeWlAqua) {
            _extremeWlAqua = wlAqua;
        } else if ((wlAqua - _extremeWlAqua) >= INFLECTION_NOISE_MM) {
            uint16_t confirmedTrough = _extremeWlAqua;
            _trendDir = 1;
            _extremeWlAqua = wlAqua;
            if ((nowMs - _lastInflectionPostMs) >= MIN_INFLECTION_INTERVAL_MS) {
                _lastInflectionPostMs = nowMs;
                _lastInflectionWlAqua = confirmedTrough;
                return TideEventType::Trough;
            }
        }
    }

    return TideEventType::None;
}

const char* AutomatismSync::toTideEventString(TideEventType eventType) {
    switch (eventType) {
        case TideEventType::Peak:
            return "peak";
        case TideEventType::Trough:
            return "trough";
        case TideEventType::None:
        default:
            return "none";
    }
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
            if (AppTasks::netPostRaw(payload, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS, AppTasks::PostCategory::Replay)) {
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
    return AppTasks::netPostRaw(ackPayload, NetworkConfig::HTTP_POST_RPC_TIMEOUT_MS, AppTasks::PostCategory::EventAck);
}

void AutomatismSync::logRemoteCommandExecution(const char* command, bool success) {
    // Logique simplifiée sans NVS pour l'instant
    Serial.printf("[Sync] Command '%s': %s\n", command, success ? "OK" : "FAILED");
}

