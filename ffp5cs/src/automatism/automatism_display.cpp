// automatism/automatism_display.cpp — alertes, rendu d'affichage et snapshot NVS
// d'Automatism (handleAlerts, updateDisplayInternal, saveActuatorSnapshotToNVS).
// Extrait de automatism.cpp (audit : découpe). Méthodes membres, comportement identique.
#include "automatism.h"
#include "automatism/flood_alert.h"  // C4: machine d'état anti-spam trop-plein (pure, testée)
#include "automatism/heater_hysteresis.h"  // C4: régulation chauffage par hystérésis (pure, testée)
#include "automatism/heater_orchestrator.h"  // C4: orchestrateur chauffage (effets de bord, testable)
#include "automatism/level_alert_orchestrator.h"  // C4: orchestrateur alertes niveau (effets de bord, testable)
#include "automatism/flood_orchestrator.h"  // C4: orchestrateur trop-plein (effets de bord, testable)
#include "automatism/level_alert.h"  // C4: alerte niveau d'eau seuil+hystérésis (pure, testée)
#include "automatism/shared_alert_gate.h"  // Phase 3: arbitrage serveur primaire / ESP relais
#include "automatism/actuator_snapshot.h"  // C4: capture/restore actionneurs (testable via IActuators)
#include "automatism/feeding_timing.h"  // C4: durée de cycle nourrissage (pure, testée, dédup ×6)
#include "automatism/threshold_util.h"  // C4: soustraction saturée seuil-marge (pure, testée)
#include "config.h"
#include "config_manager.h"
#include "mailer.h"
#include "nvs_manager.h"
#include "nvs_keys.h"
#include "dbvars_cache.h"
#include "task_monitor.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <cstring>
#include <cstdio>
#include <time.h>

extern ConfigManager config;  // global (idem automatism.cpp via headers)

namespace {
// (formatDistanceAlert / formatTemperatureAlert retirés : le formatage des mails d'alerte
//  vit désormais dans les orchestrateurs Level/Flood/Heater — plus d'usage local.)
static uint16_t cmToMm(uint16_t cm) { return SensorConfig::Ultrasonic::cmToMm(cm); }
}  // namespace


// Phase 3 arbitrage : le serveur couvre les MAILS d'alertes/confirmations partagées
// si l'échange est sain (GET config OK + dernier POST réussi < 90 s). Utilisé par
// handleAlerts (mails niveau/flood/chauffage) et par le module refill (confirmations
// mail remplissage). Ne couvre JAMAIS les actionneurs locaux (pompe, chauffage).
bool Automatism::serverCoversSharedAlerts() const {
    return SharedAlertGate::serverCovers(
        _network.isServerOk(), millis(), _network.getLastSendMs());
}

