#include "automatism/automatism_sleep.h"
#include "n3_sleep_decision.h"  // décisions de sommeil pures (mutualisé shared/n3_common)
#include "automatism/sleep_blocking.h"  // C4: décision de blocage de la veille (pure, testée)
#include "automatism.h"  // Pour accès aux méthodes de Automatism
#include "app_tasks.h"
#include "config.h"
#include "realtime_websocket.h"
#include "esp_task_wdt.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ctime>
#include <algorithm>

// ============================================================================
// Module: AutomatismSleep
// Responsabilité: Gestion sommeil adaptatif et marées
// ============================================================================

AutomatismSleep::AutomatismSleep(PowerManager& power, ConfigManager& cfg)
    : _power(power)
    , _config(cfg)
    , _lastWakeMs(0)
    , _lastActivityMs(0)
    , _lastSensorActivityMs(0)
    , _lastWebActivityMs(0)
    , _forceWakeFromWeb(false)
    , _forceWakeUp(false)
    , _hasRecentErrors(false)
    , _consecutiveWakeupFailures(0)
    , _tideTriggerCm(0)
    , _lastCanSleep(false)
    , _lastShouldSleep(false)
{
    // Configuration sleep adaptatif (valeurs par défaut de config.h)
    _sleepConfig.minSleepTime = ::SleepConfig::MIN_INACTIVITY_DELAY_SEC;
    _sleepConfig.maxSleepTime = ::SleepConfig::MAX_INACTIVITY_DELAY_SEC;
    _sleepConfig.normalSleepTime = ::SleepConfig::NORMAL_INACTIVITY_DELAY_SEC;
    _sleepConfig.errorSleepTime = ::SleepConfig::ERROR_INACTIVITY_DELAY_SEC;
    _sleepConfig.nightSleepTime = ::SleepConfig::NIGHT_INACTIVITY_DELAY_SEC;
    _sleepConfig.adaptiveSleep = ::SleepConfig::ADAPTIVE_SLEEP_ENABLED;
    
    _lastWakeMs = millis();
    Serial.println(F("[Sleep] Module initialisé - Sleep adaptatif activé"));
}

void AutomatismSleep::begin() {
    _lastWakeMs = millis();
    _lastActivityMs = millis();
    // Restauration état éventuel si nécessaire
}

// ============================================================================
// ACTIVITÉ
// ============================================================================

// v11.178: hasSignificantActivity() supprimé (toujours false - audit dead-code)
// Seuls nourrissage/remplissage retardent le sleep, gérés explicitement dans handleAutoSleep()

void AutomatismSleep::updateActivityTimestamp() {
    unsigned long currentMillis = millis();
    _lastActivityMs = currentMillis;
    _lastWakeMs = currentMillis;
    Serial.println(F("[Sleep] Timestamp activité mis à jour"));
}

void AutomatismSleep::notifyLocalWebActivity() {
    _lastWebActivityMs = millis();
    _forceWakeFromWeb = true;
    
    // NOTE: On n'active PLUS forceWakeUp automatiquement
    // On empêche juste le sleep temporairement pendant la consultation
    // forceWakeUp (GPIO 115) doit être contrôlé explicitement par l'utilisateur
    Serial.println(F("[Sleep] Activité web détectée - sleep bloqué temporairement"));
}

// ============================================================================
// CALCULS ADAPTATIFS
// ============================================================================

uint32_t AutomatismSleep::calculateAdaptiveSleepDelay() {
    // Décision pure extraite dans sleep_decision.h (testée nativement, audit §3.8).
    uint32_t delay = SleepDecision::adaptiveSleepDelay(
        _sleepConfig.adaptiveSleep,
        _sleepConfig.normalSleepTime, _sleepConfig.errorSleepTime,
        _sleepConfig.nightSleepTime, _sleepConfig.minSleepTime, _sleepConfig.maxSleepTime,
        hasRecentErrors(), isNightTime(), _consecutiveWakeupFailures);
    if (_sleepConfig.adaptiveSleep && _consecutiveWakeupFailures > 0) {
        Serial.printf("[Sleep] Délai ajusté (échecs: %d)\n", _consecutiveWakeupFailures);
    }
    return delay;
}

