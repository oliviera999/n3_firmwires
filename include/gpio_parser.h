#pragma once
#include <ArduinoJson.h>
#include "gpio_mapping.h"

class Automatism; // Forward declaration

/**
 * GPIO Parser - Parsing Unifié des Commandes Serveur Distant
 * 
 * Responsabilité: Parse JSON GPIO numériques et applique les changements
 * Version: 11.68
 */
class GPIOParser {
public:
    // Parse JSON du serveur distant et applique les changements
    static void parseAndApply(const JsonDocument& doc, Automatism& autoCtrl);
    
private:
    // Applique un GPIO selon son type
    static void applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl);
    
    // Sauvegarde dans NVS
    static void saveToNVS(const GPIOMapping& mapping, JsonVariantConst value);
    
    // Helpers conversion
    static bool parseBool(JsonVariantConst v);
    static int parseInt(JsonVariantConst v);
    static float parseFloat(JsonVariantConst v);
    static String parseString(JsonVariantConst v);
};
