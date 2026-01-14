#include "automatism/automatism_display_controller.h"
#include "automatism/automatism_utils.h"
#include "automatism.h" // Pour l'accès aux états de Automatism
#include <WiFi.h>

using namespace AutomatismUtils;

AutomatismDisplayController::AutomatismDisplayController(DisplayView& display, PowerManager& power)
    : _display(display)
    , _power(power)
    , _oledToggle(true)
    , _lastOled(0)
    , _lastScreenSwitch(0)
    , _splashStartTime(0)
    , _lastDiffMaree(-1)
{}

void AutomatismDisplayController::begin() {
    // Initialisation
    _lastOled = 0;
    _lastScreenSwitch = 0;
    _splashStartTime = millis();
}

void AutomatismDisplayController::toggleScreen() {
    _oledToggle = !_oledToggle;
}

void AutomatismDisplayController::resetCache() {
    _display.resetMainCache();
    _display.resetStatusCache();
    _display.resetVariablesCache();
}

uint32_t AutomatismDisplayController::getRecommendedDisplayIntervalMs(uint32_t nowMs) const {
    // CORRECTION v11.120: Utiliser OLED_INTERVAL_MS (80ms) au lieu de 1000ms
    // Cette méthode retourne l'intervalle par défaut pour l'affichage
    // L'ajustement pour les countdowns (250ms) se fait dans updateDisplay()
    return OLED_INTERVAL_MS;
}