bool AutomatismSleep::isNightTime() {
    time_t currentTime = _power.getCurrentEpochSafe();
    struct tm timeinfo;

    // v11.179: Utiliser localtime_r() thread-safe au lieu de localtime()
    if (localtime_r(&currentTime, &timeinfo) != nullptr) {
        // Fenêtre nuit extraite dans sleep_decision.h (testée nativement).
        return SleepDecision::isNightHour(timeinfo.tm_hour);
    }

    return false;  // Fallback sûr si conversion échoue
}

bool AutomatismSleep::hasRecentErrors() {
    return _hasRecentErrors;
}

// ============================================================================
// SLEEP CONDITIONS
// ============================================================================

namespace {
inline bool isStillPending(uint32_t targetMs, uint32_t nowMs) {
    return targetMs != 0 && static_cast<int32_t>(targetMs - nowMs) > 0;
}
}

bool AutomatismSleep::shouldEnterSleepEarly(const SensorReadings& r,
                                            bool forceWakeUp,
                                            bool forceWakeFromWeb,
                                            unsigned long lastWebActivityMs,
                                            bool feedingInProgress,
                                            bool tankPumpRunning,
                                            uint32_t countdownEnd,
                                            unsigned long lastWakeMs,
                                            int diffMaree10s,
                                            int16_t tideTriggerCm) {
    // Synchroniser l'état interne
    _forceWakeUp = forceWakeUp;
    _forceWakeFromWeb = forceWakeFromWeb;
    _lastWebActivityMs = lastWebActivityMs;
    _lastWakeMs = lastWakeMs;
    _tideTriggerCm = tideTriggerCm;

    // Conditions bloquantes immédiates
    if (_forceWakeUp) return false;
    if (_forceWakeFromWeb) return false;
    if (tankPumpRunning) return false;
    if (feedingInProgress) return false;

    // Countdown long en cours (remplissage)
    unsigned long nowMs = millis();
    if (isStillPending(countdownEnd, nowMs)) {
        uint32_t remainingCountdownMs = static_cast<uint32_t>(countdownEnd - nowMs);
        uint32_t remainingCountdownSec = remainingCountdownMs / 1000UL;
        if (remainingCountdownSec > 300) { // Plus de 5 minutes
            return false;
        }
    }

    // Délai adaptatif minimal
    uint32_t adaptiveDelaySec = calculateAdaptiveSleepDelay();
    bool delayReached = (nowMs - lastWakeMs) >= (adaptiveDelaySec * 1000UL);

    const int tideTriggerMm = static_cast<int>(SensorConfig::Ultrasonic::cmToMm(static_cast<uint16_t>(tideTriggerCm)));
    bool tideAscendingTrigger = (diffMaree10s > tideTriggerMm);

    if (tideAscendingTrigger || delayReached) {
        unsigned long awakeSec = (nowMs - lastWakeMs) / 1000UL;
        if (tideAscendingTrigger) {
            Serial.printf("[Auto] Sleep précoce déclenché: marée montante (~10s, +%d mm)\n",
                          diffMaree10s);
        } else {
            Serial.printf("[Auto] Sleep précoce déclenché: délai atteint (%lu s)\n", awakeSec);
        }
        return true;
    }

    return false;
}