// Fusionné depuis AutomatismAlertController::process()
void Automatism::handleAlerts(const AutomatismRuntimeContext& ctx) {
    const SensorReadings& readings = ctx.readings;
    const bool mailEnabled = _network.isEmailEnabled();

    // v11.162: Délai au démarrage pour éviter saturation queue mail.
    // Les mails d'alertes sont différés de 30 s ; la régulation chauffage (relais)
    // et la machine d'état trop-plein (inFlood → déverrouillage pompe) ne le sont PAS.
    const bool startupGracePeriod = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;

    // Phase 3 arbitrage mails : le serveur (CRON 1 min + alertes dérivées du POST,
    // n3_serveur 6.16.0) est l'émetteur PRIMAIRE des mails partagés de ce bloc
    // (aquarium bas, trop-plein, réserve basse, chauffage ON/OFF). Si l'échange
    // serveur est sain, l'ESP ne renvoie plus ces mails (fin des doublons) ; sinon
    // FAILOVER : évaluation locale bornée par l'anti-congestion du Mailer.
    //
    // Contrat critique : « se taire » = mails seulement.
    // - Chauffage : le serveur dérive les mails depuis `etatHeat` ; il ne pilote PAS
    //   le relais GPIO → HeaterOrchestrator toujours exécuté.
    // - Trop-plein : `inFlood` est lu par RefillOverfill pour autoriser Unlock de la
    //   pompe réserve. ExitFlood DOIT tourner même en liaison saine, sinon un flood
    //   notifié en failover fige inFlood=true et bloque le remplissage auto jusqu'au
    //   reboot. Seul le mail trop-plein est gaté (via mailEnabled=emitSharedAlertMails).
    // Remplissage pompe : déjà correct dans automatism_refill (mails gatés, pompe locale).
    // Nourrissage : non dérivable du POST → ESP primaire, non gaté.
    const bool serverCovers = serverCoversSharedAlerts();
    _mailer.setFailoverActive(!serverCovers);
    const bool emitSharedAlertMails = mailEnabled && !serverCovers && !startupGracePeriod;

    // Mails « aquarium bas » uniquement (pas d'actionneur).
    if (emitSharedAlertMails && SensorValidation::isWaterLevelKnown(readings.wlAqua)) {
        const uint16_t aqAlert = cmToMm(_network.getAqThresholdCm());
        const uint16_t aqClear = cmToMm(ThresholdUtil::subSat(_network.getAqThresholdCm(), 5));
        if (LevelAlertOrchestrator::run(_mailer, _lowAquaSent, readings.wlAqua, aqAlert, aqClear,
                                        mailEnabled, _network.getEmailAddress(),
                                        "Alerte - Niveau aquarium BAS", nullptr)) {
            armMailBlink();
        }
        if (readings.wlAqua <= aqClear) {
            _lowAquaSent = false;
        }
    }

    // Machine d'état trop-plein : TOUJOURS évaluée si niveau connu (clear inFlood /
    // horodatages). Le mail ESP n'est proposé que si emitSharedAlertMails.
    if (SensorValidation::isWaterLevelKnown(readings.wlAqua)) {
        const uint32_t nowEpoch = (uint32_t) _power.getCurrentEpochSafe();
        FloodAlert::State floodSt;
        floodSt.inFlood = inFlood;
        floodSt.floodEnterSinceEpoch = floodEnterSinceEpoch;
        floodSt.aboveResetSinceEpoch = aboveResetSinceEpoch;
        floodSt.lastFloodEmailEpoch = lastFloodEmailEpoch;
        FloodAlert::Params floodP;
        floodP.limFloodMm = cmToMm(_network.getLimFlood());
        floodP.resetThresholdMm = cmToMm(_network.getLimFlood() + floodHystCm);
        floodP.debounceSec = floodDebounceMin * 60UL;
        floodP.cooldownSec = floodCooldownMin * 60UL;
        floodP.resetStableSec = floodResetStableMin * 60UL;

        const FloodOrchestrator::Outcome floodOutcome = FloodOrchestrator::run(
            _mailer, floodSt, floodP, readings.wlAqua, nowEpoch, emitSharedAlertMails,
            _network.getEmailAddress(), (tankPumpLocked || _config.getPompeAquaLocked()),
            &inFlood);
        inFlood = floodSt.inFlood;
        floodEnterSinceEpoch = floodSt.floodEnterSinceEpoch;
        aboveResetSinceEpoch = floodSt.aboveResetSinceEpoch;
        lastFloodEmailEpoch = floodSt.lastFloodEmailEpoch;

        if (floodOutcome == FloodOrchestrator::Outcome::EmailQueued) {
            _highAquaSent = true;
            armMailBlink();
            g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, NVSKeys::Automatism::ALERT_FLOOD_LAST, lastFloodEmailEpoch);
            Serial.println(F("[Auto] Email TROP PLEIN ajouté à la queue (anti-spam actif)"));
        } else if (floodOutcome == FloodOrchestrator::Outcome::EmailFailed) {
            Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
        } else if (floodOutcome == FloodOrchestrator::Outcome::ExitedFlood) {
            _highAquaSent = false;
        }
    }

    // Mail « réserve basse » uniquement (pas d'actionneur ici — sécurité pompe ailleurs).
    if (emitSharedAlertMails) {
        const uint16_t tkAlert = cmToMm(_network.getTankThresholdCm());
        const uint16_t tkClear = cmToMm(ThresholdUtil::subSat(_network.getTankThresholdCm(), 5));
        if (LevelAlertOrchestrator::run(_mailer, _lowTankSent, readings.wlTank, tkAlert, tkClear,
                                        mailEnabled, _network.getEmailAddress(),
                                        "Alerte - Réserve BASSE", "Info - Réserve OK")) {
            armMailBlink();
        }
    }

    // C4: régulation chauffage — TOUJOURS locale (relais GPIO). Le serveur dérive
    // seulement le mail ON/OFF depuis `etatHeat` ; il ne commande pas le chauffage.
    // `emitSharedAlertMails` coupe le mail ESP en liaison saine / grâce boot, pas le relais.
    if (HeaterOrchestrator::run(_acts, _mailer, heaterPrevState,
                                readings.tempWater, _network.getHeaterThresholdC(),
                                /*hysteresisC=*/2.0f, emitSharedAlertMails,
                                _network.getEmailAddress())) {
        armMailBlink();
    }

    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
}

