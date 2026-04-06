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
    
    // v11.179: Réinitialise l'état de détection de front (appeler au boot)
    static void resetEdgeDetectionState();

    // Initialise l'état nourrissage depuis le doc serveur (1er poll) sans déclencher
    static void seedFeedStateFromDoc(const JsonDocument& doc);

    /// Après POST local (sync auto/manuel), aligner l’état edge 108/109 pour éviter un faux 0→1 au poll suivant.
    static void syncFeedEdgeStateAfterLocalPost(bool smallLevel, bool bigLevel);

private:
    // Applique un GPIO selon son type
    static void applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl, 
                         JsonDocument& configDoc, bool& hasVirtualConfig);
    
    // Sauvegarde dans NVS
    static void saveToNVS(const GPIOMapping& mapping, JsonVariantConst value);
    
    // Mapper GPIO vers clés de configuration (retourne nullptr si non mappé)
    static const char* mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value);
    
    // Helper conversion bool (gère strings "true"/"1"/"on")
    static inline bool parseBool(JsonVariantConst v) {
        if (v.is<bool>()) return v.as<bool>();
        if (v.is<int>()) return v.as<int>() == 1;
        if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            // Comparaison case-insensitive
            if (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || 
                strcasecmp(s, "on") == 0 || strcasecmp(s, "checked") == 0) {
                return true;
            }
        }
        return false;
    }
};
