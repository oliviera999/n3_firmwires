#pragma once
#include "gpio_mapping.h"
#include <Arduino.h>

/**
 * GPIO Notifier - POST Instantané des Changements
 * 
 * Responsabilité: Envoie immédiatement les changements GPIO au serveur distant
 * Version: 11.68
 */
class GPIONotifier {
public:
    // Notifie changement GPIO au serveur distant
    static bool notifyChange(uint8_t gpio, const char* value);
    
    // Notifie changement actionneur (helper)
    static bool notifyActuator(uint8_t gpio, bool state);
    
    // Notifie changement config (helper)
    static bool notifyConfig(uint8_t gpio, int value);
    static bool notifyConfig(uint8_t gpio, float value);
    static bool notifyConfig(uint8_t gpio, const char* value);
};