// Fusionné depuis AutomatismDisplayController
void Automatism::updateDisplayInternal(const AutomatismRuntimeContext& ctx) {
    const uint32_t providedNow = ctx.nowMs;
    const uint32_t currentMillis = providedNow == 0 ? millis() : providedNow;

    if (!_disp.isPresent()) return;
    if (_disp.isLocked()) return;

    // Gestion du splash screen (timeout de sécurité)
    if (_splashStartTime == 0) {
        // Déjà terminé
    } else if (currentMillis - _splashStartTime > DisplayConfig::SPLASH_DURATION_MS + 2000) {
        // Timeout de sécurité : 2 secondes après la durée normale
        _disp.forceEndSplash();
        _splashStartTime = 0;
    }

    // Basculement automatique d'écran
    if (_lastScreenSwitch == 0) {
        _lastScreenSwitch = currentMillis;
    } else if (currentMillis - _lastScreenSwitch >= DisplayConfig::SCREEN_SWITCH_INTERVAL_MS) {
        _oledToggle = !_oledToggle;
        _lastScreenSwitch = currentMillis;  // Reset à "now" pour éviter rafales après countdown/veille
        _disp.resetMainCache();
        _disp.resetStatusCache();
        _disp.resetVariablesCache();
    }

    // Intervalle de rafraîchissement
    bool hasCountdown = (_countdownEnd > 0 && currentMillis < _countdownEnd);
    bool hasFeedingPhase = (_currentFeedingPhase != FeedingPhase::NONE && currentMillis < _feedingPhaseEnd);
    bool isCountdownMode = hasCountdown || hasFeedingPhase;
    
    // Utiliser l'intervalle recommandé (250ms si countdown, sinon 80ms)
    const uint32_t displayInterval = isCountdownMode ? DisplayConfig::OLED_COUNTDOWN_INTERVAL_MS : DisplayConfig::OLED_INTERVAL_MS;
    const uint32_t delta = (currentMillis >= _lastOled) ? (currentMillis - _lastOled) : 0;
#if FEATURE_DIAG_OLED_LOGS
    const bool throttleBlock = (delta < displayInterval);
    Serial.printf("{\"h\":\"H1\",\"t\":%lu,\"last\":%lu,\"int\":%u,\"delta\":%u,\"block\":%d,\"cd\":%d,\"feed\":%d}\n",
                  (unsigned long)currentMillis, (unsigned long)_lastOled, (unsigned)displayInterval, (unsigned)delta,
                  throttleBlock ? 1 : 0, hasCountdown ? 1 : 0, hasFeedingPhase ? 1 : 0);
#endif
    if (currentMillis - _lastOled < displayInterval) {
        return;
    }
    
    _lastOled = currentMillis;

    SensorReadings readings = ctx.readings;

    // Validation basique des lectures pour affichage
    if (readings.tempWater < -50 || readings.tempWater > 100 ||
        readings.tempAir < -50 || readings.tempAir > 100 ||
        readings.humidity < 0 || readings.humidity > 100) {
        readings.tempWater = NAN;
        readings.tempAir = NAN;
        readings.humidity = NAN;
        readings.wlAqua = 0;
        readings.wlTank = 0;
        readings.wlPota = 0;
        readings.luminosite = 0;
    }

    // Calcul de la différence de marée
    int diffMaree = _lastDiffMaree;
    
    if (diffMaree == -1 && _lastReadings.wlAqua > 0) {
        if (readings.wlAqua > 0 && _lastReadings.wlAqua > 0) {
            diffMaree = static_cast<int>(readings.wlAqua) - static_cast<int>(_lastReadings.wlAqua);
        } else {
            diffMaree = 0;
        }
    } else if (diffMaree == -1) {
        diffMaree = 0;
    }
    
    // Calcul de la direction de marée
    int8_t tideDir = 0;
    if (diffMaree > 1) {
        tideDir = 1; // Montée
    } else if (diffMaree < -1) {
        tideDir = -1; // Descente
    } else {
        tideDir = 0; // Stable
    }
    
    // Affichage du countdown si actif
    if (hasCountdown && strlen(_countdownLabel) > 0) {
#if FEATURE_DIAG_OLED_LOGS
        Serial.printf("{\"h\":\"H4\",\"drew\":\"countdown\",\"t\":%lu}\n", (unsigned long)currentMillis);
#endif
        uint32_t secLeft = (_countdownEnd > currentMillis) ? 
                          ((_countdownEnd - currentMillis) / 1000) : 0;
        bool isManual = _manualFeedingActive;
        _disp.showCountdown(_countdownLabel, secLeft, isManual);
        bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _disp.drawStatusEx(sendState, recvState, rssi, mailBlink, tideDir, diffMaree, false);
        _disp.flush();
        return;
    }
    
    // Affichage du countdown de nourrissage si une phase est active
    if (hasFeedingPhase) {
#if FEATURE_DIAG_OLED_LOGS
        Serial.printf("{\"h\":\"H4\",\"drew\":\"feeding\",\"t\":%lu}\n", (unsigned long)currentMillis);
#endif
        const char* fishType = "Auto";
        uint32_t secLeft = (_feedingPhaseEnd > currentMillis) ? 
                          ((_feedingPhaseEnd - currentMillis) / 1000) : 0;
        
        if (_currentFeedingType != nullptr) {
            fishType = _currentFeedingType;
        } else if (!_manualFeedingActive) {
            const uint32_t bigCycleMs = FeedingTiming::cycleDurationMs(tempsGros);
            const uint32_t delayMs = 2 * 1000UL;
            const uint32_t bigPhaseTotalMs = bigCycleMs + delayMs;
            const uint32_t smallCycleMs = FeedingTiming::cycleDurationMs(tempsPetits);
            const uint32_t totalFeedingMs = bigPhaseTotalMs + smallCycleMs;
            
            // v14.02 (m2): clamp anti-underflow. Si tempsGros/Petits changent en plein
            // cycle (config distante), (_feedingPhaseEnd - now) peut dépasser le total
            // recalculé -> soustraction unsigned négative. On borne à [0, totalFeedingMs].
            uint32_t elapsedMs;
            if (_feedingPhaseEnd > currentMillis) {
                const uint32_t remainingMs = _feedingPhaseEnd - currentMillis;
                elapsedMs = (remainingMs <= totalFeedingMs) ? (totalFeedingMs - remainingMs) : 0;
            } else {
                elapsedMs = totalFeedingMs;
            }

            if (elapsedMs < bigPhaseTotalMs) {
                fishType = "Gros";
            } else {
                fishType = "Petits";
            }
        }
        
        const char* phase = (_currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) 
                          ? "Avant" : "Retour";
        bool isManual = _manualFeedingActive;
        _disp.showFeedingCountdown(fishType, phase, secLeft, isManual);
        bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _disp.drawStatusEx(sendState, recvState, rssi, mailBlink, tideDir, diffMaree, false);
        _disp.flush();
        return;
    }
    
    // Affichage normal (écran principal ou écran variables)
    char timeStr[64];
    _power.getCurrentTimeString(timeStr, sizeof(timeStr));
    bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
    int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
    
    // Basculement entre écran principal et écran variables
#if FEATURE_DIAG_OLED_LOGS
    Serial.printf("{\"h\":\"H2\",\"drew\":\"%s\",\"t\":%lu}\n", _oledToggle ? "main" : "vars", (unsigned long)currentMillis);
#endif
    if (_oledToggle) {
        // Écran principal avec données des capteurs
        _disp.showMain(
            readings.tempWater,
            readings.tempAir,
            readings.humidity,
            readings.wlAqua,
            readings.wlTank,
            readings.wlPota,
            readings.luminosite,
            timeStr
        );
    } else {
        // Écran variables avec états des actionneurs
        _disp.showVariables(
            _acts.isAquaPumpRunning(),
            _acts.isTankPumpRunning(),
            _acts.isHeaterOn(),
            _acts.isLightOn(),
            bouffeMatin,
            bouffeMidi,
            bouffeSoir,
            tempsPetits,
            tempsGros,
            _network.getAqThresholdCm(),
            _network.getTankThresholdCm(),
            _network.getHeaterThresholdC(),
            _network.getLimFlood()
        );
    }
#if FEATURE_DIAG_OLED_LOGS
    Serial.printf("{\"ts\":%lu,\"loc\":\"automatism.cpp:updateDisplay\",\"msg\":\"after showMain/showVar before drawStatus\",\"hypothesisId\":\"E\"}\n", millis());
#endif
    // Toujours afficher la barre d'état (en overlay)
    _disp.drawStatus(sendState, recvState, rssi, mailBlink, tideDir, diffMaree);
    // Envoyer le buffer à l'écran : drawStatus modifie la ligne 0 mais ne fait pas flush quand _updateMode est false
    _disp.flush();
#if FEATURE_DIAG_OLED_LOGS
    Serial.printf("{\"ts\":%lu,\"loc\":\"automatism.cpp:updateDisplay\",\"msg\":\"after drawStatus+flush\",\"hypothesisId\":\"A\"}\n", millis());
#endif
}

