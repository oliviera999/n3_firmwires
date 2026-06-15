// automatism/automatism_refill.cpp — logique de remplissage/réservoir d'Automatism
// (sécurités trop-plein/niveau-bas, démarrage/arrêt auto, durée max, récupération).
// Extrait de automatism.cpp (audit : découpe par responsabilité). Comportement identique.
#include "automatism.h"
#include "automatism/threshold_util.h"  // C4: soustraction saturée seuil-marge (pure, testée)
#include "automatism/refill_start.h"  // C4: décision démarrage remplissage (pure, testée)
#include "automatism/refill_duration.h"  // C4: décision timing remplissage (pure, testée)
#include "automatism/refill_efficiency.h"  // C4: décision efficacité/retry remplissage (pure, testée)
#include "automatism/refill_overfill.h"  // C4: décision sécurité trop-plein (pure, testée)
#include "config.h"
#include "mailer.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <time.h>

// Helper local (identique à automatism.cpp) : conversion cm -> mm.
static uint16_t cmToMm(uint16_t cm) { return SensorConfig::Ultrasonic::cmToMm(cm); }


void Automatism::handleRefillAquariumOverfillSecurity(const SensorReadings& r) {
    // C4: décision de sécurité trop-plein déléguée à RefillOverfill (pure, testée).
    // Les effets de bord (verrou + motif, arrêt pompe, notif, reset flags email) restent ici.
    const bool lockedForOverfill =
        tankPumpLocked && _tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL;
    const RefillOverfill::Decision d = RefillOverfill::evaluate(
        SensorValidation::isWaterLevelKnown(r.wlAqua), r.wlAqua,
        cmToMm(_network.getLimFlood()), lockedForOverfill, inFlood);

    if (d == RefillOverfill::Decision::Lock) {
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::AQUARIUM_OVERFILL;
        if (_acts.isTankPumpRunning()) {
            _acts.stopTankPump(_pumpStartMs);
            _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
            _countdownEnd = 0;
            if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                sendFullUpdate(r, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
                Serial.println(F("[Auto] Arrêt sécurité notifié - pump_tank=0"));
            }
        }
        Serial.println(F("[CRITIQUE] Aquarium trop plein - pompe verrouillée"));
    } else if (d == RefillOverfill::Decision::Unlock) {
        tankPumpLocked = false;
        _tankPumpLockReason = TankPumpLockReason::NONE;
        emailTankSent = false;
        emailTankStartSent = false;
        emailTankStopSent = false;
        Serial.println(F("[Auto] Pompe déverrouillée (aquarium OK)"));
    }
}

// Sous-fonction: Vérification cohérence mode manuel
void Automatism::handleRefillManualModeCheck() {
    if (!_acts.isTankPumpRunning() && _manualTankOverride) {
        Serial.println(F("[CRITIQUE] Mode manuel sans pompe - reset"));
        _manualTankOverride = false;
        _countdownEnd = 0;
        _pumpStartMs = 0;
    }
}

