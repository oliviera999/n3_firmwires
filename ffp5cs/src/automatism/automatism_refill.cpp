// automatism/automatism_refill.cpp — logique de remplissage/réservoir d'Automatism
// (sécurités trop-plein/niveau-bas, démarrage/arrêt auto, durée max, récupération).
// Extrait de automatism.cpp (audit : découpe par responsabilité). Comportement identique.
#include "automatism.h"
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
    if (r.wlAqua < cmToMm(_network.getLimFlood())) {
        if (!tankPumpLocked || _tankPumpLockReason != TankPumpLockReason::AQUARIUM_OVERFILL) {
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
        }
    } else {
        if (tankPumpLocked && _tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL && !inFlood) {
            tankPumpLocked = false;
            _tankPumpLockReason = TankPumpLockReason::NONE;
            emailTankSent = false;
            emailTankStartSent = false;
            emailTankStopSent = false;
            Serial.println(F("[Auto] Pompe déverrouillée (aquarium OK)"));
        }
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
    if (r.wlAqua > cmToMm(_network.getAqThresholdCm()) && !tankPumpLocked &&
        tankPumpRetries < MAX_PUMP_RETRIES && !_manualTankOverride) {
        if (!_acts.isTankPumpRunning()) {
            // Vérifier si réserve trop basse
            if (r.wlTank > cmToMm(_network.getTankThresholdCm())) {
                Serial.printf("[CRITIQUE] Réserve basse (distance %u mm > seuil %u mm) - pompe verrouillée\n",
                              r.wlTank, cmToMm(_network.getTankThresholdCm()));
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
                             r.wlTank, cmToMm(_network.getTankThresholdCm()), r.wlAqua);
                    _mailer.sendAlert("Pompe BLOQUÉE (réserve basse)", msg, _network.getEmailAddress());
                    emailTankSent = true;
                }
                // En période de grâce on n'envoie pas et on ne marque pas emailTankSent :
                // après 30s on enverra le mail si la condition est toujours vraie.
                return true; // Bloqué
            }
            // Démarrage effectif
            Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTO ==="));
            Serial.printf("[CRITIQUE] Aqua: %d mm, Seuil: %d mm, Durée: %lu s\n",
                          r.wlAqua, cmToMm(_network.getAqThresholdCm()), refillDurationMs / 1000);

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
                int levelImprovement = _levelAtPumpStart - r.wlAqua;
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
    
    // Détection anomalie timing (>50 min = 3000000ms)
    // Cela détecte aussi les cas où _pumpStartMs était invalide
    if (elapsedMs > 3000000UL) {
        Serial.printf("[CRITIQUE] Anomalie timing: elapsed=%u ms, reset\n", (unsigned)elapsedMs);
        _pumpStartMs = nowMs;
        return;
    }
    
    if (elapsedMs < maxMs) return;

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

    int levelImprovement = _levelAtPumpStart - r.wlAqua;
    Serial.printf("[CRITIQUE] Amélioration niveau: %d mm\n", levelImprovement);

    if (levelImprovement < 1) {
        ++tankPumpRetries;
        Serial.printf("[CRITIQUE] Pompe inefficace (%u/%u)\n", tankPumpRetries, MAX_PUMP_RETRIES);
        if (tankPumpRetries >= MAX_PUMP_RETRIES) {
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
    } else {
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

    static uint8_t aboveCount = 0;
    static uint8_t belowCount = 0;
    
    if (r.wlTank > cmToMm(_network.getTankThresholdCm())) {
        aboveCount = min<uint8_t>(aboveCount + 1, 3);
        belowCount = 0;
        if (!tankPumpLocked && aboveCount >= 2) {
            Serial.println(F("[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ==="));
            Serial.printf("[CRITIQUE] Réserve basse (distance %d mm > seuil %d mm) - pompe verrouillée\n",
                          r.wlTank, cmToMm(_network.getTankThresholdCm()));
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
                         r.wlTank, cmToMm(_network.getTankThresholdCm()));
                _mailer.sendAlert("Pompe verrouillée (réserve basse)", msg, _network.getEmailAddress());
                emailTankSent = true;
            }
            // En période de grâce on n'envoie pas et on ne marque pas emailTankSent :
            // après 30s on enverra le mail si la condition est toujours vraie.
        }
    } else if (r.wlTank < cmToMm((_network.getTankThresholdCm() > 5) ? (_network.getTankThresholdCm() - 5) : 0)) {
        belowCount = min<uint8_t>(belowCount + 1, 3);
        aboveCount = 0;
        if (tankPumpLocked && belowCount >= 3) {
            tankPumpLocked = false;
            _tankPumpLockReason = TankPumpLockReason::NONE;
            emailTankSent = false;
            emailTankStartSent = false;
            emailTankStopSent = false;
            Serial.printf("[Auto] Pompe déverrouillée (réserve: %d mm)\n", r.wlTank);
        }
    } else {
        aboveCount = min<uint8_t>(aboveCount, 2);
        belowCount = min<uint8_t>(belowCount, 2);
    }
}

// Sous-fonction: Récupération automatique après blocage
void Automatism::handleRefillAutomaticRecovery(const SensorReadings& r) {
    static unsigned long lastRecoveryAttempt = 0;
    if (tankPumpLocked && tankPumpRetries >= MAX_PUMP_RETRIES) {
        unsigned long currentMillisLocal = millis();
        if (currentMillisLocal - lastRecoveryAttempt > 30 * 1000UL) {
            if (r.wlTank < cmToMm((_network.getTankThresholdCm() > 10) ? (_network.getTankThresholdCm() - 10) : 0)) {
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

// Fusionné depuis AutomatismAlertController::process()
