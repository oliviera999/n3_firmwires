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
}

uint32_t AutomatismDisplayController::getRecommendedDisplayIntervalMs(uint32_t nowMs) const {
    // Si countdown actif, rafraîchissement rapide (250ms), sinon normal (1000ms)
    // Note: La détection du countdown se fait via le contexte ou l'état interne
    // Pour l'instant on garde une valeur par défaut safe
    return 1000u; 
    
    // TODO: Passer l'état isCountdownMode en paramètre pour plus de précision
}

void AutomatismDisplayController::updateDisplay(const AutomatismRuntimeContext& ctx, 
                                                SystemActuators& acts, 
                                                const Automatism& core) {
    const uint32_t providedNow = ctx.nowMs;
    const uint32_t currentMillis = providedNow == 0 ? millis() : providedNow;

    if (!_display.isPresent()) return;
    if (_display.isLocked()) return;

    // Gestion du splash screen
    if (_splashStartTime == 0) {
        // Déjà terminé
    } else if (currentMillis - _splashStartTime > 5000) {
        _display.forceEndSplash();
        _splashStartTime = 0;
    }

    // Basculement automatique d'écran
    if (_lastScreenSwitch == 0) {
        _lastScreenSwitch = currentMillis;
    } else if (currentMillis - _lastScreenSwitch >= SCREEN_SWITCH_INTERVAL_MS) {
        _oledToggle = !_oledToggle;
        _lastScreenSwitch += SCREEN_SWITCH_INTERVAL_MS;
        resetCache();
    }

    // Intervalle de rafraîchissement
    // On force un rafraîchissement rapide si countdown ou animation
    // TODO: Récupérer isCountdownMode depuis core
    bool isCountdownMode = false; // Placeholder
    
    const uint32_t displayInterval = isCountdownMode ? 250u : 1000u;
    
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

    // Logique d'affichage complexe déléguée à Automatism::updateDisplayInternal pour l'instant
    // car elle dépend de trop de variables privées de Automatism (countdown, phases, etc.)
    // En attendant la migration complète en Phase 2.8, on appelle la méthode interne de core.
    // Mais on prépare la structure ici.
    
    // core.updateDisplayInternal(ctx); // Appel temporaire à l'ancienne logique
    
    // TODO: Migrer ici toute la logique de showMain/showServerVars/showCountdown
    // Cela nécessite d'exposer beaucoup de getters sur Automatism
}