// Sous-fonction: Démarrage automatique (retourne true si bloqué par réserve basse)
bool Automatism::handleRefillAutomaticStart(const SensorReadings& r) {
    // C4: décision (gating démarrage / blocage réserve basse) déléguée à la machine
    // pure RefillStart. Les effets de bord (pompe, verrou, email, timers, logs) restent ici.
    const uint16_t aqThrMm   = cmToMm(_network.getAqThresholdCm());
    const uint16_t tankThrMm = cmToMm(_network.getTankThresholdCm());
    const RefillStart::Decision d = RefillStart::evaluate(
        SensorValidation::isWaterLevelKnown(r.wlAqua), r.wlAqua, r.wlTank,
        aqThrMm, tankThrMm, tankPumpLocked, tankPumpRetries, MAX_PUMP_RETRIES,
        _manualTankOverride, _acts.isTankPumpRunning());

    if (d == RefillStart::Decision::BlockReserveLow) {
        Serial.printf("[CRITIQUE] Réserve basse (distance %u mm > seuil %u mm) - pompe verrouillée\n",
                      r.wlTank, tankThrMm);
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        const bool startupGrace = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
        if (_network.isEmailEnabled() && !emailTankSent && !startupGrace) {
            char msg[384];
            snprintf(msg, sizeof(msg),
                     "Remplissage bloqué (réserve basse)\n"
                     "Réserve: %d mm (seuil: %d mm)\nAqua: %d mm",
                     r.wlTank, tankThrMm, r.wlAqua);
            _mailer.sendAlert("Pompe BLOQUÉE (réserve basse)", msg, _network.getEmailAddress());
            emailTankSent = true;
        }
        // En période de grâce on n'envoie pas et on ne marque pas emailTankSent :
        // après 30s on enverra le mail si la condition est toujours vraie.
        return true; // Bloqué
    }

    if (d == RefillStart::Decision::Start) {
        // Démarrage effectif
        Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTO ==="));
        Serial.printf("[CRITIQUE] Aqua: %d mm, Seuil: %d mm, Durée: %lu s\n",
                      r.wlAqua, aqThrMm, refillDurationMs / 1000);

        _acts.startTankPump();
        strncpy(_countdownLabel, "Refill", sizeof(_countdownLabel) - 1);
        _countdownLabel[sizeof(_countdownLabel) - 1] = '\0';
        _countdownEnd = millis() + refillDurationMs;
        _pumpStartMs = millis();
        _levelAtPumpStart = r.wlAqua;
        logActivity("Démarrage pompe réservoir automatique");

        if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
            sendFullUpdate(r, "etatPompeTank=1");
        }

        if (_network.isEmailEnabled() && !emailTankStartSent) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Remplissage AUTO démarré\nAqua: %d mm, Réserve: %d mm, Durée: %lu s",
                     r.wlAqua, r.wlTank, refillDurationMs / 1000);
            _mailer.send("Remplissage démarré", msg, "System", _network.getEmailAddress());
            emailTankStartSent = true;
            emailTankStopSent = false;
        }
    }
    return false;
}

// Sous-fonction: Fin cycle manuel
void Automatism::handleRefillManualCycleEnd(const SensorReadings& r) {
    if (_manualTankOverride && _acts.isTankPumpRunning()) {
        if (_countdownEnd > 0 && millis() >= _countdownEnd) {
            Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE MANUEL ==="));
            _acts.stopTankPump(_pumpStartMs);
            _lastTankStopReason = TankPumpStopReason::MANUAL;
            _manualTankOverride = false;
            _countdownEnd = 0;
            _pumpStartMs = 0;

            if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                sendFullUpdate(r, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
            }

            if (_network.isEmailEnabled() && !emailTankStopSent) {
                int levelImprovement = 0;
                if (SensorValidation::isWaterLevelKnown(r.wlAqua) && _levelAtPumpStart > 0) {
                    levelImprovement = _levelAtPumpStart - r.wlAqua;
                }
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Remplissage MANUEL terminé\nAmélioration: %d mm, Aqua: %d mm",
                         levelImprovement, r.wlAqua);
                _mailer.send("Remplissage terminé", msg, "System", _network.getEmailAddress());
                emailTankStopSent = true;
                emailTankStartSent = false;
            }
        }
    }
}

