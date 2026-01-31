#pragma once
#include "pins.h"
#include <cstdint>
#include <cstddef>
#include <cstring>  // v11.172: pour strcmp dans les helpers

/**
 * Variable Registry - Source Unique de Vérité
 * 
 * v11.172: Extension du GPIO Mapping pour centraliser TOUS les noms de variables:
 * - GPIO numérique (serveur GET): "104"
 * - Nom serveur POST: "chauffageThreshold"
 * - Nom firmware interne: "heaterThreshold"
 * - Clé NVS: "heatThr"
 * 
 * Synchronisé avec:
 * - ffp3/src/Controller/OutputController.php (GPIO GET)
 * - ffp3/src/Controller/PostDataController.php (POST names)
 * - docs/technical/VARIABLE_NAMING.md (documentation)
 * 
 * Version: 11.172
 */

// Type de variable pour validation et sérialisation
enum class GPIOType {
    ACTUATOR,      // GPIO physiques (relais, LED) - bool
    CONFIG_INT,    // Paramètres entiers
    CONFIG_FLOAT,  // Paramètres flottants
    CONFIG_STRING, // Paramètres texte (email)
    CONFIG_BOOL    // Paramètres booléens
};

// Valeur par défaut (union pour différents types)
union DefaultValue {
    int intVal;
    float floatVal;
    bool boolVal;
    const char* strVal;
    
    constexpr DefaultValue(int v) : intVal(v) {}
    constexpr DefaultValue(float v) : floatVal(v) {}
    constexpr DefaultValue(bool v) : boolVal(v) {}
    constexpr DefaultValue(const char* v) : strVal(v) {}
};

// Configuration variable complète - Source de vérité unique
struct GPIOMapping {
    uint8_t gpio;              // Numéro GPIO (GET serveur)
    GPIOType type;             // Type de donnée
    const char* nvsKey;        // Clé NVS (max 15 chars)
    const char* serverPostName;// Nom dans POST serveur (ex: "chauffageThreshold")
    const char* internalName;  // Nom firmware (ex: "heaterThreshold")  
    const char* description;   // Description debug
    DefaultValue defaultVal;   // Valeur par défaut
};

// TABLE DE VÉRITÉ UNIQUE - Synchronisée avec serveur distant
// Format: {gpio, type, nvsKey, serverPostName, internalName, description, defaultVal}
namespace GPIOMap {
    // ACTIONNEURS PHYSIQUES (états booléens)
    //                       GPIO  Type                NVS           POST              Interne         Desc                Default
    constexpr GPIOMapping PUMP_AQUA     = {16, GPIOType::ACTUATOR, "pump_aqua", "etatPompeAqua", "pumpAqua", "Pompe aquarium", false};
    constexpr GPIOMapping PUMP_TANK     = {18, GPIOType::ACTUATOR, "pump_tank", "etatPompeTank", "pumpTank", "Pompe réservoir", false};
    constexpr GPIOMapping HEATER        = {Pins::RADIATEURS, GPIOType::ACTUATOR, "heater", "etatHeat", "heater", "Chauffage", false};
    constexpr GPIOMapping LIGHT         = {15, GPIOType::ACTUATOR, "light", "etatUV", "light", "Lumière", false};
    
    // COMMANDES NOURRISSAGE (flags)
    constexpr GPIOMapping FEED_SMALL    = {108, GPIOType::ACTUATOR, "feed_small", "bouffePetits", "feedSmall", "Nourrir petits", false};
    constexpr GPIOMapping FEED_BIG      = {109, GPIOType::ACTUATOR, "feed_big", "bouffeGros", "feedBig", "Nourrir gros", false};
    
