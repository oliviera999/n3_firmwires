#include "automatism/automatism_sleep.h"
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
    if (!_sleepConfig.adaptiveSleep) {
        return _sleepConfig.normalSleepTime;
    }
    
    uint32_t baseDelay = _sleepConfig.normalSleepTime;
    
    // Réduire si erreurs récentes
    if (hasRecentErrors()) {
        baseDelay = _sleepConfig.errorSleepTime;
    }
    
    // Réduire la nuit (économie énergie)
    if (isNightTime()) {
        baseDelay = _sleepConfig.nightSleepTime;
    }
    
    // Ajuster selon échecs réveil
    if (_consecutiveWakeupFailures > 0) {
        baseDelay = std::min(baseDelay * 2, _sleepConfig.maxSleepTime);
        Serial.printf("[Sleep] Délai ajusté (échecs: %d)\n", _consecutiveWakeupFailures);
    }
    
    // Limiter aux bornes
    baseDelay = std::max(baseDelay, _sleepConfig.minSleepTime);
    baseDelay = std::min(baseDelay, _sleepConfig.maxSleepTime);
    
    return baseDelay;
}

bool AutomatismSleep::isNightTime() {
    time_t currentTime = _power.getCurrentEpochSafe();
    struct tm timeinfo;
    
    // v11.179: Utiliser localtime_r() thread-safe au lieu de localtime()
    if (localtime_r(&currentTime, &timeinfo) != nullptr) {
        int hour = timeinfo.tm_hour;
        // Nuit: 19h-6h
        return (hour >= 19 || hour < 6);
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

bool AutomatismSleep::handleBlockingConditions(SystemActuators& acts,
                                               bool& forceWakeUp,
                                               bool& forceWakeFromWeb,
                                               unsigned long& lastWebActivityMs,
                                               uint32_t countdownEnd,
                                               unsigned long& lastWakeMs,
                                               bool feedingInProgress,
                                               bool tankPumpRunning,
                                               uint8_t wsClients) {
    unsigned long now = millis();

    // 1. VÉRIFICATION CLIENTS WEBSOCKET
    if (wsClients > 0) {
        if (_wsBlockStartMs == 0) {
            _wsBlockStartMs = now;
        }

        const uint32_t WS_BLOCK_TIMEOUT_MS = 300000; // 5 minutes
        if (now - _wsBlockStartMs > WS_BLOCK_TIMEOUT_MS) {
            Serial.printf("[Auto] ⚠️ TIMEOUT WebSocket atteint (%u ms) - Forcer sleep malgré %u clients\n",
                          WS_BLOCK_TIMEOUT_MS, wsClients);
            _wsBlockStartMs = 0;
        } else {
            if (now - _lastWsLogMs > 30000) {
                _lastWsLogMs = now;
                uint32_t elapsed = now - _wsBlockStartMs;
                Serial.printf("[Auto] Auto-sleep bloqué (%u clients WebSocket, %u ms écoulées)\n",
                              wsClients, elapsed);
            }
            return false;
        }
    } else {
        _wsBlockStartMs = 0;
    }

    // 2. ACTIVITÉ WEB TEMPORAIRE
    if (forceWakeFromWeb) {
        if (lastWebActivityMs > 0 && (now - lastWebActivityMs > TimingConfig::WEB_ACTIVITY_TIMEOUT_MS)) {
            forceWakeFromWeb = false;
            Serial.println(F("[Auto] Activité web expirée - sleep autorisé à nouveau"));
        } else {
            if (now - _lastWebLogMs > 30000) {
                _lastWebLogMs = now;
                Serial.println(F("[Auto] Auto-sleep bloqué temporairement (activité web récente)"));
            }
            return false;
        }
    }

    // 3. FORCEWAKEUP PERSISTANT
    if (forceWakeUp) {
        if (now - _lastForceWakeLogMs > 30000) {
            _lastForceWakeLogMs = now;
            Serial.println(F("[Auto] Auto-sleep désactivé (forceWakeUp GPIO 115 = true)"));
        }
        return false;
    }

    // 4. POMPE DE REMPLISSAGE
    if (tankPumpRunning) {
        lastWakeMs = now;
        Serial.println(F("[Auto] Auto-sleep différé - pompe de remplissage active"));
        return false;
    }

    // 5. NOURRISSAGE EN COURS
    if (feedingInProgress) {
        lastWakeMs = now;
        Serial.println(F("[Auto] Auto-sleep différé - nourrissage en cours"));
        return false;
    }

    // 6. DÉCOMPTE EN COURS
    if (isStillPending(countdownEnd, now)) {
        uint32_t remainingCountdownMs = static_cast<uint32_t>(countdownEnd - now);
        uint32_t remainingCountdownSec = remainingCountdownMs / 1000UL;

        if (remainingCountdownSec > 300) {
            lastWakeMs = now;
            Serial.printf("[Auto] Auto-sleep différé - décompte long en cours (%u s restants)\n",
                          remainingCountdownSec);
        } else {
            Serial.printf("[Auto] Décompte court en cours (%u s restants) - chronomètre préservé\n",
                          remainingCountdownSec);
        }
        return false;
    }

    // v11.178: Bloc hasSignificantActivity() supprimé (toujours false - audit dead-code)

    return true;
}

// ============================================================================
// MÉTHODE PRINCIPALE : handleAutoSleep
// ============================================================================

bool AutomatismSleep::handleAutoSleep(const SensorReadings& r, SystemActuators& acts, Automatism& core) {
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

    // Appel à la veille
    uint32_t actualSleptSec = _power.goToLightSleep(sleepDurationSec);

    // Réveil: attente WiFi, stabilisation réseau, fetch config avec retries, puis envoi
    unsigned long wakeStartMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wakeStartMs) < TimingConfig::WIFI_BOOT_TIMEOUT_MS) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // Phase 1: Stabilisation réseau (déjà fait par waitForNetworkReady)
        core.waitForNetworkReady();
        
        // Phase 2: Délai supplémentaire pour garantir stabilisation complète TCP/IP
        // Après light sleep, le stack réseau peut nécessiter un délai supplémentaire
        Serial.println(F("[Auto] Attente stabilisation complète réseau après réveil..."));
        vTaskDelay(pdMS_TO_TICKS(NetworkConfig::WAKEUP_NETWORK_STABILIZATION_DELAY_MS));
        esp_task_wdt_reset();
        
        // Phase 3: Fetch config avec retries (critique pour commandes programmées)
        ArduinoJson::StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
        bool fetchSuccess = false;
        bool fromNVSFallback = false;
        
        for (int attempt = 1; attempt <= NetworkConfig::WAKEUP_FETCH_MAX_RETRIES && !fetchSuccess; attempt++) {
            // Vérifier que WiFi est toujours connecté avant chaque tentative
            if (WiFi.status() != WL_CONNECTED) {
                Serial.printf("[Auto] ⚠️ WiFi déconnecté avant tentative fetch %d/%d\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
                break;
            }
            
            Serial.printf("[Auto] 🔄 Tentative fetch config %d/%d après réveil...\n", attempt, NetworkConfig::WAKEUP_FETCH_MAX_RETRIES);
            bool ok = AppTasks::netFetchRemoteState(doc, NetworkConfig::WAKEUP_FETCH_TIMEOUT_MS, &fromNVSFallback);
            
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
        
        if (!fetchSuccess) {
            Serial.println(F("[Auto] ⚠️ Fetch config échoué après tous les retries - envoi différé"));
            // Optionnel: différer l'envoi si fetch critique échoue
            // Pour l'instant, on continue avec l'envoi pour garantir les données
        }
        
        // Phase 4: Envoi données (garanti si WiFi connecté et envoi activé)
        if (core.isRemoteSendEnabled()) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println(F("[Auto] 📤 Envoi données après réveil..."));
                core.sendFullUpdate(r, nullptr);
            } else {
                Serial.println(F("[Auto] ⚠️ WiFi déconnecté - envoi annulé"));
            }
        }
    }

    // Restauration aqua / chauffage / lumière au réveil
    core.restoreActuatorsAfterWake(acts);

    // 4. Mettre à jour _lastWakeMs après le réveil
    _lastWakeMs = millis();

    Serial.printf("[Auto] Réveil après %u secondes de veille\n", actualSleptSec);
    
    // Envoi du mail de réveil (si notifications activées)
    if (core.isEmailEnabled()) {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        const char* wakeReason = (cause == ESP_SLEEP_WAKEUP_TIMER) ? "Timer" : "Autre";
        core.sendWakeMail(wakeReason, actualSleptSec, r);
    }

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
    Serial.println(F("[Auto] Demande vérification OTA après réveil"));
    AppTasks::netRequestOtaCheck();
#endif

    // Le système est entré en veille et s'est réveillé
    return true;
}