// Sous-fonction: Arrêt forcé après durée max
void Automatism::handleRefillMaxDurationStop(const SensorReadings& r) {
    if (!_acts.isTankPumpRunning()) return;

    const uint32_t nowMs = millis();
    // v11.176: Gestion overflow millis() (wrap après ~49 jours) - audit robustesse
    // Si _pumpStartMs > nowMs, c'est un overflow -> calcul correct grâce à l'arithmétique unsigned
    const uint32_t elapsedMs = nowMs - _pumpStartMs;
    const uint32_t maxMs = refillDurationMs;
    
    // C4: décision de timing (anomalie / continuer / arrêt forcé) déléguée à RefillDuration (pure, testée).
    const RefillDuration::Decision durDec = RefillDuration::evaluate(elapsedMs, maxMs);
    if (durDec == RefillDuration::Decision::AnomalyReset) {
        // Anomalie timing (>50 min) ; détecte aussi un _pumpStartMs invalide.
        Serial.printf("[CRITIQUE] Anomalie timing: elapsed=%u ms, reset\n", (unsigned)elapsedMs);
        _pumpStartMs = nowMs;
        return;
    }
    if (durDec == RefillDuration::Decision::Continue) return;
    // ForcedStop -> on poursuit l'arrêt forcé ci-dessous.

    // Arrêt forcé
    Serial.println(F("[CRITIQUE] === ARRÊT FORCÉ REMPLISSAGE ==="));
    Serial.printf("[CRITIQUE] Durée: %u s / %u s max\n", 
                  (unsigned)(elapsedMs/1000U), (unsigned)(maxMs/1000U));

    _lastTankStopReason = TankPumpStopReason::MAX_DURATION;
    _acts.stopTankPump(_pumpStartMs);
    _pumpStartMs = 0;
    _countdownEnd = 0;
    _manualTankOverride = false;

    if (WiFi.status() == WL_CONNECTED) {
        sendFullUpdate(r, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
    }

    int levelImprovement = 0;
    if (SensorValidation::isWaterLevelKnown(r.wlAqua) && _levelAtPumpStart > 0) {
        levelImprovement = _levelAtPumpStart - r.wlAqua;
    }
    Serial.printf("[CRITIQUE] Amélioration niveau: %d mm\n", levelImprovement);

    // C4: décision d'efficacité (retry / lock / effective / sans-éval) déléguée à
    // RefillEfficiency (pure, testée). Les effets de bord (compteur, verrou, email, sync) ici.
    const RefillEfficiency::Decision effDec = RefillEfficiency::evaluate(
        SensorValidation::isWaterLevelKnown(r.wlAqua), levelImprovement,
        tankPumpRetries, MAX_PUMP_RETRIES);

    if (effDec == RefillEfficiency::Decision::NoEval) {
        Serial.println(F("[CRITIQUE] Niveau aquarium inconnu — fin remplissage sans évaluation d'efficacité"));
        Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE ==="));
        return;
    }

    if (effDec == RefillEfficiency::Decision::Inefficient ||
        effDec == RefillEfficiency::Decision::InefficientLock) {
        ++tankPumpRetries;
        Serial.printf("[CRITIQUE] Pompe inefficace (%u/%u)\n", tankPumpRetries, MAX_PUMP_RETRIES);
        if (effDec == RefillEfficiency::Decision::InefficientLock) {
            tankPumpLocked = true;
            _tankPumpLockReason = TankPumpLockReason::INEFFICIENT;
            Serial.println(F("[CRITIQUE] Pompe BLOQUÉE - max tentatives"));
            sendFullUpdate(r, "etatPompeTank=0&pump_tankCmd=0&pump_tank=0");
            if (_network.isEmailEnabled() && !emailTankSent) {
                char msg[384];
                snprintf(msg, sizeof(msg),
                         "Pompe BLOQUÉE (inefficace)\nTentatives: %d/%d, Amélioration: %d mm",
                         tankPumpRetries, (unsigned)MAX_PUMP_RETRIES, levelImprovement);
                _mailer.sendAlert("Pompe réservoir bloquée", msg, _network.getEmailAddress());
                emailTankSent = true;
            }
        }
    } else {  // Effective
        tankPumpRetries = 0;
        Serial.printf("[CRITIQUE] Remplissage OK: +%d mm\n", levelImprovement);
        if (_network.isEmailEnabled() && !emailTankStopSent) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Remplissage TERMINÉ\nDurée: %u s, Amélioration: %d mm, Aqua: %d mm",
                     (unsigned)(elapsedMs / 1000), levelImprovement, r.wlAqua);
            _mailer.send("Remplissage terminé", msg, "System", _network.getEmailAddress());
            emailTankStopSent = true;
            emailTankStartSent = false;
        }
    }
    Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE ==="));
}

