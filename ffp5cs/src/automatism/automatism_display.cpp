// automatism/automatism_display.cpp — alertes, rendu d'affichage et snapshot NVS
// d'Automatism (handleAlerts, updateDisplayInternal, saveActuatorSnapshotToNVS).
// Extrait de automatism.cpp (audit : découpe). Méthodes membres, comportement identique.
#include "automatism.h"
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
static void formatDistanceAlert(char* buffer, size_t bufferSize, const char* prefix,
                                float distance, const char* suffix, float threshold) {
  snprintf(buffer, bufferSize, "%s%.1f mm (%s%.1f)", prefix, distance, suffix, threshold);
}
static void formatTemperatureAlert(char* buffer, size_t bufferSize, const char* prefix, float temp) {
  snprintf(buffer, bufferSize, "%s%.1f°C", prefix, temp);
}
static uint16_t cmToMm(uint16_t cm) { return SensorConfig::Ultrasonic::cmToMm(cm); }
}  // namespace


void Automatism::handleAlerts(const AutomatismRuntimeContext& ctx) {
    const SensorReadings& readings = ctx.readings;
    const bool mailEnabled = _network.isEmailEnabled();
    
    // v11.162: Délai au démarrage pour éviter saturation queue mail
    // Les alertes non-critiques sont différées de 30s après le boot
    const bool startupGracePeriod = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
    if (startupGracePeriod && mailEnabled) {
        // Pendant la période de grâce, on n'envoie pas mais on ne marque pas comme déjà envoyé :
        // après 30s la première évaluation enverra l'alerte si la condition est toujours vraie.
        return;  // Pas d'alertes pendant la période de grâce
    }

    if (readings.wlAqua > cmToMm(_network.getAqThresholdCm()) && !_lowAquaSent && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlAqua, " mm (> ", cmToMm(_network.getAqThresholdCm()));
        _mailer.sendAlert("Alerte - Niveau aquarium BAS", msgBuffer, _network.getEmailAddress());
        _lowAquaSent = true;
        armMailBlink();
    }

    if (readings.wlAqua <= cmToMm((_network.getAqThresholdCm() > 5) ? (_network.getAqThresholdCm() - 5) : 0)) {
        _lowAquaSent = false;
    }

    time_t nowEpoch = _power.getCurrentEpochSafe();
    if (readings.wlAqua < cmToMm(_network.getLimFlood())) {
        if (floodEnterSinceEpoch == 0) {
            floodEnterSinceEpoch = nowEpoch;
        }
        aboveResetSinceEpoch = 0;
        if (!inFlood && mailEnabled) {
            uint32_t elapsedUnder = (nowEpoch >= floodEnterSinceEpoch) ? (nowEpoch - floodEnterSinceEpoch) : 0;
            bool debounceOk = elapsedUnder >= (floodDebounceMin * 60UL);
            bool cooldownOk = (lastFloodEmailEpoch == 0) || ((nowEpoch - lastFloodEmailEpoch) >= (floodCooldownMin * 60UL));
            if (debounceOk && cooldownOk) {
                char msgBuffer[128];
                formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlAqua, " mm (< ", cmToMm(_network.getLimFlood()));
                if (tankPumpLocked || _config.getPompeAquaLocked()) {
                    strncat(msgBuffer, " / Pompe verrouillée", sizeof(msgBuffer) - strlen(msgBuffer) - 1);
                }
                bool sent = _mailer.sendAlert("Alerte - Aquarium TROP PLEIN", msgBuffer, _network.getEmailAddress());
                if (sent) {
                    inFlood = true;
                    _highAquaSent = true;
                    armMailBlink();
                    lastFloodEmailEpoch = nowEpoch;
                    g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, NVSKeys::Automatism::ALERT_FLOOD_LAST, lastFloodEmailEpoch);
                    Serial.println(F("[Auto] Email TROP PLEIN ajouté à la queue (anti-spam actif)"));
                } else {
                    Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
                }
            }
        }
    } else {
        floodEnterSinceEpoch = 0;
        if (readings.wlAqua >= cmToMm(_network.getLimFlood() + floodHystCm)) {
            if (aboveResetSinceEpoch == 0) {
                aboveResetSinceEpoch = nowEpoch;
            }
            uint32_t elapsedAbove = (nowEpoch >= aboveResetSinceEpoch) ? (nowEpoch - aboveResetSinceEpoch) : 0;
            if (elapsedAbove >= (floodResetStableMin * 60UL)) {
                inFlood = false;
                _highAquaSent = false;
            }
        } else {
            aboveResetSinceEpoch = 0;
        }
    }

    if (readings.wlTank > cmToMm(_network.getTankThresholdCm()) && !_lowTankSent && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlTank, " mm (> ", cmToMm(_network.getTankThresholdCm()));
        _mailer.sendAlert("Alerte - Réserve BASSE", msgBuffer, _network.getEmailAddress());
        _lowTankSent = true;
        armMailBlink();
    } else if (_lowTankSent && readings.wlTank <= cmToMm((_network.getTankThresholdCm() > 5) ? (_network.getTankThresholdCm() - 5) : 0) && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlTank, " mm (<= ",
                            cmToMm((_network.getTankThresholdCm() > 5) ? (_network.getTankThresholdCm() - 5) : 0));
        _mailer.sendAlert("Info - Réserve OK", msgBuffer, _network.getEmailAddress());
        _lowTankSent = false;
        armMailBlink();
    }

    if (readings.tempWater < _network.getHeaterThresholdC() && !heaterPrevState) {
        _acts.startHeater();
        heaterPrevState = true;
        if (mailEnabled) {
            char msgBuffer[64];
            formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", readings.tempWater);
            _mailer.sendAlert("Chauffage ON", msgBuffer, _network.getEmailAddress());
            armMailBlink();
        }
    } else if (readings.tempWater > _network.getHeaterThresholdC() + 2 && heaterPrevState) {
        _acts.stopHeater();
        heaterPrevState = false;
        if (mailEnabled) {
            char msgBuffer[64];
            formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", readings.tempWater);
            _mailer.sendAlert("Chauffage OFF", msgBuffer, _network.getEmailAddress());
            armMailBlink();
        }
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
            const uint32_t bigCycleMs = (tempsGros + (tempsGros / 2U)) * 1000UL;
            const uint32_t delayMs = 2 * 1000UL;
            const uint32_t bigPhaseTotalMs = bigCycleMs + delayMs;
            const uint32_t smallCycleMs = (tempsPetits + (tempsPetits / 2U)) * 1000UL;
            const uint32_t totalFeedingMs = bigPhaseTotalMs + smallCycleMs;
            
            uint32_t elapsedMs = (_feedingPhaseEnd > currentMillis) ? 
                                 (totalFeedingMs - ((_feedingPhaseEnd - currentMillis))) : totalFeedingMs;
            
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

void Automatism::prepareActuatorsForSleep(SystemActuators& acts) {
    bool aquaOn = acts.isAquaPumpRunning();
    bool heaterOn = acts.isHeaterOn();
    bool lightOn = acts.isLightOn();
    saveActuatorSnapshotToNVS(aquaOn, heaterOn, lightOn);
    acts.stopAquaPump();
    acts.stopHeater();
    acts.stopLight();
    Serial.printf("[Auto] Snapshot avant veille: aqua=%s heater=%s light=%s - actionneurs coupés\n",
                  aquaOn ? "ON" : "OFF", heaterOn ? "ON" : "OFF", lightOn ? "ON" : "OFF");
}

void Automatism::restoreActuatorsAfterWake(SystemActuators& acts) {
    bool restoreAqua, restoreHeater, restoreLight;
    if (!loadActuatorSnapshotFromNVS(restoreAqua, restoreHeater, restoreLight)) {
        return;
    }
    if (restoreAqua) acts.startAquaPump();
    if (restoreHeater) acts.startHeater();
    if (restoreLight) acts.startLight();
    clearActuatorSnapshotInNVS();
    Serial.printf("[Auto] État restauré au réveil: aqua=%s heater=%s light=%s\n",
                  restoreAqua ? "ON" : "OFF", restoreHeater ? "ON" : "OFF", restoreLight ? "ON" : "OFF");
}

// États actuels persistants (méthodes statiques pour compatibilité avec web_server.cpp)
// v11.178: Utilisation des clés NVS centralisées pour state_lastLocal (audit nvs-keys)
void Automatism::saveCurrentActuatorState(const char* actuator, bool state) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);  // Dynamique basé sur nom actionneur
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key, state);
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Automatism::STATE_LAST_LOCAL, millis());
    Serial.printf("[Auto] État %s=%s sauvegardé en NVS (priorité locale)\n",
                   actuator, state ? "ON" : "OFF");
}