// ============================================================================
// MÉTHODES DE PERSISTANCE (fusionnées depuis AutomatismPersistence)
// ============================================================================

// Snapshots sleep/wake
// v11.178: Utilisation des clés NVS centralisées (audit nvs-keys)
void Automatism::saveActuatorSnapshotToNVS(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn) {
    NVSError ePending = g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_PENDING, true);
    NVSError eAqua = g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_AQUA, pumpAquaWasOn);
    NVSError eHeat = g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_HEATER, heaterWasOn);
    NVSError eLight = g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_LIGHT, lightWasOn);

    Serial.printf("[Auto] Snapshot actionneurs NVS: aqua=%s heater=%s light=%s\n",
                  pumpAquaWasOn ? "ON" : "OFF", heaterWasOn ? "ON" : "OFF", lightWasOn ? "ON" : "OFF");
    if (ePending != NVSError::SUCCESS || eAqua != NVSError::SUCCESS ||
        eHeat != NVSError::SUCCESS || eLight != NVSError::SUCCESS) {
      Serial.printf("[Auto] ATTENTION: écriture snapshot NVS incomplète (codes: pend=%d aqua=%d heat=%d light=%d)\n",
                    static_cast<int>(ePending), static_cast<int>(eAqua),
                    static_cast<int>(eHeat), static_cast<int>(eLight));
    }
}

