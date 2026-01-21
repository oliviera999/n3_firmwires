#pragma once
#include <ArduinoJson.h>
#include "gpio_mapping.h"
#include "nvs_manager.h" // v11.108

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
    
    // Charge les états GPIO actionneurs depuis NVS au démarrage
    static void loadGPIOStatesFromNVS(Automatism& autoCtrl);
    
private:
    // Applique un GPIO selon son type
    static void applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl, 
                         JsonDocument& configDoc, bool& hasVirtualConfig);
    
    // Sauvegarde dans NVS
    static void saveToNVS(const GPIOMapping& mapping, JsonVariantConst value);
    
    // Mapper GPIO vers clés de configuration (retourne nullptr si non mappé)
    static const char* mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value);
    
    // Helpers conversion
    static bool parseBool(JsonVariantConst v);
    static int parseInt(JsonVariantConst v);
    static float parseFloat(JsonVariantConst v);
    // Parse string et écrit dans le buffer fourni, retourne la taille écrite
    static size_t parseString(JsonVariantConst v, char* buffer, size_t bufferSize);
};
