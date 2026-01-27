#include "automatism/automatism_sleep.h"
#include "automatism.h" // Pour accès aux méthodes de Automatism
#include "config.h"
#include "task_monitor.h"
#include "realtime_websocket.h"
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
// MARÉES
// ============================================================================

void AutomatismSleep::handleMaree(const SensorReadings& r) {
    // Note: Le sleep anticipé marée est géré dans handleAutoSleep()
    // Cette méthode fait juste le logging ou des actions spécifiques marée
    // Pour l'instant minimaliste
    // Serial.printf("[Sleep] Marée - wlAqua=%u cm\n", r.wlAqua);
}

// ============================================================================
// ACTIVITÉ
// ============================================================================

bool AutomatismSleep::hasSignificantActivity() {
    // Désactivé: seuls nourrissage/remplissage retardent le sleep
    // Gérés explicitement dans handleAutoSleep()
    return false;
}

void AutomatismSleep::updateActivityTimestamp() {
    unsigned long currentMillis = millis();
    _lastActivityMs = currentMillis;
    _lastWakeMs = currentMillis;
    Serial.println(F("[Sleep] Timestamp activité mis à jour"));
}

void AutomatismSleep::logActivity(const char* activity) {
    Serial.printf("[Sleep] Activité détectée: %s\n", activity);
    updateActivityTimestamp();
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
        // Serial.println(F("[Sleep] Délai réduit (erreurs)"));
    }
    
    // Réduire la nuit (économie énergie)
    if (isNightTime()) {
        baseDelay = _sleepConfig.nightSleepTime;
        // Serial.println(F("[Sleep] Délai réduit (nuit)"));
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

bool AutomatismSleep::isAdaptiveSleepEnabled() const {
    return _sleepConfig.adaptiveSleep;
}

bool AutomatismSleep::isNightTime() {
    time_t currentTime = _power.getCurrentEpoch();
    struct tm* timeinfo = localtime(&currentTime);
    
    if (timeinfo != nullptr) {
        int hour = timeinfo->tm_hour;
        // Nuit: 22h-6h (selon convention)
        return (hour >= 22 || hour < 6);
    }
    
    return false;
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

    bool tideAscendingTrigger = (diffMaree10s > tideTriggerCm);

    if (tideAscendingTrigger || delayReached) {
        unsigned long awakeSec = (nowMs - lastWakeMs) / 1000UL;
        if (tideAscendingTrigger) {
            Serial.printf("[Auto] Sleep précoce déclenché: marée montante (~10s, +%d cm)\n",
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

    // 7. ACTIVITÉ FINE
    if (hasSignificantActivity()) {
        if (now - _lastActivityLogMs > 5000) {
            _lastActivityLogMs = now;
            Serial.println(F("[Auto] Activité détectée - sleep différé (priorité réduite)"));
        }
        lastWakeMs = now;
        return false;
    }

    return true;
}

bool AutomatismSleep::evaluateAutoSleep(const AutoSleepContext& ctx, AutoSleepDecision& outDecision) {
    AutoSleepDecision decision{};
    decision.diff10s = ctx.diff10s;
    decision.adaptiveDelaySec = calculateAdaptiveSleepDelay();
    decision.awakeSec = (ctx.currentMillis - *ctx.lastWakeMs) / 1000UL;
    decision.tideAscending = (ctx.diff10s > ctx.tideTriggerCm);

    bool delayReached = (ctx.currentMillis - *ctx.lastWakeMs) >= (decision.adaptiveDelaySec * 1000UL);
    bool ready = decision.tideAscending || delayReached;

    outDecision = decision;
    return ready;
}

void AutomatismSleep::logSleepDecision(bool pumpReservoirOn,
                                      bool feedingActive,
                                      bool countdownActive,
                                      bool tideAscending,
                                      int diff10s,
                                      unsigned long awakeSec,
                                      uint32_t adaptiveDelaySec,
                                      uint16_t nextWakeSec) {
    Serial.printf("[Auto] Délai écoulé: éveillé=%lu s, cible=%u s. Veille prévue=%u s\n",
                  awakeSec, adaptiveDelaySec, nextWakeSec);
    Serial.printf("[Auto] Raison du passage: aucune activité bloquante (pompeReserv=%s, nourrissage=%s, decompte=%s)\n",
                  pumpReservoirOn ? "ON" : "OFF",
                  feedingActive ? "OUI" : "NON",
                  countdownActive ? "OUI" : "NON");
    if (tideAscending) {
        Serial.printf("[Auto] Sleep anticipé: marée montante (~10s, +%d cm)\n", diff10s);
    }
}

// ============================================================================
// TRANSITION ET LOGS
// ============================================================================

void AutomatismSleep::logSleepTransitionStart(const char* reason,
                                         uint32_t scheduledSeconds,
                                         unsigned long awakeSec,
                                         bool tideAscending,
                                         int diff10s,
                                         uint32_t heapAfterCleanup,
                                         const TaskMonitor::Snapshot& tasks) {
  LOG_INFO("=== DÉBUT TRANSITION VEILLE ===");
  LOG_INFO("Raison: %s", reason);
  LOG_INFO("Durée prévue: %u sec", scheduledSeconds);
  LOG_INFO("Temps d'éveil: %lu sec", awakeSec);
  LOG_INFO("Marée: %s (diff10s: %d)", tideAscending ? "Montante" : "Descendante", diff10s);
  LOG_INFO("Heap avant sleep: %u bytes", heapAfterCleanup);
  
  TaskMonitor::logSnapshot(tasks, "pre-sleep");
}

void AutomatismSleep::logSleepTransitionEnd(uint32_t scheduledSeconds,
                                       uint32_t actualSeconds,
                                       esp_sleep_wakeup_cause_t wakeCause,
                                       const TaskMonitor::Snapshot& tasksBefore,
                                       const TaskMonitor::Snapshot& tasksAfter) {
  LOG_INFO("=== FIN TRANSITION VEILLE ===");
  LOG_INFO("Durée réelle: %u sec (prévue: %u)", actualSeconds, scheduledSeconds);
  
  // Utiliser le PowerManager pour logger la cause de réveil en détail
  _power.logWakeupCause(wakeCause);
  
  TaskMonitor::logDiff(tasksBefore, tasksAfter, "sleep-resume");
}

// ============================================================================
// MÉTHODE PRINCIPALE : PROCESS
// ============================================================================

bool AutomatismSleep::process(const SensorReadings& r, SystemActuators& acts, Automatism& core) {
    // Cette méthode remplace handleAutoSleep() en centralisant la logique
    return handleAutoSleep(r, acts, core);
}

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
    const bool tideAscending = (diffMaree10s > tideTriggerCm);
    const bool decisionChanged = (canSleep != _lastCanSleep) || (shouldSleep != _lastShouldSleep);
    if (decisionChanged || (nowMs - _lastDecisionLogMs > 30000)) {
        _lastDecisionLogMs = nowMs;
        _lastCanSleep = canSleep;
        _lastShouldSleep = shouldSleep;
        Serial.printf("[Auto] Sleep decision: can=%d should=%d delay=%d tide=%d diff=%d trig=%d ws=%u feed=%d pump=%d cd=%lu wake=%lu s sleep=%u s\n",
                      canSleep ? 1 : 0,
                      shouldSleep ? 1 : 0,
                      delayReached ? 1 : 0,
                      tideAscending ? 1 : 0,
                      diffMaree10s,
                      tideTriggerCm,
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
    
    // Envoi du mail de mise en veille (si notifications activées)
    if (core.isEmailEnabled()) {
        const char* reason = tideAscending ? "Marée montante détectée" : "Délai d'inactivité atteint";
        core.sendSleepMail(reason, sleepDurationSec, r);
    }
    
    // Appel à la veille
    uint32_t actualSleptSec = _power.goToLightSleep(sleepDurationSec);
    
    // 4. Mettre à jour _lastWakeMs après le réveil
    _lastWakeMs = millis();
    
    Serial.printf("[Auto] Réveil après %u secondes de veille\n", actualSleptSec);
    
    // Envoi du mail de réveil (si notifications activées)
    if (core.isEmailEnabled()) {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        const char* wakeReason = (cause == ESP_SLEEP_WAKEUP_TIMER) ? "Timer" : "Autre";
        core.sendWakeMail(wakeReason, actualSleptSec, r);
    }
    
    // Le système est entré en veille et s'est réveillé
    return true;
}
