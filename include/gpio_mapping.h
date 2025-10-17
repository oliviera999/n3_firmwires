#pragma once
#include "pins.h"
#include <cstdint>
#include <cstddef>

/**
 * GPIO Mapping - Source Unique de Vérité
 * 
 * Table centralisée de tous les GPIO (physiques 0-39 + virtuels 100-116)
 * Synchronisée avec ffp3/src/Controller/OutputController.php
 * 
 * Version: 11.68
 */

// Type de GPIO pour validation
enum class GPIOType {
    ACTUATOR,      // GPIO physiques (relais, LED)
    CONFIG_INT,    // Paramètres entiers
    CONFIG_FLOAT,  // Paramètres flottants
    CONFIG_STRING, // Paramètres texte (email)
    CONFIG_BOOL    // Paramètres booléens
};

// Configuration GPIO complète
struct GPIOMapping {
    uint8_t gpio;           // Numéro GPIO
    GPIOType type;          // Type de donnée
    const char* nvsKey;     // Clé NVS (max 15 chars)
    const char* description;// Description debug
};

// TABLE DE VÉRITÉ UNIQUE - Synchronisée avec serveur distant
namespace GPIOMap {
    // ACTIONNEURS PHYSIQUES
    constexpr GPIOMapping PUMP_AQUA     = {16, GPIOType::ACTUATOR, "pump_aqua", "Pompe aquarium"};
    constexpr GPIOMapping PUMP_TANK     = {18, GPIOType::ACTUATOR, "pump_tank", "Pompe réservoir"};
    constexpr GPIOMapping HEATER        = {Pins::RADIATEURS, GPIOType::ACTUATOR, "heater", "Chauffage"};
    constexpr GPIOMapping LIGHT         = {15, GPIOType::ACTUATOR, "light", "Lumière"};
    
    // COMMANDES NOURRISSAGE
    constexpr GPIOMapping FEED_SMALL    = {108, GPIOType::ACTUATOR, "feed_small", "Nourrir petits"};
    constexpr GPIOMapping FEED_BIG      = {109, GPIOType::ACTUATOR, "feed_big", "Nourrir gros"};
    
    // CONFIGURATION SYSTÈME
    constexpr GPIOMapping EMAIL_ADDR    = {100, GPIOType::CONFIG_STRING, "email", "Email"};
    constexpr GPIOMapping EMAIL_EN      = {101, GPIOType::CONFIG_BOOL, "emailEn", "Notif email"};
    constexpr GPIOMapping AQ_THRESHOLD  = {102, GPIOType::CONFIG_INT, "aqThr", "Seuil aquarium"};
    constexpr GPIOMapping TANK_THRESHOLD= {103, GPIOType::CONFIG_INT, "tankThr", "Seuil réservoir"};
    constexpr GPIOMapping HEAT_THRESHOLD= {104, GPIOType::CONFIG_FLOAT, "heatThr", "Seuil chauffage"};
    constexpr GPIOMapping FEED_MORNING  = {105, GPIOType::CONFIG_INT, "feedMorn", "Heure matin"};
    constexpr GPIOMapping FEED_NOON     = {106, GPIOType::CONFIG_INT, "feedNoon", "Heure midi"};
    constexpr GPIOMapping FEED_EVENING  = {107, GPIOType::CONFIG_INT, "feedEve", "Heure soir"};
    constexpr GPIOMapping RESET_CMD     = {110, GPIOType::ACTUATOR, "reset", "Reset ESP32"};
    constexpr GPIOMapping FEED_BIG_DUR  = {111, GPIOType::CONFIG_INT, "feedBigD", "Durée gros"};
    constexpr GPIOMapping FEED_SMALL_DUR= {112, GPIOType::CONFIG_INT, "feedSmD", "Durée petits"};
    constexpr GPIOMapping REFILL_DUR    = {113, GPIOType::CONFIG_INT, "refillD", "Durée remplissage"};
    constexpr GPIOMapping LIM_FLOOD     = {114, GPIOType::CONFIG_INT, "limFlood", "Limite inondation"};
    constexpr GPIOMapping WAKEUP        = {115, GPIOType::CONFIG_BOOL, "wakeup", "Forcer réveil"};
    constexpr GPIOMapping FREQ_WAKEUP   = {116, GPIOType::CONFIG_INT, "freqWake", "Fréq réveil"};
    
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