void AutomatismDisplayController::updateDisplay(const AutomatismRuntimeContext& ctx, 
                                                SystemActuators& acts, 
                                                Automatism& core) {
    const uint32_t providedNow = ctx.nowMs;
    const uint32_t currentMillis = providedNow == 0 ? millis() : providedNow;

    if (!_display.isPresent()) return;
    if (_display.isLocked()) return;

    // Gestion du splash screen (timeout de sécurité)
    if (_splashStartTime == 0) {
        // Déjà terminé
    } else if (currentMillis - _splashStartTime > DisplayConfig::SPLASH_DURATION_MS + 2000) {
        // Timeout de sécurité : 2 secondes après la durée normale
        _display.forceEndSplash();
        _splashStartTime = 0;
    }

    // Basculement automatique d'écran
    if (_lastScreenSwitch == 0) {
        _lastScreenSwitch = currentMillis;
    } else if (currentMillis - _lastScreenSwitch >= DisplayConfig::SCREEN_SWITCH_INTERVAL_MS) {
        _oledToggle = !_oledToggle;
        _lastScreenSwitch += DisplayConfig::SCREEN_SWITCH_INTERVAL_MS;
        resetCache();
    }

    // Intervalle de rafraîchissement
    // CORRECTION v11.120: Vérifier si un countdown est actif pour ajuster l'intervalle
    bool hasCountdown = (core._countdownEnd > 0 && currentMillis < core._countdownEnd);
    bool hasFeedingPhase = (core._currentFeedingPhase != Automatism::FeedingPhase::NONE &&
                            currentMillis < core._feedingPhaseEnd);
    bool isCountdownMode = hasCountdown || hasFeedingPhase;
    
    // Utiliser l'intervalle recommandé (250ms si countdown, sinon OLED_INTERVAL_MS=80ms)
    const uint32_t displayInterval = isCountdownMode ? 250u : OLED_INTERVAL_MS;
    
    if (currentMillis - _lastOled < displayInterval) {
        return;
    }
    
    _lastOled = currentMillis;

    static unsigned long lastDebugLog = 0;
    if (currentMillis - lastDebugLog >= 10000) {
        // Serial.printf("[Display] updateDisplay appelée - OLED présent: %s\n", _display.isPresent() ? "OUI" : "NON");
        lastDebugLog = currentMillis;
    }

    SensorReadings readings = ctx.readings;

    // Validation basique des lectures pour affichage
    if (readings.tempWater < -50 || readings.tempWater > 100 ||
        readings.tempAir < -50 || readings.tempAir > 100 ||
        readings.humidity < 0 || readings.humidity > 100) {
        // Serial.println(F("[Display] Erreur lecture capteurs, utilisation valeurs par défaut"));
        readings.tempWater = NAN;
        readings.tempAir = NAN;
        readings.humidity = NAN;
        readings.wlAqua = 0;
        readings.wlTank = 0;
        readings.wlPota = 0;
        readings.luminosite = 0;
    }

    // CORRECTION v11.120: Implémentation complète de l'affichage OLED
    // Calcul de la différence de marée depuis Automatism (stocké dans _lastDiffMaree)
    // Accès aux membres privés via friend class
    int diffMaree = core._lastDiffMaree;
    
    // Si _lastDiffMaree n'est pas initialisé (-1), calculer la différence manuellement
    // en utilisant _lastReadings stocké dans Automatism
    if (diffMaree == -1 && core._lastReadings.wlAqua > 0) {
        // Calcul simple: différence entre le niveau actuel et le niveau précédent
        if (readings.wlAqua > 0 && core._lastReadings.wlAqua > 0) {
            diffMaree = static_cast<int>(readings.wlAqua) - static_cast<int>(core._lastReadings.wlAqua);
        } else {
            diffMaree = 0;
        }
    } else if (diffMaree == -1) {
        // Aucune valeur précédente, utiliser 0 par défaut
        diffMaree = 0;
    }
    
    // Calcul de la direction de marée (tideDir) : -1 = descente, 0 = stable, 1 = montée
    int8_t tideDir = 0;
    if (diffMaree > 1) {
        tideDir = 1; // Montée
    } else if (diffMaree < -1) {
        tideDir = -1; // Descente
    } else {
        tideDir = 0; // Stable
    }
    
    // Mise à jour de _lastDiffMaree local (pour tracking)
    _lastDiffMaree = diffMaree;
    
    // Affichage du countdown si actif
    if (hasCountdown && core._countdownLabel.length() > 0) {
        uint32_t secLeft = (core._countdownEnd > currentMillis) ? 
                          ((core._countdownEnd - currentMillis) / 1000) : 0;
        bool isManual = core._manualFeedingActive;
        _display.showCountdown(core._countdownLabel.c_str(), secLeft, isManual);
        // Afficher aussi la barre d'état
        bool mailBlink = (core.mailBlinkUntil > 0 && currentMillis < core.mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _display.drawStatusEx(core.sendState, core.recvState, rssi, mailBlink, tideDir, diffMaree, false);
        return;
    }
    
    // Affichage du countdown de nourrissage si une phase est active
    if (hasFeedingPhase) {
        const char* fishType = "Auto"; // Par défaut
        uint32_t secLeft = (core._feedingPhaseEnd > currentMillis) ? 
                          ((core._feedingPhaseEnd - currentMillis) / 1000) : 0;
        
        // Si _currentFeedingType est défini, l'utiliser
        if (core._currentFeedingType != nullptr) {
            fishType = core._currentFeedingType;
        } else if (!core._manualFeedingActive) {
            // Pour le nourrissage automatique séquentiel: déterminer la phase en fonction de la durée restante
            // Durée phase gros = tempsGros + (tempsGros / 2) + délai (2s)
            const uint32_t bigCycleMs = (core.tempsGros + (core.tempsGros / 2U)) * 1000UL;
            const uint32_t delayMs = 2 * 1000UL;
            const uint32_t bigPhaseTotalMs = bigCycleMs + delayMs;
            const uint32_t smallCycleMs = (core.tempsPetits + (core.tempsPetits / 2U)) * 1000UL;
            const uint32_t totalFeedingMs = bigPhaseTotalMs + smallCycleMs;
            
            // Calculer le temps écoulé depuis le début
            uint32_t elapsedMs = (core._feedingPhaseEnd > currentMillis) ? 
                                 (totalFeedingMs - ((core._feedingPhaseEnd - currentMillis))) : totalFeedingMs;
            
            // Si on est dans la première partie (gros), utiliser "Gros"
            if (elapsedMs < bigPhaseTotalMs) {
                fishType = "Gros";
            } else {
                fishType = "Petits";
            }
        }
        
        const char* phase = (core._currentFeedingPhase == Automatism::FeedingPhase::FEEDING_FORWARD) 
                          ? "Avant" : "Retour";
        bool isManual = core._manualFeedingActive;
        _display.showFeedingCountdown(fishType, phase, secLeft, isManual);
        // Afficher aussi la barre d'état
        bool mailBlink = (core.mailBlinkUntil > 0 && currentMillis < core.mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _display.drawStatusEx(core.sendState, core.recvState, rssi, mailBlink, tideDir, diffMaree, false);
        return;
    }
    
    // Affichage normal (écran principal ou écran variables)
    // CORRECTION v11.120: Utiliser _power directement car getCurrentTimeString() n'est pas const
    String timeStr = _power.getCurrentTimeString();
    bool mailBlink = (core.mailBlinkUntil > 0 && currentMillis < core.mailBlinkUntil);
    int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
    
    // Basculement entre écran principal et écran variables
    if (_oledToggle) {
        // Écran principal avec données des capteurs
        _display.showMain(
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
        _display.showVariables(
            acts.isAquaPumpRunning(),
            acts.isTankPumpRunning(),
            acts.isHeaterOn(),
            acts.isLightOn(),
            core.bouffeMatin,
            core.bouffeMidi,
            core.bouffeSoir,
            core.tempsPetits,
            core.tempsGros,
            core.aqThresholdCm,
            core.tankThresholdCm,
            core.heaterThresholdC,
            core.limFlood
        );
    }
    
    // Toujours afficher la barre d'état (en overlay)
    _display.drawStatus(core.sendState, core.recvState, rssi, mailBlink, tideDir, diffMaree);
}