    // CONFIGURATION SYSTÈME
    constexpr GPIOMapping EMAIL_ADDR    = {100, GPIOType::CONFIG_STRING, "email", "mail", "emailAddress", "Email", ""};
    constexpr GPIOMapping EMAIL_EN      = {101, GPIOType::CONFIG_BOOL, "emailEn", "mailNotif", "emailEnabled", "Notif email", true};
    constexpr GPIOMapping AQ_THRESHOLD  = {102, GPIOType::CONFIG_INT, "aqThr", "aqThreshold", "aqThresholdCm", "Seuil aquarium", 18};
    constexpr GPIOMapping TANK_THRESHOLD= {103, GPIOType::CONFIG_INT, "tankThr", "tankThreshold", "tankThresholdCm", "Seuil réservoir", 80};
    constexpr GPIOMapping HEAT_THRESHOLD= {104, GPIOType::CONFIG_FLOAT, "heatThr", "chauffageThreshold", "heaterThresholdC", "Seuil chauffage", 25.0f};
    constexpr GPIOMapping FEED_MORNING  = {105, GPIOType::CONFIG_INT, "feedMorn", "bouffeMatin", "bouffeMatin", "Heure matin", 8};
    constexpr GPIOMapping FEED_NOON     = {106, GPIOType::CONFIG_INT, "feedNoon", "bouffeMidi", "bouffeMidi", "Heure midi", 12};
    constexpr GPIOMapping FEED_EVENING  = {107, GPIOType::CONFIG_INT, "feedEve", "bouffeSoir", "bouffeSoir", "Heure soir", 19};
    constexpr GPIOMapping RESET_CMD     = {110, GPIOType::ACTUATOR, "reset", "resetMode", "resetMode", "Reset ESP32", false};
    constexpr GPIOMapping FEED_BIG_DUR  = {111, GPIOType::CONFIG_INT, "feedBigD", "tempsGros", "tempsGros", "Durée gros", 3};
    constexpr GPIOMapping FEED_SMALL_DUR= {112, GPIOType::CONFIG_INT, "feedSmD", "tempsPetits", "tempsPetits", "Durée petits", 2};
    constexpr GPIOMapping REFILL_DUR    = {113, GPIOType::CONFIG_INT, "refillD", "tempsRemplissageSec", "refillDurationSec", "Durée remplissage", 120};
    constexpr GPIOMapping LIM_FLOOD     = {114, GPIOType::CONFIG_INT, "limFlood", "limFlood", "limFlood", "Limite inondation", 8};
    constexpr GPIOMapping WAKEUP        = {115, GPIOType::CONFIG_BOOL, "wakeup", "WakeUp", "forceWakeUp", "Forcer réveil", false};
    constexpr GPIOMapping FREQ_WAKEUP   = {116, GPIOType::CONFIG_INT, "freqWake", "FreqWakeUp", "freqWakeSec", "Fréq réveil", 600};
    
    // Array pour itération
    constexpr GPIOMapping ALL_MAPPINGS[] = {
        PUMP_AQUA, PUMP_TANK, HEATER, LIGHT,
        FEED_SMALL, FEED_BIG,
        EMAIL_ADDR, EMAIL_EN, AQ_THRESHOLD, TANK_THRESHOLD, HEAT_THRESHOLD,
        FEED_MORNING, FEED_NOON, FEED_EVENING,
        RESET_CMD, FEED_BIG_DUR, FEED_SMALL_DUR, REFILL_DUR, LIM_FLOOD,
        WAKEUP, FREQ_WAKEUP
    };
    constexpr size_t MAPPING_COUNT = sizeof(ALL_MAPPINGS) / sizeof(GPIOMapping);
    
    // Helper: Trouver mapping par GPIO
    inline const GPIOMapping* findByGPIO(uint8_t gpio) {
        for (size_t i = 0; i < MAPPING_COUNT; i++) {
            if (ALL_MAPPINGS[i].gpio == gpio) {
                return &ALL_MAPPINGS[i];
            }
        }
        return nullptr;
    }
    
    // v11.172: Helper - Trouver mapping par nom serveur POST
    inline const GPIOMapping* findByServerPostName(const char* name) {
        if (!name) return nullptr;
        for (size_t i = 0; i < MAPPING_COUNT; i++) {
            if (ALL_MAPPINGS[i].serverPostName && strcmp(ALL_MAPPINGS[i].serverPostName, name) == 0) {
                return &ALL_MAPPINGS[i];
            }
        }
        return nullptr;
    }
    
    // v11.172: Helper - Trouver mapping par nom interne firmware
    inline const GPIOMapping* findByInternalName(const char* name) {
        if (!name) return nullptr;
        for (size_t i = 0; i < MAPPING_COUNT; i++) {
            if (ALL_MAPPINGS[i].internalName && strcmp(ALL_MAPPINGS[i].internalName, name) == 0) {
                return &ALL_MAPPINGS[i];
            }
        }
        return nullptr;
    }
    
    // v11.172: Helper - Convertir nom serveur POST en GPIO
    inline uint8_t serverPostNameToGPIO(const char* name) {
        const GPIOMapping* m = findByServerPostName(name);
        return m ? m->gpio : 0;
    }
    
    // v11.172: Helper - Convertir GPIO en nom serveur POST
    inline const char* gpioToServerPostName(uint8_t gpio) {
        const GPIOMapping* m = findByGPIO(gpio);
        return m ? m->serverPostName : nullptr;
    }
    
    // v11.172: Helper - Convertir nom interne en nom serveur POST
    inline const char* internalToServerPost(const char* internalName) {
        const GPIOMapping* m = findByInternalName(internalName);
        return m ? m->serverPostName : internalName; // fallback to same name
    }
    
    // Helper: Vérifier si GPIO est connu
    inline bool isValidGPIO(uint8_t gpio) {
        return findByGPIO(gpio) != nullptr;
    }
    
    // Helper: Obtenir type GPIO
    inline GPIOType getType(uint8_t gpio) {
        const GPIOMapping* mapping = findByGPIO(gpio);
        return mapping ? mapping->type : GPIOType::CONFIG_INT;
    }
}