bool AutomatismSleep::handleBlockingConditions(IActuators& acts,
                                               bool& forceWakeUp,
                                               bool& forceWakeFromWeb,
                                               unsigned long& lastWebActivityMs,
                                               uint32_t countdownEnd,
                                               unsigned long& lastWakeMs,
                                               bool feedingInProgress,
                                               bool tankPumpRunning,
                                               uint8_t wsClients) {
    (void)acts;  // non utilisé (déjà le cas dans l'ancien corps) — signature conservée pour l'appelant
    const unsigned long now = millis();

    // C4: la DÉCISION (faut-il bloquer la veille et pourquoi) est extraite dans
    // SleepBlocking::decide (pure, testée nativement). Le membre _wsBlockStartMs (seul
    // état influençant la décision) est passé via State ; les logs Serial, l'anti-spam
    // (throttle) et les mutations de membres/params restent ici, pilotés par le Result
    // (parité ligne à ligne avec l'ancien corps).
    SleepBlocking::State st;
    st.wsBlockStartMs = static_cast<uint32_t>(_wsBlockStartMs);

    SleepBlocking::Inputs in;
    in.wsClients = wsClients;
    in.forceWakeFromWeb = forceWakeFromWeb;
    in.lastWebActivityMs = static_cast<uint32_t>(lastWebActivityMs);
    in.forceWakeUp = forceWakeUp;
    in.tankPumpRunning = tankPumpRunning;
    in.feedingInProgress = feedingInProgress;
    in.countdownEnd = countdownEnd;
    in.nowMs = static_cast<uint32_t>(now);
    in.webActivityTimeoutMs = TimingConfig::WEB_ACTIVITY_TIMEOUT_MS;

    const SleepBlocking::Result res = SleepBlocking::decide(st, in);
    _wsBlockStartMs = st.wsBlockStartMs;  // appliquer l'état mis à jour (cycle de vie blocage WS)

    // Transitions « fall-through » non-throttlées (ordre historique préservé).
    if (res.wsTimedOut) {
        Serial.printf("[Auto] ⚠️ TIMEOUT WebSocket atteint (%u ms) - Forcer sleep malgré %u clients\n",
                      SleepBlocking::WS_BLOCK_TIMEOUT_MS, wsClients);
    }
    if (res.webExpired) {
        forceWakeFromWeb = false;
        Serial.println(F("[Auto] Activité web expirée - sleep autorisé à nouveau"));
    }
    if (res.refreshLastWake) {
        lastWakeMs = now;
    }

    // Logs terminaux + throttle (anti-spam) géré ici, comme avant l'extraction.
    switch (res.reason) {
        case SleepBlocking::Reason::WsClients:
            if (now - _lastWsLogMs > SleepBlocking::LOG_THROTTLE_MS) {
                _lastWsLogMs = now;
                Serial.printf("[Auto] Auto-sleep bloqué (%u clients WebSocket, %u ms écoulées)\n",
                              wsClients, res.wsElapsedMs);
            }
            return false;
        case SleepBlocking::Reason::WebActivity:
            if (now - _lastWebLogMs > SleepBlocking::LOG_THROTTLE_MS) {
                _lastWebLogMs = now;
                Serial.println(F("[Auto] Auto-sleep bloqué temporairement (activité web récente)"));
            }
            return false;
        case SleepBlocking::Reason::ForceWakeUp:
            if (now - _lastForceWakeLogMs > SleepBlocking::LOG_THROTTLE_MS) {
                _lastForceWakeLogMs = now;
                Serial.println(F("[Auto] Auto-sleep désactivé (forceWakeUp GPIO 115 = true)"));
            }
            return false;
        case SleepBlocking::Reason::TankPump:
            Serial.println(F("[Auto] Auto-sleep différé - pompe de remplissage active"));
            return false;
        case SleepBlocking::Reason::Feeding:
            Serial.println(F("[Auto] Auto-sleep différé - nourrissage en cours"));
            return false;
        case SleepBlocking::Reason::CountdownLong:
            Serial.printf("[Auto] Auto-sleep différé - décompte long en cours (%u s restants)\n",
                          res.remainingCountdownSec);
            return false;
        case SleepBlocking::Reason::CountdownShort:
            Serial.printf("[Auto] Décompte court en cours (%u s restants) - chronomètre préservé\n",
                          res.remainingCountdownSec);
            return false;
        case SleepBlocking::Reason::Allow:
            break;
    }

    // v11.178: Bloc hasSignificantActivity() supprimé (toujours false - audit dead-code)
    return true;
}

