#pragma once

#include "display_view.h"
#include "power.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "automatism/automatism_state.h"

// Forward declaration
class Automatism;

class AutomatismDisplayController {
public:
    AutomatismDisplayController(DisplayView& display, PowerManager& power);

    // Initialisation
    void begin();

    // Méthode principale appelée par Automatism::updateDisplay()
    void updateDisplay(const AutomatismRuntimeContext& ctx, 
                       SystemActuators& acts, 
                       const Automatism& core);

    // Recommande l'intervalle d'update OLED en fonction de l'état courant
    uint32_t getRecommendedDisplayIntervalMs(uint32_t nowMs) const;

    // Gestion de l'état d'affichage
    void toggleScreen();
    void resetCache();
    bool isPresent() const { return _display.isPresent(); }
    void forceEndSplash() { _display.forceEndSplash(); }

private:
    DisplayView& _display;
    PowerManager& _power;

    // État interne d'affichage
    bool _oledToggle;           // true=Principal, false=Vars
    unsigned long _lastOled;    // Dernier update
    unsigned long _lastScreenSwitch;
    unsigned long _splashStartTime;
    int _lastDiffMaree;

    static constexpr unsigned long SCREEN_SWITCH_INTERVAL_MS = 4000;
    static constexpr unsigned long OLED_INTERVAL_MS = 80;
};