bool Automatism::loadCurrentActuatorState(const char* actuator, bool defaultValue) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);  // Dynamique basé sur nom actionneur
    bool state;
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, key, state, defaultValue);
    return state;
}

uint32_t Automatism::getLastLocalActionTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, NVSKeys::Automatism::STATE_LAST_LOCAL, timestamp, 0);
    return timestamp;
}

bool Automatism::hasRecentLocalAction(uint32_t timeoutMs) {
    uint32_t lastAction = getLastLocalActionTime();
    if (lastAction == 0) return false;
    uint32_t elapsed = millis() - lastAction;
    return elapsed < timeoutMs;
}

// Pending sync
// v11.178: Utilisation des clés NVS centralisées (audit nvs-keys)
void Automatism::markPendingSync(const char* actuator, bool state) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key_state, state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, actuator) == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, actuator);
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, millis());
    Serial.printf("[Auto] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                   actuator, state ? "ON" : "OFF", count);
}

void Automatism::markConfigPendingSync() {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Sync::CONFIG, true);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, "config") == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, "config");
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, millis());
    Serial.printf("[Auto] ⏳ Config pending sync marquée (total: %u)\n", count);
}

void Automatism::clearPendingSync(const char* actuator) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key_state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, actuator) != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[24];
                snprintf(newKey, sizeof(newKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, newCount);
    Serial.printf("[Auto] ✅ Pending sync effacé: %s (reste: %u)\n", actuator, newCount);
}

void Automatism::clearConfigPendingSync() {
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, NVSKeys::Sync::CONFIG);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, "config") != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[24];
                snprintf(newKey, sizeof(newKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, newCount);
    Serial.printf("[Auto] ✅ Config pending sync effacée (reste: %u)\n", newCount);
}

bool Automatism::hasPendingSync() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    return count > 0;
}

uint8_t Automatism::getPendingSyncCount() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    return (uint8_t)count;
}

uint32_t Automatism::getLastPendingSyncTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, timestamp, 0);
    return timestamp;
}