// ============================================================================
// MÉTHODE PRINCIPALE : handleAutoSleep
// ============================================================================

bool AutomatismSleep::handleAutoSleep(const SensorReadings& r, IActuators& acts, Automatism& core) {
    // Récupération des informations via accesseurs publics
    bool forceWakeUp = core.getForceWakeUp();
    bool tankPumpRunning = core.isTankPumpRunning();
    
    bool feedingInProgress = core.isFeedingInProgress();
    uint32_t countdownEnd = core.getCountdownEndMs();
    int diffMaree10s = core.computeDiffMaree(r.wlAqua);
    int16_t tideTriggerCm = core.getTideTriggerCm();
    
    // Récupération du nombre de clients WebSocket
    uint8_t wsClients = g_realtimeWebSocket.getConnectedClients();
    
    // Variables modifiables par handleBlockingConditions
    bool localForceWakeUp = forceWakeUp;
    bool localForceWakeFromWeb = _forceWakeFromWeb;
    unsigned long localLastWebActivityMs = _lastWebActivityMs;
    unsigned long localLastWakeMs = _lastWakeMs;
    
    // 1. Vérifier les conditions bloquantes
    bool canSleep = handleBlockingConditions(acts,
                                             localForceWakeUp,
                                             localForceWakeFromWeb,
                                             localLastWebActivityMs,
                                             countdownEnd,
                                             localLastWakeMs,
                                             feedingInProgress,
                                             tankPumpRunning,
                                             wsClients);
    
    if (!canSleep) {
        // Conditions bloquantes détectées, ne pas entrer en veille
        return false;
    }
    
    // 2. Évaluer si le système doit entrer en veille
    bool shouldSleep = shouldEnterSleepEarly(r,
                                             localForceWakeUp,
                                             localForceWakeFromWeb,
                                             localLastWebActivityMs,
                                             feedingInProgress,
                                             tankPumpRunning,
                                             countdownEnd,
                                             localLastWakeMs,
                                             diffMaree10s,
                                             tideTriggerCm);

    uint32_t sleepDurationSec = core.getFreqWakeSec();
    if (sleepDurationSec == 0) {
        #if defined(PROFILE_TEST)
            sleepDurationSec = 6;  // 6s par défaut pour wroom-test
        #else
            sleepDurationSec = 600; // 600s par défaut pour production
        #endif
    }
    if (SleepConfig::LOCAL_SLEEP_DURATION_CONTROL && isNightTime()) {
        sleepDurationSec = static_cast<uint32_t>(sleepDurationSec) *
                           static_cast<uint32_t>(SleepConfig::NIGHT_SLEEP_MULTIPLIER);
    }

    const unsigned long nowMs = millis();
    const uint32_t adaptiveDelaySec = calculateAdaptiveSleepDelay();
    const bool delayReached = (nowMs - localLastWakeMs) >= (adaptiveDelaySec * 1000UL);
    const int tideTriggerMm = static_cast<int>(SensorConfig::Ultrasonic::cmToMm(static_cast<uint16_t>(tideTriggerCm)));
    const bool tideAscending = (diffMaree10s > tideTriggerMm);
    const bool decisionChanged = (canSleep != _lastCanSleep) || (shouldSleep != _lastShouldSleep);
    if (decisionChanged || (nowMs - _lastDecisionLogMs > 30000)) {
        _lastDecisionLogMs = nowMs;
        _lastCanSleep = canSleep;
        _lastShouldSleep = shouldSleep;
        Serial.printf("[Auto] Sleep decision: can=%d should=%d delay=%d tide=%d diff_mm=%d trig_mm=%d ws=%u feed=%d pump=%d cd=%lu wake=%lu s sleep=%u s\n",
                      canSleep ? 1 : 0,
                      shouldSleep ? 1 : 0,
                      delayReached ? 1 : 0,
                      tideAscending ? 1 : 0,
                      diffMaree10s,
                      tideTriggerMm,
                      wsClients,
                      feedingInProgress ? 1 : 0,
                      tankPumpRunning ? 1 : 0,
                      static_cast<unsigned long>(countdownEnd),
                      static_cast<unsigned long>((nowMs - localLastWakeMs) / 1000UL),
                      sleepDurationSec);
    }
    
    if (!shouldSleep) {
        // Conditions pour dormir non remplies
        return false;
    }
    
    // 3. Entrer en veille
    Serial.println(F("[Auto] Conditions remplies - Entrée en veille"));

    // Envoi des données vers le serveur distant avant veille (si WiFi + envoi activé)
    if (WiFi.status() == WL_CONNECTED && core.isRemoteSendEnabled()) {
        core.sendFullUpdate(r, nullptr);
    }

    // Snapshot aqua / chauffage / lumière avant veille, puis coupure pour la veille
    core.prepareActuatorsForSleep(acts);

    // Envoi du mail de mise en veille (si notifications activées)
    if (core.isEmailEnabled()) {
        const char* reason = tideAscending ? "Marée montante détectée" : "Délai d'inactivité atteint";
        core.sendSleepMail(reason, sleepDurationSec, r);
    }

    if (!AppTasks::quiesceHttpBeforeLightSleep(NetworkConfig::LIGHT_SLEEP_HTTP_QUIESCE_TIMEOUT_MS)) {
        Serial.println(F("[Auto] Avertissement: quiesce HTTP incomplet avant veille (transferts peuvent encore être actifs)"));
    }

    // Appel à la veille
    uint32_t actualSleptSec = _power.goToLightSleep(sleepDurationSec);

    AppTasks::releaseHttpAfterLightSleep();

    // Restauration aqua / chauffage / lumière tout de suite après réveil (avant WiFi / POST).
    // Sinon sendFullUpdate post-réveil envoyait etatPompeAqua=0 puis le poll réappliquait OFF.
    core.restoreActuatorsAfterWake(acts);

    // Réveil réseau : goToLightSleep() a déjà reconnecté + waitForNetworkReady si succès
    const bool wifiAlreadyUp = (WiFi.status() == WL_CONNECTED);

    if (!wifiAlreadyUp) {
        unsigned long wakeStartMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - wakeStartMs) < TimingConfig::WIFI_BOOT_TIMEOUT_MS) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiAlreadyUp) {
            core.waitForNetworkReady();
        } else {
            Serial.println(F("[Auto] WiFi déjà reconnecté par goToLightSleep — skip attente redondante"));
        }

        // Délai TCP/IP post-réveil (stack peut nécessiter un court buffer)
        Serial.println(F("[Auto] Attente stabilisation réseau après réveil..."));
        vTaskDelay(pdMS_TO_TICKS(NetworkConfig::WAKEUP_NETWORK_STABILIZATION_DELAY_MS));
        esp_task_wdt_reset();

        AppTasks::markWakeProtectionStart();

        // Phase 3 (v14.01): vider files résiduelles pre-veille puis POST réveil prioritaire
        if (!AppTasks::waitForNetworkQueuesDrain(NetworkConfig::WAKEUP_POST_DRAIN_TIMEOUT_MS)) {
            Serial.println(F("[Auto] Files réseau non vides avant POST réveil (timeout drain)"));
        }

        if (core.isRemoteSendEnabled() && WiFi.status() == WL_CONNECTED) {
            Serial.println(F("[Auto] 📤 Envoi données après réveil (prioritaire)..."));
            core.sendFullUpdate(r, nullptr);
            if (!AppTasks::waitForNetworkQueuesDrain(NetworkConfig::WAKEUP_POST_DRAIN_TIMEOUT_MS)) {
                Serial.println(F("[Auto] POST réveil peut encore être en cours (timeout drain)"));
            }
        } else if (!core.isRemoteSendEnabled()) {
            Serial.println(F("[Auto] Envoi distant désactivé — POST réveil ignoré"));
        } else {
            Serial.println(F("[Auto] ⚠️ WiFi déconnecté — POST réveil annulé"));
        }

        // Phase 4: Fetch config après POST (évite concurrence HTTP au réveil)
        ArduinoJson::StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
        bool fetchSuccess = false;
        bool fromNVSFallback = false;
        int fetchAttempts = 0;

        for (int attempt = 1; attempt <= NetworkConfig::WAKEUP_FETCH_MAX_RETRIES && !fetchSuccess; ++attempt) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.printf("[Auto] ⚠️ WiFi déconnecté avant tentative fetch %d/%d\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
                break;
            }

            Serial.printf("[Auto] 🔄 Tentative fetch config %d/%d après réveil...\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
            bool deferred = false;
            bool ok = AppTasks::netFetchRemoteState(doc, NetworkConfig::WAKEUP_FETCH_TIMEOUT_MS, &fromNVSFallback, &deferred);

            if (deferred) {
                Serial.printf("[Auto] Fetch config différé (tentative %d/%d) — retry\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
                vTaskDelay(pdMS_TO_TICKS(NetworkConfig::WAKEUP_FETCH_RETRY_DELAY_MS));
                esp_task_wdt_reset();
                --attempt;
                continue;
            }

            fetchAttempts++;
            if (ok && !fromNVSFallback && doc.size() > 0) {
                fetchSuccess = true;
                Serial.printf("[Auto] ✅ Fetch config réussi (tentative %d/%d)\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
                core.processFetchedRemoteConfig(doc);
            } else {
                Serial.printf("[Auto] ⚠️ Fetch config échoué (tentative %d/%d): ok=%d fromNVS=%d docSize=%u\n",
                              attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES, ok ? 1 : 0, fromNVSFallback ? 1 : 0, doc.size());
                if (attempt < NetworkConfig::WAKEUP_FETCH_MAX_RETRIES) {
                    Serial.printf("[Auto] Retry dans %u ms...\n", NetworkConfig::WAKEUP_FETCH_RETRY_DELAY_MS);
                    vTaskDelay(pdMS_TO_TICKS(NetworkConfig::WAKEUP_FETCH_RETRY_DELAY_MS));
                    esp_task_wdt_reset();
                }
            }
        }

        if (!fetchSuccess && fetchAttempts > 0) {
            Serial.println(F("[Auto] ⚠️ Fetch config échoué après tous les retries"));
        }
    }

    // 4. Mettre à jour _lastWakeMs après le réveil
    _lastWakeMs = millis();

    Serial.printf("[Auto] Réveil après %u secondes de veille\n", actualSleptSec);
    
    // Envoi du mail de réveil (si notifications activées) — après POST drain
    if (core.isEmailEnabled()) {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        const char* wakeReason = (cause == ESP_SLEEP_WAKEUP_TIMER) ? "Timer" : "Autre";
        core.sendWakeMail(wakeReason, actualSleptSec, r);
    }

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
    Serial.printf("[Auto] Délai OTA post-réveil (%u s)\n",
                  (unsigned)(TimingConfig::OTA_CHECK_DELAY_AFTER_WAKE_MS / 1000));
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::OTA_CHECK_DELAY_AFTER_WAKE_MS));
    esp_task_wdt_reset();
    Serial.println(F("[Auto] Demande vérification OTA après réveil (post POST)"));
    AppTasks::netRequestOtaCheck();
#endif

    // Le système est entré en veille et s'est réveillé
    return true;
}