bool Automatism::loadActuatorSnapshotFromNVS(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn) {
    bool pending;
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_PENDING, pending, false);
    
    if (!pending) {
        Serial.println(F("[Auto] Pas de snapshot NVS (snap_pending absent ou false) — restauration actionneurs après veille ignorée"));
        return false;
    }
    
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_AQUA, pumpAquaWasOn, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_HEATER, heaterWasOn, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_LIGHT, lightWasOn, false);
    
    Serial.printf("[Auto] Snapshot chargé depuis NVS: aqua=%s heater=%s light=%s\n",
                  pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
    
    return true;
}

void Automatism::clearActuatorSnapshotInNVS() {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Automatism::SNAP_PENDING, false);
    Serial.println("[Auto] Snapshot actionneurs effacé");
}

void Automatism::prepareActuatorsForSleep(IActuators& acts) {
    const ActuatorSnapshot::State snap = ActuatorSnapshot::capture(acts);
    saveActuatorSnapshotToNVS(snap.aqua, snap.heater, snap.light);
    ActuatorSnapshot::stopAll(acts);
    Serial.printf("[Auto] Snapshot avant veille: aqua=%s heater=%s light=%s - actionneurs coupés\n",
                  snap.aqua ? "ON" : "OFF", snap.heater ? "ON" : "OFF", snap.light ? "ON" : "OFF");
}