// Sous-fonction: Sécurité réserve basse avec hystérésis
void Automatism::handleRefillReservoirLowSecurity(const SensorReadings& r) {
    if (_manualTankOverride) return;

    // C4: décision (debounce + hystérésis) déléguée à la machine d'état pure
    // ReservoirLowSecurity. L'état persistant vit dans _reservoirLowState (membre,
    // remplace les anciens static locaux). Les effets de bord restent ici.
    const uint16_t thrCm  = _network.getTankThresholdCm();
    const uint16_t lowThr = cmToMm(thrCm);
    const uint16_t okThr  = cmToMm(ThresholdUtil::subSat(thrCm, 5));

    const ReservoirLowSecurity::Decision d =
        ReservoirLowSecurity::evaluate(_reservoirLowState, r.wlTank, lowThr, okThr, tankPumpLocked);

    if (d == ReservoirLowSecurity::Decision::Lock) {
        Serial.println(F("[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ==="));
        Serial.printf("[CRITIQUE] Réserve basse (distance %d mm > seuil %d mm) - pompe verrouillée\n",
                      r.wlTank, lowThr);
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _acts.stopTankPump(_pumpStartMs);
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        const bool startupGrace = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
        if (_network.isEmailEnabled() && !emailTankSent && !startupGrace) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Pompe VERROUILLÉE (réserve basse)\nRéserve: %d mm (seuil: %d mm)",
                     r.wlTank, lowThr);
            _mailer.sendAlert("Pompe verrouillée (réserve basse)", msg, _network.getEmailAddress());
            emailTankSent = true;
        }
        // En période de grâce on n'envoie pas et on ne marque pas emailTankSent :
        // après 30s on enverra le mail si la condition est toujours vraie.
    } else if (d == ReservoirLowSecurity::Decision::Unlock) {
        tankPumpLocked = false;
        _tankPumpLockReason = TankPumpLockReason::NONE;
        emailTankSent = false;
        emailTankStartSent = false;
        emailTankStopSent = false;
        Serial.printf("[Auto] Pompe déverrouillée (réserve: %d mm)\n", r.wlTank);
    }
}

// Sous-fonction: Récupération automatique après blocage
void Automatism::handleRefillAutomaticRecovery(const SensorReadings& r) {
    static unsigned long lastRecoveryAttempt = 0;
    if (tankPumpLocked && tankPumpRetries >= MAX_PUMP_RETRIES) {
        unsigned long currentMillisLocal = millis();
        if (currentMillisLocal - lastRecoveryAttempt > 30 * 1000UL) {
            if (r.wlTank < cmToMm(ThresholdUtil::subSat(_network.getTankThresholdCm(), 10))) {
                Serial.println(F("[CRITIQUE] === RÉCUPÉRATION AUTO ==="));
                Serial.printf("[CRITIQUE] Réservoir: %d mm (OK)\n", r.wlTank);
                tankPumpLocked = false;
                tankPumpRetries = 0;
                emailTankSent = false;
                emailTankStartSent = false;
                emailTankStopSent = false;
                _tankPumpLockReason = TankPumpLockReason::NONE;
                lastRecoveryAttempt = currentMillisLocal;
                Serial.println(F("[CRITIQUE] Pompe débloquée"));
            }
        }
    }
}

// Fonction principale refactorisée - appelle les sous-fonctions
void Automatism::handleRefill(const AutomatismRuntimeContext& ctx) {
    const SensorReadings& r = ctx.readings;

    // 0. Sécurité aquarium trop plein
    handleRefillAquariumOverfillSecurity(r);

    // 1. Vérification cohérence mode manuel
    handleRefillManualModeCheck();

    // 2. Démarrage automatique (si conditions remplies)
    if (handleRefillAutomaticStart(r)) {
        return; // Bloqué par réserve basse
    }

    // 3. Fin cycle manuel (timeout)
    handleRefillManualCycleEnd(r);

    // 4. Arrêt forcé après durée max
    handleRefillMaxDurationStop(r);

    // 5. Sécurité réserve basse avec hystérésis
    handleRefillReservoirLowSecurity(r);

    // 6. Récupération automatique après blocage
    handleRefillAutomaticRecovery(r);
}

